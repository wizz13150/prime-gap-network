#include "device_control_sidecar.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

namespace FrontierDeviceControlSidecar {
namespace {

bool HasExactKeys(const QJsonObject& object, const QSet<QString>& expected)
{
    if (object.size() != expected.size()) return false;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!expected.contains(it.key())) return false;
    }
    return true;
}

bool ValidateRequest(const Request& request, QString& error)
{
    static const QRegularExpression requestIdPattern(
        QStringLiteral("\\A[a-zA-Z0-9_-]+\\z"));
    if (!requestIdPattern.match(request.requestId).hasMatch()) {
        error = QStringLiteral(
            "requestId must contain only ASCII letters, digits, '_' or '-'");
        return false;
    }
    if (request.deviceId.isEmpty()) {
        error = QStringLiteral("deviceId must not be empty");
        return false;
    }
    if (request.budgetKind != QLatin1String("cpu_threads") &&
        request.budgetKind != QLatin1String("gpu_power_work_share")) {
        error = QStringLiteral(
            "budgetKind must be 'cpu_threads' or 'gpu_power_work_share'");
        return false;
    }
    if (request.budgetPercent < 5 || request.budgetPercent > 100) {
        error = QStringLiteral("budgetPercent must be in [5,100]");
        return false;
    }
    if (!request.requestedUtc.isValid()) {
        error = QStringLiteral("requestedUtc must be valid");
        return false;
    }
    return true;
}

std::optional<Response> Rejected(const QString& message, QString& error)
{
    error = message;
    return std::nullopt;
}

void SkipJsonWhitespace(const QByteArray& json, qsizetype& offset)
{
    while (offset < json.size()) {
        const char character = json.at(offset);
        if (character != ' ' && character != '\t' &&
            character != '\r' && character != '\n') {
            return;
        }
        ++offset;
    }
}

bool SkipJsonString(const QByteArray& json, qsizetype& offset)
{
    if (offset >= json.size() || json.at(offset) != '"') return false;
    ++offset;
    while (offset < json.size()) {
        const unsigned char character = static_cast<unsigned char>(json.at(offset++));
        if (character == '"') return true;
        if (character < 0x20) return false;
        if (character != '\\') continue;
        if (offset >= json.size()) return false;
        const char escaped = json.at(offset++);
        if (escaped == 'u') {
            if (json.size() - offset < 4) return false;
            offset += 4;
        }
    }
    return false;
}

bool SkipJsonValue(const QByteArray& json, qsizetype& offset)
{
    SkipJsonWhitespace(json, offset);
    if (offset >= json.size()) return false;
    if (json.at(offset) == '"') return SkipJsonString(json, offset);

    if (json.at(offset) == '{' || json.at(offset) == '[') {
        QByteArray closingCharacters;
        closingCharacters.push_back(json.at(offset++) == '{' ? '}' : ']');
        while (offset < json.size() && !closingCharacters.isEmpty()) {
            const char character = json.at(offset);
            if (character == '"') {
                if (!SkipJsonString(json, offset)) return false;
            } else if (character == '{' || character == '[') {
                closingCharacters.push_back(character == '{' ? '}' : ']');
                ++offset;
            } else if (character == '}' || character == ']') {
                if (character != closingCharacters.back()) return false;
                closingCharacters.chop(1);
                ++offset;
            } else {
                ++offset;
            }
        }
        return closingCharacters.isEmpty();
    }

    const qsizetype start = offset;
    while (offset < json.size()) {
        const char character = json.at(offset);
        if (character == ',' || character == '}' || character == ']' ||
            character == ' ' || character == '\t' ||
            character == '\r' || character == '\n') {
            break;
        }
        ++offset;
    }
    return offset > start;
}

bool HasUniqueRootObjectKeys(const QByteArray& json)
{
    qsizetype offset = 0;
    SkipJsonWhitespace(json, offset);
    if (offset >= json.size() || json.at(offset++) != '{') return false;
    SkipJsonWhitespace(json, offset);
    if (offset < json.size() && json.at(offset) == '}') return true;

    QSet<QString> keys;
    while (offset < json.size()) {
        SkipJsonWhitespace(json, offset);
        const qsizetype keyStart = offset;
        if (!SkipJsonString(json, offset)) return false;
        const QByteArray encodedKey = json.mid(keyStart, offset - keyStart);
        QJsonParseError keyError;
        const QJsonDocument keyDocument = QJsonDocument::fromJson(
            QByteArrayLiteral("[") + encodedKey + QByteArrayLiteral("]"),
            &keyError);
        if (keyError.error != QJsonParseError::NoError ||
            !keyDocument.isArray() || keyDocument.array().size() != 1 ||
            !keyDocument.array().at(0).isString()) {
            return false;
        }
        const QString key = keyDocument.array().at(0).toString();
        if (keys.contains(key)) return false;
        keys.insert(key);

        SkipJsonWhitespace(json, offset);
        if (offset >= json.size() || json.at(offset++) != ':') return false;
        if (!SkipJsonValue(json, offset)) return false;
        SkipJsonWhitespace(json, offset);
        if (offset >= json.size()) return false;
        if (json.at(offset) == '}') return true;
        if (json.at(offset++) != ',') return false;
    }
    return false;
}

} // namespace

bool WriteRequestCreateOnly(const QString& directory,
                            const Request& request,
                            QString& error)
{
    error.clear();
    if (!ValidateRequest(request, error)) return false;

    const QFileInfo directoryInfo(directory);
    if (!directoryInfo.exists() || !directoryInfo.isDir()) {
        error = QStringLiteral("request directory does not exist or is not a directory");
        return false;
    }

    const QJsonObject root{
        {QStringLiteral("schema"), QLatin1String(REQUEST_SCHEMA)},
        {QStringLiteral("request_id"), request.requestId},
        {QStringLiteral("device_id"), request.deviceId},
        {QStringLiteral("budget_kind"), request.budgetKind},
        {QStringLiteral("enabled"), request.enabled},
        {QStringLiteral("budget_percent"), request.budgetPercent},
        {QStringLiteral("requested_utc"),
         request.requestedUtc.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("status"), QStringLiteral("pending")},
        {QStringLiteral("requires_backend_handler"), true},
    };
    const QByteArray encoded = QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';
    const QString path = QDir(directory).filePath(
        request.requestId + QStringLiteral(".request.json"));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        error = QStringLiteral("cannot create request '%1': %2")
                    .arg(path, file.errorString());
        return false;
    }

    if (file.write(encoded) != encoded.size() || !file.flush()) {
        const QString writeError = file.errorString();
        file.close();
        QFile::remove(path);
        error = QStringLiteral("cannot write request '%1': %2")
                    .arg(path, writeError);
        return false;
    }
    file.close();
    return true;
}

std::optional<Response> ParseResponse(const QByteArray& json,
                                      const Request& expected,
                                      QString& error)
{
    error.clear();
    if (json.size() > MAX_RESPONSE_BYTES) {
        return Rejected(QStringLiteral("response exceeds %1 bytes")
                            .arg(MAX_RESPONSE_BYTES), error);
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return Rejected(QStringLiteral("response JSON root invalid: %1")
                            .arg(parseError.errorString()), error);
    }
    if (!HasUniqueRootObjectKeys(json)) {
        return Rejected(QStringLiteral("response contains duplicate object keys"), error);
    }

    const QJsonObject root = document.object();
    static const QSet<QString> responseKeys{
        QStringLiteral("schema"),
        QStringLiteral("request_id"),
        QStringLiteral("device_id"),
        QStringLiteral("status"),
        QStringLiteral("detail"),
        QStringLiteral("budget_kind"),
        QStringLiteral("enabled"),
        QStringLiteral("budget_percent"),
        QStringLiteral("process_id"),
        QStringLiteral("telemetry_observed_utc"),
    };
    if (!HasExactKeys(root, responseKeys)) {
        return Rejected(QStringLiteral("response schema keys invalid"), error);
    }

    const QJsonValue schema = root.value(QStringLiteral("schema"));
    if (!schema.isString() || schema.toString() != QLatin1String(RESPONSE_SCHEMA)) {
        return Rejected(QStringLiteral("response schema identity invalid"), error);
    }

    const QJsonValue requestId = root.value(QStringLiteral("request_id"));
    if (!requestId.isString() || requestId.toString() != expected.requestId) {
        return Rejected(QStringLiteral("response request_id mismatch"), error);
    }

    const QJsonValue deviceId = root.value(QStringLiteral("device_id"));
    if (!deviceId.isString() || deviceId.toString() != expected.deviceId) {
        return Rejected(QStringLiteral("response device_id mismatch"), error);
    }

    const QJsonValue status = root.value(QStringLiteral("status"));
    if (!status.isString() ||
        (status.toString() != QLatin1String("applied") &&
         status.toString() != QLatin1String("rejected"))) {
        return Rejected(QStringLiteral(
                            "response status must be 'applied' or 'rejected'"),
                        error);
    }

    const QJsonValue detail = root.value(QStringLiteral("detail"));
    if (!detail.isString()) {
        return Rejected(QStringLiteral("response detail must be a string"), error);
    }

    const QJsonValue budgetKind = root.value(QStringLiteral("budget_kind"));
    const QJsonValue enabled = root.value(QStringLiteral("enabled"));
    const QJsonValue budgetPercent = root.value(QStringLiteral("budget_percent"));
    if (!budgetKind.isString() || budgetKind.toString() != expected.budgetKind ||
        !enabled.isBool() || enabled.toBool() != expected.enabled ||
        !budgetPercent.isDouble() || std::floor(budgetPercent.toDouble()) != budgetPercent.toDouble() ||
        budgetPercent.toInt() != expected.budgetPercent) {
        return Rejected(QStringLiteral("response command echo mismatch"), error);
    }
    const QJsonValue processId = root.value(QStringLiteral("process_id"));
    const QJsonValue telemetryObserved = root.value(QStringLiteral("telemetry_observed_utc"));
    std::optional<qint64> parsedProcessId;
    std::optional<QDateTime> parsedTelemetry;
    if (!processId.isNull()) {
        if (!processId.isDouble() || std::floor(processId.toDouble()) != processId.toDouble() ||
            processId.toDouble() < 1.0 || processId.toDouble() > 4294967295.0) {
            return Rejected(QStringLiteral("response process_id invalid"), error);
        }
        parsedProcessId = static_cast<qint64>(processId.toDouble());
    }
    if (!telemetryObserved.isNull()) {
        if (!telemetryObserved.isString()) {
            return Rejected(QStringLiteral("response telemetry_observed_utc invalid"), error);
        }
        const QDateTime timestamp = QDateTime::fromString(telemetryObserved.toString(), Qt::ISODateWithMs);
        if (!timestamp.isValid()) {
            return Rejected(QStringLiteral("response telemetry_observed_utc invalid"), error);
        }
        parsedTelemetry = timestamp.toUTC();
    }
    const bool applied = status.toString() == QLatin1String("applied");
    if (applied && (!parsedTelemetry || (expected.enabled && !parsedProcessId))) {
        return Rejected(QStringLiteral("APPLIED requires confirming telemetry and an enabled-process PID"), error);
    }
    if (!applied && (parsedProcessId || parsedTelemetry)) {
        return Rejected(QStringLiteral("REJECTED response must not claim applied evidence"), error);
    }

    Response response;
    response.requestId = requestId.toString();
    response.deviceId = deviceId.toString();
    response.status = status.toString();
    response.detail = detail.toString();
    response.budgetKind = budgetKind.toString();
    response.enabled = enabled.toBool();
    response.budgetPercent = budgetPercent.toInt();
    response.processId = parsedProcessId;
    response.telemetryObservedUtc = parsedTelemetry;
    return response;
}

std::optional<Response> ReadResponse(const QString& path,
                                     const Request& expected,
                                     QString& error)
{
    error.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Rejected(QStringLiteral("cannot open response '%1': %2")
                            .arg(path, file.errorString()), error);
    }
    if (file.size() > MAX_RESPONSE_BYTES) {
        return Rejected(QStringLiteral("response exceeds %1 bytes")
                            .arg(MAX_RESPONSE_BYTES), error);
    }

    const QByteArray json = file.read(MAX_RESPONSE_BYTES + 1);
    if (json.size() > MAX_RESPONSE_BYTES) {
        return Rejected(QStringLiteral("response exceeds %1 bytes")
                            .arg(MAX_RESPONSE_BYTES), error);
    }
    if (file.error() != QFileDevice::NoError) {
        return Rejected(QStringLiteral("cannot read response '%1': %2")
                            .arg(path, file.errorString()), error);
    }
    return ParseResponse(json, expected, error);
}

} // namespace FrontierDeviceControlSidecar

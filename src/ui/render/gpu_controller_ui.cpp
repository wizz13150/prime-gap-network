#include "gpu_controller_ui.h"

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

#include <algorithm>
#include <cmath>

namespace FrontierGpuControllerUi {
namespace {

bool HasExactKeys(const QJsonObject& object,
                  const QSet<QString>& allowed,
                  const QSet<QString>& required,
                  QString& error)
{
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        if (!allowed.contains(iterator.key())) {
            error = QStringLiteral("unknown JSON key '%1'").arg(iterator.key());
            return false;
        }
    }
    for (const QString& key : required) {
        if (!object.contains(key)) {
            error = QStringLiteral("required JSON key '%1' is missing").arg(key);
            return false;
        }
    }
    return true;
}

void SkipWhitespace(const QByteArray& json, qsizetype& offset)
{
    while (offset < json.size()) {
        const char character = json.at(offset);
        if (character != ' ' && character != '\t' && character != '\r' && character != '\n') {
            break;
        }
        ++offset;
    }
}

bool ScanString(const QByteArray& json, qsizetype& offset)
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
            for (qsizetype index = 0; index < 4; ++index) {
                const char digit = json.at(offset + index);
                const bool hexadecimal = (digit >= '0' && digit <= '9') ||
                    (digit >= 'a' && digit <= 'f') || (digit >= 'A' && digit <= 'F');
                if (!hexadecimal) return false;
            }
            offset += 4;
        } else if (escaped != '"' && escaped != '\\' && escaped != '/' &&
                   escaped != 'b' && escaped != 'f' && escaped != 'n' &&
                   escaped != 'r' && escaped != 't') {
            return false;
        }
    }
    return false;
}

bool ScanJsonValue(const QByteArray& json, qsizetype& offset)
{
    SkipWhitespace(json, offset);
    if (offset >= json.size()) return false;
    if (json.at(offset) == '"') return ScanString(json, offset);
    if (json.at(offset) == '{') {
        ++offset;
        SkipWhitespace(json, offset);
        if (offset < json.size() && json.at(offset) == '}') {
            ++offset;
            return true;
        }
        QSet<QString> keys;
        while (offset < json.size()) {
            SkipWhitespace(json, offset);
            const qsizetype keyStart = offset;
            if (!ScanString(json, offset)) return false;
            const QByteArray encodedKey = json.mid(keyStart, offset - keyStart);
            QJsonParseError keyError;
            const QJsonDocument keyDocument = QJsonDocument::fromJson(
                QByteArrayLiteral("[") + encodedKey + QByteArrayLiteral("]"), &keyError);
            if (keyError.error != QJsonParseError::NoError ||
                !keyDocument.isArray() || keyDocument.array().size() != 1 ||
                !keyDocument.array().at(0).isString()) {
                return false;
            }
            const QString key = keyDocument.array().at(0).toString();
            if (keys.contains(key)) return false;
            keys.insert(key);
            SkipWhitespace(json, offset);
            if (offset >= json.size() || json.at(offset++) != ':') return false;
            if (!ScanJsonValue(json, offset)) return false;
            SkipWhitespace(json, offset);
            if (offset >= json.size()) return false;
            if (json.at(offset) == '}') {
                ++offset;
                return true;
            }
            if (json.at(offset++) != ',') return false;
        }
        return false;
    }
    if (json.at(offset) == '[') {
        ++offset;
        SkipWhitespace(json, offset);
        if (offset < json.size() && json.at(offset) == ']') {
            ++offset;
            return true;
        }
        while (offset < json.size()) {
            if (!ScanJsonValue(json, offset)) return false;
            SkipWhitespace(json, offset);
            if (offset >= json.size()) return false;
            if (json.at(offset) == ']') {
                ++offset;
                return true;
            }
            if (json.at(offset++) != ',') return false;
        }
        return false;
    }

    const qsizetype primitiveStart = offset;
    while (offset < json.size()) {
        const char character = json.at(offset);
        if (character == ',' || character == ']' || character == '}' ||
            character == ' ' || character == '\t' || character == '\r' || character == '\n') {
            break;
        }
        ++offset;
    }
    return offset > primitiveStart;
}

bool HasUniqueObjectKeys(const QByteArray& json)
{
    qsizetype offset = 0;
    if (!ScanJsonValue(json, offset)) return false;
    SkipWhitespace(json, offset);
    return offset == json.size();
}

bool ReadBounded(const QString& path, qint64 maximum, QByteArray& bytes, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("cannot open '%1': %2").arg(path, file.errorString());
        return false;
    }
    if (file.size() > maximum) {
        error = QStringLiteral("'%1' exceeds %2 bytes").arg(path).arg(maximum);
        return false;
    }
    bytes = file.read(maximum + 1);
    if (bytes.size() > maximum || file.error() != QFileDevice::NoError) {
        error = QStringLiteral("cannot read bounded file '%1': %2")
                    .arg(path, file.errorString());
        return false;
    }
    return true;
}

bool ReadTelemetryTail(const QString& path, QByteArray& bytes, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("cannot open telemetry '%1': %2")
                    .arg(path, file.errorString());
        return false;
    }
    const qint64 size = file.size();
    const qint64 offset = std::max<qint64>(0, size - MAX_TELEMETRY_TAIL_BYTES);
    if (!file.seek(offset)) {
        error = QStringLiteral("cannot seek telemetry '%1'").arg(path);
        return false;
    }
    bytes = file.read(MAX_TELEMETRY_TAIL_BYTES);
    if (file.error() != QFileDevice::NoError) {
        error = QStringLiteral("cannot read telemetry '%1': %2")
                    .arg(path, file.errorString());
        return false;
    }
    if (offset > 0) {
        const qsizetype firstLine = bytes.indexOf('\n');
        if (firstLine < 0) {
            error = QStringLiteral("telemetry record exceeds bounded tail size");
            return false;
        }
        bytes.remove(0, firstLine + 1);
    }
    return true;
}

bool ParseObject(const QByteArray& bytes, QJsonObject& object, QString& error)
{
    if (!HasUniqueObjectKeys(bytes)) {
        error = QStringLiteral("JSON contains duplicate keys or invalid structure");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("JSON object is invalid: %1").arg(parseError.errorString());
        return false;
    }
    object = document.object();
    return true;
}

bool JsonFinite(const QJsonValue& value, double& result)
{
    if (!value.isDouble()) return false;
    result = value.toDouble();
    return std::isfinite(result);
}

bool JsonString(const QJsonObject& object, const QString& key, QString& result)
{
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) return false;
    result = value.toString();
    return true;
}

bool CanonicalGpuUuid(const QString& value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^GPU-[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"));
    return pattern.match(value).hasMatch();
}

QString FindDeviceKey(const QHash<QString, DeviceStatus>& devices, const QString& uuid)
{
    for (auto iterator = devices.constBegin(); iterator != devices.constEnd(); ++iterator) {
        if (iterator.key().compare(uuid, Qt::CaseInsensitive) == 0) return iterator.key();
    }
    return {};
}

void SetError(Snapshot& snapshot, const QString& detail)
{
    if (detail.isEmpty()) return;
    if (!snapshot.error.isEmpty()) snapshot.error += QStringLiteral(" · ");
    snapshot.error += detail;
}

QString OwnershipText(const DeviceStatus& device)
{
    if (!device.ownershipObserved) return QStringLiteral("WAITING");
    return device.ownershipVerified ? QStringLiteral("VERIFIED") : QStringLiteral("BLOCKED");
}

QString ReadbackText(const DeviceStatus& device, bool dryRun)
{
    if (dryRun) return QStringLiteral("DRY-RUN / NO WRITE");
    if (!device.readbackObserved) return QStringLiteral("WAITING");
    return device.readbackVerified ? QStringLiteral("VERIFIED") : QStringLiteral("BLOCKED");
}

QString RestoreText(const DeviceStatus& device, bool restoreOnExit)
{
    if (!restoreOnExit) return QStringLiteral("DISABLED");
    if (!device.restoreObserved) return QStringLiteral("WAITING");
    return device.restoreVerified ? QStringLiteral("VERIFIED") : QStringLiteral("ATTENTION");
}

void MergeTelemetryRecord(Snapshot& snapshot, const QJsonObject& root)
{
    const QJsonValue liveValue = root.value(QStringLiteral("controller_live_mutations"));
    if (!liveValue.isBool()) {
        SetError(snapshot, QStringLiteral("telemetry live-mutation gate is missing"));
        return;
    }
    if (liveValue.toBool()) snapshot.liveMutationViolation = true;

    QString uuid;
    if (!JsonString(root, QStringLiteral("uuid"), uuid)) return;
    const QString key = FindDeviceKey(snapshot.devices, uuid);
    if (key.isEmpty()) return;
    DeviceStatus& device = snapshot.devices[key];
    const QString event = root.value(QStringLiteral("event")).toString();
    if (event == QLatin1String("telemetry")) {
        device.ownershipObserved = root.contains(QStringLiteral("ownership_verified"));
        if (device.ownershipObserved && root.value(QStringLiteral("ownership_verified")).isBool()) {
            device.ownershipVerified = root.value(QStringLiteral("ownership_verified")).toBool();
        } else {
            device.ownershipObserved = false;
            SetError(snapshot, QStringLiteral("ownership evidence is invalid"));
        }
        device.ownershipDetail = root.value(QStringLiteral("ownership_detail")).toString();
        if (root.value(QStringLiteral("mode")).isString()) {
            device.mode = root.value(QStringLiteral("mode")).toString();
        }
        if (root.value(QStringLiteral("enabled")).isBool()) {
            device.enabled = root.value(QStringLiteral("enabled")).toBool();
        }
        const QJsonObject observed = root.value(QStringLiteral("device")).toObject();
        double temperature = 0.0;
        if (JsonFinite(observed.value(QStringLiteral("temperature_c")), temperature)) {
            device.temperatureC = temperature;
        }
        const QJsonArray actions = root.value(QStringLiteral("actions")).toArray();
        if (!actions.isEmpty()) {
            device.readbackObserved = true;
            bool allVerified = true;
            QStringList details;
            for (const QJsonValue& value : actions) {
                const QJsonObject action = value.toObject();
                if (!action.value(QStringLiteral("verified")).isBool()) {
                    allVerified = false;
                    details.append(QStringLiteral("invalid read-back record"));
                    continue;
                }
                allVerified = allVerified && action.value(QStringLiteral("verified")).toBool();
                const QString detail = action.value(QStringLiteral("detail")).toString();
                if (!detail.isEmpty()) details.append(detail);
            }
            device.readbackVerified = allVerified;
            device.readbackDetail = details.join(QStringLiteral(" · "));
        }
    } else if (event == QLatin1String("restore")) {
        device.restoreObserved = true;
        const QJsonArray actions = root.value(QStringLiteral("actions")).toArray();
        if (actions.isEmpty()) {
            device.restoreVerified = false;
            device.restoreDetail = QStringLiteral("restore event has no action evidence");
            return;
        }
        bool allVerified = true;
        QStringList details;
        for (const QJsonValue& value : actions) {
            const QJsonObject action = value.toObject();
            if (!action.value(QStringLiteral("verified")).isBool()) {
                allVerified = false;
                details.append(QStringLiteral("invalid restore record"));
                continue;
            }
            allVerified = allVerified && action.value(QStringLiteral("verified")).toBool();
            const QString detail = action.value(QStringLiteral("detail")).toString();
            if (!detail.isEmpty()) details.append(detail);
        }
        device.restoreVerified = allVerified;
        device.restoreDetail = details.join(QStringLiteral(" · "));
    }
}

bool ParseConfig(const QByteArray& bytes, Snapshot& snapshot, QString& error)
{
    QJsonObject root;
    if (!ParseObject(bytes, root, error)) return false;
    const QSet<QString> allowed{
        QStringLiteral("schema"), QStringLiteral("state_dir"),
        QStringLiteral("telemetry_path"), QStringLiteral("poll_interval_seconds"),
        QStringLiteral("temperature"), QStringLiteral("live_mutations"),
        QStringLiteral("restore_on_exit"), QStringLiteral("owner_required"),
        QStringLiteral("devices")};
    if (!HasExactKeys(root, allowed, {QStringLiteral("schema"), QStringLiteral("devices")}, error)) {
        return false;
    }
    if (root.value(QStringLiteral("schema")).toString() != QLatin1String(CONFIG_SCHEMA)) {
        error = QStringLiteral("config schema identity is invalid");
        return false;
    }

    // Requiring the explicit false value keeps the UI from treating a default
    // or an omitted field as proof that no live write gate was supplied.
    if (!root.value(QStringLiteral("live_mutations")).isBool() ||
        root.value(QStringLiteral("live_mutations")).toBool()) {
        error = QStringLiteral("live_mutations must be explicitly false");
        return false;
    }
    snapshot.dryRunExplicit = true;
    snapshot.restoreOnExit = root.contains(QStringLiteral("restore_on_exit"))
        ? root.value(QStringLiteral("restore_on_exit")).toBool() : true;
    if (root.contains(QStringLiteral("restore_on_exit")) &&
        !root.value(QStringLiteral("restore_on_exit")).isBool()) {
        error = QStringLiteral("restore_on_exit must be boolean");
        return false;
    }
    if (!snapshot.restoreOnExit) {
        error = QStringLiteral("restore_on_exit must remain enabled");
        return false;
    }
    snapshot.ownerRequired = root.contains(QStringLiteral("owner_required"))
        ? root.value(QStringLiteral("owner_required")).toBool() : true;
    if (root.contains(QStringLiteral("owner_required")) &&
        !root.value(QStringLiteral("owner_required")).isBool()) {
        error = QStringLiteral("owner_required must be boolean");
        return false;
    }
    if (!snapshot.ownerRequired) {
        error = QStringLiteral("owner_required must remain enabled");
        return false;
    }

    const QJsonObject temperature = root.value(QStringLiteral("temperature")).toObject();
    if (root.contains(QStringLiteral("temperature")) && temperature.isEmpty() &&
        !root.value(QStringLiteral("temperature")).isObject()) {
        error = QStringLiteral("temperature must be an object");
        return false;
    }
    if (!HasExactKeys(temperature,
                      {QStringLiteral("lower_c"), QStringLiteral("upper_c")}, {}, error)) {
        return false;
    }
    if (temperature.contains(QStringLiteral("lower_c")) &&
        !JsonFinite(temperature.value(QStringLiteral("lower_c")), snapshot.lowerC)) {
        error = QStringLiteral("temperature.lower_c must be finite");
        return false;
    }
    if (temperature.contains(QStringLiteral("upper_c")) &&
        !JsonFinite(temperature.value(QStringLiteral("upper_c")), snapshot.upperC)) {
        error = QStringLiteral("temperature.upper_c must be finite");
        return false;
    }
    if (std::abs(snapshot.lowerC - 82.0) > 1.0e-9 ||
        std::abs(snapshot.upperC - 84.0) > 1.0e-9) {
        error = QStringLiteral("temperature hysteresis must be exactly 82–84 C");
        return false;
    }

    QString stateDir;
    if (root.contains(QStringLiteral("state_dir")) &&
        !JsonString(root, QStringLiteral("state_dir"), stateDir)) {
        error = QStringLiteral("state_dir must be a non-empty path");
        return false;
    }
    if (stateDir.isEmpty()) stateDir = QStringLiteral("runtime/gpu-control");
    const QFileInfo configInfo(snapshot.configPath);
    const QString configBase = configInfo.absolutePath();
    QDir stateDirectory(QDir::isAbsolutePath(stateDir)
                            ? stateDir
                            : QDir(configBase).filePath(stateDir));
    snapshot.statePath = stateDirectory.filePath(QStringLiteral("controller-state.json"));

    QString configuredTelemetryPath;
    if (root.contains(QStringLiteral("telemetry_path")) &&
        !root.value(QStringLiteral("telemetry_path")).isNull() &&
        !JsonString(root, QStringLiteral("telemetry_path"), configuredTelemetryPath)) {
        error = QStringLiteral("telemetry_path must be a non-empty path or null");
        return false;
    }
    QString telemetryPath;
    if (!configuredTelemetryPath.isEmpty()) telemetryPath = configuredTelemetryPath;
    if (telemetryPath.isEmpty()) {
        snapshot.telemetryPath = stateDirectory.filePath(QStringLiteral("telemetry.jsonl"));
    } else {
        snapshot.telemetryPath = QDir::isAbsolutePath(telemetryPath)
            ? telemetryPath : stateDirectory.filePath(telemetryPath);
    }

    const QJsonValue rawDevices = root.value(QStringLiteral("devices"));
    if (!rawDevices.isArray() || rawDevices.toArray().isEmpty()) {
        error = QStringLiteral("devices must be a non-empty array");
        return false;
    }
    const QSet<QString> deviceAllowed{
        QStringLiteral("uuid"), QStringLiteral("enabled"),
        QStringLiteral("normal_power_limit_w"), QStringLiteral("thermal_power_limit_w"),
        QStringLiteral("normal_fan_percent"), QStringLiteral("thermal_fan_percent"),
        QStringLiteral("normal_work_percent"), QStringLiteral("thermal_work_percent"),
        QStringLiteral("allowed_process_names")};
    const QJsonArray devices = rawDevices.toArray();
    for (qsizetype index = 0; index < devices.size(); ++index) {
        if (!devices.at(index).isObject()) {
            error = QStringLiteral("devices[%1] must be an object").arg(index);
            return false;
        }
        const QJsonObject device = devices.at(index).toObject();
        if (!HasExactKeys(device, deviceAllowed, {QStringLiteral("uuid")}, error)) return false;
        QString uuid;
        if (!JsonString(device, QStringLiteral("uuid"), uuid) || !CanonicalGpuUuid(uuid)) {
            error = QStringLiteral("devices[%1].uuid must be a canonical GPU UUID").arg(index);
            return false;
        }
        if (snapshot.devices.contains(uuid)) {
            error = QStringLiteral("duplicate GPU UUID '%1'").arg(uuid);
            return false;
        }
        if (device.contains(QStringLiteral("enabled")) &&
            !device.value(QStringLiteral("enabled")).isBool()) {
            error = QStringLiteral("devices[%1].enabled must be boolean").arg(index);
            return false;
        }
        DeviceStatus status;
        status.uuid = uuid;
        status.configured = true;
        status.enabled = device.contains(QStringLiteral("enabled"))
            ? device.value(QStringLiteral("enabled")).toBool() : false;
        snapshot.devices.insert(uuid, status);
    }
    return true;
}

void ParseState(Snapshot& snapshot, const QByteArray& bytes)
{
    QJsonObject root;
    QString error;
    if (!ParseObject(bytes, root, error)) {
        SetError(snapshot, QStringLiteral("state invalid: %1").arg(error));
        return;
    }
    if (root.value(QStringLiteral("schema")).toString() != QLatin1String(STATE_SCHEMA) ||
        !root.value(QStringLiteral("devices")).isObject()) {
        SetError(snapshot, QStringLiteral("state schema identity is invalid"));
        return;
    }
    const QJsonObject devices = root.value(QStringLiteral("devices")).toObject();
    for (auto iterator = devices.constBegin(); iterator != devices.constEnd(); ++iterator) {
        const QString key = FindDeviceKey(snapshot.devices, iterator.key());
        if (key.isEmpty() || !iterator.value().isObject()) continue;
        const QJsonObject state = iterator.value().toObject();
        DeviceStatus& device = snapshot.devices[key];
        if (state.value(QStringLiteral("mode")).isString()) {
            device.mode = state.value(QStringLiteral("mode")).toString();
        }
        const QJsonObject observed = state.value(QStringLiteral("last_observed")).toObject();
        double temperature = 0.0;
        if (JsonFinite(observed.value(QStringLiteral("temperature_c")), temperature)) {
            device.temperatureC = temperature;
        }
        if (state.value(QStringLiteral("restored_utc")).isString()) {
            device.restoreObserved = true;
            device.restoreDetail = QStringLiteral("restore timestamp persisted");
        }
    }
    snapshot.stateAvailable = true;
}

void ParseTelemetry(Snapshot& snapshot, const QByteArray& bytes)
{
    const QList<QByteArray> lines = bytes.split('\n');
    bool validRecord = false;
    for (const QByteArray& line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        QJsonObject root;
        QString error;
        if (!ParseObject(trimmed, root, error)) {
            SetError(snapshot, QStringLiteral("telemetry invalid: %1").arg(error));
            continue;
        }
        if (root.value(QStringLiteral("schema")).toString() != QLatin1String(TELEMETRY_SCHEMA)) {
            SetError(snapshot, QStringLiteral("telemetry schema identity is invalid"));
            continue;
        }
        validRecord = true;
        MergeTelemetryRecord(snapshot, root);
    }
    snapshot.telemetryAvailable = validRecord;
}

} // namespace

Snapshot DisabledSnapshot()
{
    Snapshot snapshot;
    snapshot.configured = false;
    snapshot.configValid = true;
    snapshot.error.clear();
    return snapshot;
}

QString Snapshot::controllerSummary() const
{
    if (!configured) return QStringLiteral("THERMAL GUARD OFF · OPTIONAL · NO CONFIG");
    if (!configValid) {
        return QStringLiteral("THERMAL GUARD FAIL-CLOSED · CONFIG INVALID");
    }
    int enabledCount = 0;
    int ownershipBlocked = 0;
    int ownershipWaiting = 0;
    int ownershipVerified = 0;
    int restoreAttention = 0;
    int restoreVerified = 0;
    for (const DeviceStatus& device : devices) {
        if (!device.enabled) continue;
        ++enabledCount;
        if (!device.ownershipObserved) ++ownershipWaiting;
        else if (device.ownershipVerified) ++ownershipVerified;
        else ++ownershipBlocked;
        if (device.restoreObserved && device.restoreVerified) ++restoreVerified;
        else if (device.restoreObserved) ++restoreAttention;
    }
    const QString ownership = enabledCount == 0
        ? QStringLiteral("N/A")
        : ownershipBlocked > 0 ? QStringLiteral("BLOCKED")
        : ownershipWaiting > 0 ? QStringLiteral("WAITING")
        : QStringLiteral("VERIFIED");
    const QString restore = !restoreOnExit
        ? QStringLiteral("DISABLED")
        : restoreAttention > 0 ? QStringLiteral("ATTENTION")
        : restoreVerified == enabledCount && enabledCount > 0
            ? QStringLiteral("VERIFIED") : QStringLiteral("ENABLED/WAITING");
    const QString dryRun = dryRunExplicit && !liveMutationViolation
        ? QStringLiteral("DRY-RUN EXPLICIT") : QStringLiteral("DRY-RUN VIOLATION");
    const QString safety = liveMutationViolation || !error.isEmpty()
        ? QStringLiteral("FAIL-CLOSED/ATTENTION") : QStringLiteral("FAIL-CLOSED");
    return QStringLiteral("THERMAL GUARD %1 UUID(S) · 82–84 C · %2 · OWNERSHIP %3 · READBACK %4 · RESTORE %5 · %6")
        .arg(enabledCount)
        .arg(dryRun, ownership, dryRunExplicit ? QStringLiteral("DRY-RUN/NO WRITE") : QStringLiteral("WAITING"), restore, safety);
}

QString Snapshot::deviceSummary(const QString& uuid) const
{
    const QString key = FindDeviceKey(devices, uuid);
    if (!configured) return QStringLiteral("UUID %1 · THERMAL GUARD OFF · NO CONFIG").arg(uuid);
    if (!configValid) return QStringLiteral("UUID %1 · THERMAL GUARD FAIL-CLOSED").arg(uuid);
    if (key.isEmpty()) return QStringLiteral("UUID %1 · THERMAL GUARD OFF · UUID NOT CONFIGURED").arg(uuid);
    const DeviceStatus& device = devices.value(key);
    const QString temperature = device.temperatureC
        ? QStringLiteral(" · %1 C").arg(*device.temperatureC, 0, 'f', 0) : QString{};
    return QStringLiteral("UUID %1 · %2 · BAND 82–84 C · %3 · OWNERSHIP %4 · READBACK %5 · RESTORE %6%7")
        .arg(device.uuid,
             device.enabled ? QStringLiteral("ENABLED") : QStringLiteral("DISABLED"),
             dryRunExplicit && !liveMutationViolation
                 ? QStringLiteral("DRY-RUN") : QStringLiteral("FAIL-CLOSED"),
             OwnershipText(device), ReadbackText(device, dryRunExplicit && !liveMutationViolation),
             RestoreText(device, restoreOnExit), temperature);
}

bool LoadSnapshot(const QString& configPath, Snapshot& snapshot, QString& error)
{
    error.clear();
    if (configPath.trimmed().isEmpty()) {
        snapshot = DisabledSnapshot();
        return true;
    }

    snapshot = Snapshot{};
    snapshot.configured = true;
    snapshot.configPath = QFileInfo(configPath).absoluteFilePath();
    QByteArray configBytes;
    if (!ReadBounded(snapshot.configPath, MAX_CONFIG_BYTES, configBytes, error)) {
        snapshot.error = error;
        return false;
    }
    if (!ParseConfig(configBytes, snapshot, error)) {
        snapshot.error = error;
        return false;
    }
    snapshot.configValid = true;

    QByteArray stateBytes;
    QString stateError;
    if (ReadBounded(snapshot.statePath, MAX_STATE_BYTES, stateBytes, stateError)) {
        ParseState(snapshot, stateBytes);
    } else {
        SetError(snapshot, QStringLiteral("state unavailable: %1").arg(stateError));
    }

    QByteArray telemetryBytes;
    QString telemetryError;
    if (ReadTelemetryTail(snapshot.telemetryPath, telemetryBytes, telemetryError)) {
        ParseTelemetry(snapshot, telemetryBytes);
    } else {
        SetError(snapshot, QStringLiteral("telemetry unavailable: %1").arg(telemetryError));
    }
    error = snapshot.error;
    // A valid config with no controller process is still a useful UI state;
    // the missing evidence remains visible as WAITING/FAIL-CLOSED.
    return true;
}

} // namespace FrontierGpuControllerUi

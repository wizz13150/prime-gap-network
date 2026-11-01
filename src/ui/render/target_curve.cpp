#include "target_curve.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <utility>

namespace {

constexpr double MAX_EXACT_JSON_INTEGER = 9'007'199'254'740'991.0;

FrontierTargetCurveLoadResult Rejected(const QString& error)
{
    FrontierTargetCurveLoadResult result;
    result.error = error;
    return result;
}

bool HasExactKeys(const QJsonObject& object, const QSet<QString>& expected)
{
    if (object.size() != expected.size()) return false;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!expected.contains(it.key())) return false;
    }
    return true;
}

bool ParseNonNegativeJsonInteger(const QJsonValue& value, qint64& out)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 || number > MAX_EXACT_JSON_INTEGER ||
        std::floor(number) != number) {
        return false;
    }
    out = static_cast<qint64>(number);
    return true;
}

bool ParseCanonicalPositiveU64(const QJsonValue& value, quint64& out)
{
    if (!value.isString()) return false;
    static const QRegularExpression canonicalDecimal(
        QStringLiteral("^[1-9][0-9]*$"));
    const QString text = value.toString();
    if (!canonicalDecimal.match(text).hasMatch()) return false;
    bool ok = false;
    out = text.toULongLong(&ok, 10);
    return ok && out > 0;
}

QDateTime ParseExplicitUtc(const QJsonValue& value)
{
    if (!value.isString()) return {};
    const QString text = value.toString();
    if (!text.endsWith(QLatin1Char('Z'))) return {};
    QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!parsed.isValid()) parsed = QDateTime::fromString(text, Qt::ISODate);
    return parsed.isValid() ? parsed.toUTC() : QDateTime{};
}

QString PointFieldError(qsizetype index, const QString& field)
{
    return QStringLiteral("points[%1].%2 invalid").arg(index).arg(field);
}

} // namespace

const FrontierTargetCurvePoint* FrontierTargetCurveSnapshot::lookupAtOrBefore(
    qint64 unixSeconds) const noexcept
{
    const FrontierTargetCurvePoint* found = nullptr;
    for (const FrontierTargetCurvePoint& point : points) {
        if (point.blockTime > unixSeconds) continue;
        if (found == nullptr || point.blockTime > found->blockTime ||
            (point.blockTime == found->blockTime && point.height > found->height)) {
            found = &point;
        }
    }
    return found;
}

const FrontierTargetCurvePoint* FrontierTargetCurveSnapshot::lookupAtOrBefore(
    const QDateTime& timestamp) const noexcept
{
    return timestamp.isValid() ? lookupAtOrBefore(timestamp.toSecsSinceEpoch()) : nullptr;
}

FrontierTargetCurveLoadResult FrontierParseTargetCurveSnapshot(
    const QByteArray& json,
    const QDateTime& nowUtc)
{
    if (!nowUtc.isValid()) return Rejected(QStringLiteral("nowUtc invalid"));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return Rejected(QStringLiteral("JSON root invalid: %1").arg(parseError.errorString()));
    }

    const QJsonObject root = document.object();
    static const QSet<QString> rootKeys{
        QStringLiteral("schema"), QStringLiteral("source"),
        QStringLiteral("observed_utc"), QStringLiteral("points")};
    if (!HasExactKeys(root, rootKeys)) {
        return Rejected(QStringLiteral("root schema keys invalid"));
    }
    if (!root.value(QStringLiteral("schema")).isString() ||
        root.value(QStringLiteral("schema")).toString() !=
            QLatin1String(FRONTIER_TARGET_CURVE_SCHEMA)) {
        return Rejected(QStringLiteral("schema identity invalid"));
    }

    if (!root.value(QStringLiteral("source")).isObject()) {
        return Rejected(QStringLiteral("source invalid"));
    }
    const QJsonObject source = root.value(QStringLiteral("source")).toObject();
    static const QSet<QString> sourceKeys{
        QStringLiteral("rpc"), QStringLiteral("chain"), QStringLiteral("genesis")};
    if (!HasExactKeys(source, sourceKeys)) {
        return Rejected(QStringLiteral("source schema keys invalid"));
    }
    if (!source.value(QStringLiteral("rpc")).isString() ||
        source.value(QStringLiteral("rpc")).toString() !=
            QLatin1String(FRONTIER_TARGET_CURVE_RPC_SOURCE) ||
        !source.value(QStringLiteral("chain")).isString() ||
        source.value(QStringLiteral("chain")).toString() !=
            QLatin1String(FRONTIER_TARGET_CURVE_CHAIN) ||
        !source.value(QStringLiteral("genesis")).isString() ||
        source.value(QStringLiteral("genesis")).toString() !=
            QLatin1String(FRONTIER_TARGET_CURVE_GENESIS)) {
        return Rejected(QStringLiteral("source identity invalid"));
    }

    const QDateTime observedUtc = ParseExplicitUtc(root.value(QStringLiteral("observed_utc")));
    if (!observedUtc.isValid()) {
        return Rejected(QStringLiteral("observed_utc invalid or not explicit UTC"));
    }
    const qint64 ageMs = observedUtc.msecsTo(nowUtc.toUTC());
    if (ageMs < -FRONTIER_TARGET_CURVE_MAX_AGE_MS ||
        ageMs > FRONTIER_TARGET_CURVE_MAX_AGE_MS) {
        return Rejected(QStringLiteral("observed_utc outside 30-second freshness window"));
    }

    if (!root.value(QStringLiteral("points")).isArray()) {
        return Rejected(QStringLiteral("points invalid"));
    }
    const QJsonArray points = root.value(QStringLiteral("points")).toArray();
    if (points.isEmpty() || points.size() > 512) {
        return Rejected(QStringLiteral("points count must be in [1,512]"));
    }

    FrontierTargetCurveSnapshot snapshot;
    snapshot.observedUtc = observedUtc;
    snapshot.points.reserve(points.size());
    static const QSet<QString> pointKeys{
        QStringLiteral("height"), QStringLiteral("block_hash"),
        QStringLiteral("block_time"), QStringLiteral("work_type"),
        QStringLiteral("confirmations"), QStringLiteral("required_merit_q48")};
    static const QRegularExpression blockHashPattern(
        QStringLiteral("^[0-9a-fA-F]{64}$"));

    qint64 previousHeight = -1;
    for (qsizetype index = 0; index < points.size(); ++index) {
        const QJsonValue value = points.at(index);
        if (!value.isObject()) {
            return Rejected(QStringLiteral("points[%1] invalid").arg(index));
        }
        const QJsonObject pointObject = value.toObject();
        if (!pointObject.contains(QStringLiteral("required_merit_q48"))) {
            if (pointObject.contains(QStringLiteral("difficulty"))) {
                return Rejected(QStringLiteral(
                    "points[%1] legacy difficulty-only target rejected").arg(index));
            }
            return Rejected(QStringLiteral("points[%1].required_merit_q48 missing").arg(index));
        }
        if (!HasExactKeys(pointObject, pointKeys)) {
            return Rejected(QStringLiteral("points[%1] schema keys invalid").arg(index));
        }

        FrontierTargetCurvePoint point;
        if (!ParseNonNegativeJsonInteger(pointObject.value(QStringLiteral("height")),
                                         point.height)) {
            return Rejected(PointFieldError(index, QStringLiteral("height")));
        }
        if (index > 0 && point.height != previousHeight + 1) {
            return Rejected(QStringLiteral("points heights are not contiguous at index %1")
                                .arg(index));
        }
        previousHeight = point.height;

        if (!pointObject.value(QStringLiteral("block_hash")).isString()) {
            return Rejected(PointFieldError(index, QStringLiteral("block_hash")));
        }
        point.blockHash = pointObject.value(QStringLiteral("block_hash")).toString();
        if (!blockHashPattern.match(point.blockHash).hasMatch()) {
            return Rejected(PointFieldError(index, QStringLiteral("block_hash")));
        }
        point.blockHash = point.blockHash.toLower();

        if (!ParseNonNegativeJsonInteger(pointObject.value(QStringLiteral("block_time")),
                                         point.blockTime)) {
            return Rejected(PointFieldError(index, QStringLiteral("block_time")));
        }
        if (!pointObject.value(QStringLiteral("work_type")).isString() ||
            pointObject.value(QStringLiteral("work_type")).toString() !=
                QLatin1String(FRONTIER_TARGET_CURVE_WORK_TYPE)) {
            return Rejected(PointFieldError(index, QStringLiteral("work_type")));
        }
        if (!ParseNonNegativeJsonInteger(pointObject.value(QStringLiteral("confirmations")),
                                         point.confirmations)) {
            return Rejected(PointFieldError(index, QStringLiteral("confirmations")));
        }
        if (!ParseCanonicalPositiveU64(
                pointObject.value(QStringLiteral("required_merit_q48")),
                point.requiredMeritQ48)) {
            return Rejected(PointFieldError(index, QStringLiteral("required_merit_q48")));
        }
        point.targetMerit = static_cast<double>(point.requiredMeritQ48) /
            static_cast<double>(FRONTIER_TARGET_CURVE_Q48);
        snapshot.points.push_back(std::move(point));
    }

    FrontierTargetCurveLoadResult result;
    result.accepted = true;
    result.snapshot = std::move(snapshot);
    return result;
}

FrontierTargetCurveLoadResult FrontierLoadTargetCurveSnapshot(
    const QString& path,
    const QDateTime& nowUtc)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return Rejected(QStringLiteral("cannot open target curve snapshot: %1")
                            .arg(file.errorString()));
    }
    return FrontierParseTargetCurveSnapshot(file.readAll(), nowUtc);
}

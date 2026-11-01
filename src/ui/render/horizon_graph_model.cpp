#include "horizon_graph_model.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr qint64 kMinuteMs = 60LL * 1000LL;
constexpr qint64 kHourMs = 60LL * kMinuteMs;
constexpr qint64 kDayMs = 24LL * kHourMs;
constexpr qint64 kWeekMs = 7LL * kDayMs;
constexpr qint64 kMonthMs = 30LL * kDayMs;

QString lower(QStringView value)
{
    return value.toString().trimmed().toLower();
}

std::optional<qint64> timestampFromObject(const QJsonObject& object)
{
    static const QStringList keys{
        QStringLiteral("observed_utc"), QStringLiteral("observedUtc"),
        QStringLiteral("timestamp_utc"), QStringLiteral("timestampUtc"),
        QStringLiteral("timestamp")};
    for (const QString& key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isString()) {
            const QDateTime date = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
            if (date.isValid()) return date.toUTC().toMSecsSinceEpoch();
        } else if (value.isDouble() && std::isfinite(value.toDouble())) {
            const double number = value.toDouble();
            if (number >= 0.0 && number <= static_cast<double>(std::numeric_limits<qint64>::max())) {
                return static_cast<qint64>(number);
            }
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t> positiveInteger(const QJsonValue& value)
{
    if (value.isString()) {
        bool ok = false;
        const qulonglong parsed = value.toString().trimmed().toULongLong(&ok);
        if (ok && parsed > 0) return static_cast<std::uint64_t>(parsed);
    }
    if (value.isDouble() && std::isfinite(value.toDouble()) &&
        value.toDouble() > 0.0 &&
        value.toDouble() <= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        const double parsed = value.toDouble();
        if (std::floor(parsed) == parsed) return static_cast<std::uint64_t>(parsed);
    }
    return std::nullopt;
}

double numberOrZero(const QJsonValue& value)
{
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().trimmed().toDouble(&ok);
        return ok && std::isfinite(parsed) ? parsed : 0.0;
    }
    return value.isDouble() && std::isfinite(value.toDouble()) ? value.toDouble() : 0.0;
}

QString firstString(const QJsonObject& object, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QJsonValue value = object.value(QLatin1String(key));
        if (value.isString() && !value.toString().trimmed().isEmpty()) {
            return value.toString().trimmed();
        }
    }
    return {};
}

double eventCoordinate(const FrontierGraphEvent& event, FrontierGraphXAxis axis)
{
    return axis == FrontierGraphXAxis::Time
        ? static_cast<double>(event.timestampMs)
        : static_cast<double>(event.length);
}

class LodAccumulator final {
public:
    explicit LodAccumulator(const FrontierGraphQuery& query)
        : m_query(query)
    {
        m_maxPoints = std::clamp<std::size_t>(query.maxPoints, 1, 65536);
        m_buckets.resize(static_cast<qsizetype>(m_maxPoints));
        m_used.resize(static_cast<qsizetype>(m_maxPoints), false);
    }

    bool add(const FrontierGraphEvent& event)
    {
        const double value = eventCoordinate(event, m_query.axis);
        if (!std::isfinite(value) || value <= 0.0) return false;
        if (m_haveExplicitBounds) {
            if (value < m_min || value > m_max) return false;
        } else {
            if (m_query.axis == FrontierGraphXAxis::Time) {
                if (m_query.minTimeMs > 0 && event.timestampMs < m_query.minTimeMs) return false;
                if (m_query.maxTimeMs > m_query.minTimeMs && event.timestampMs > m_query.maxTimeMs) return false;
            } else {
                if (m_query.minLength > 0 && event.length < m_query.minLength) return false;
                if (m_query.maxLength > m_query.minLength && event.length > m_query.maxLength) return false;
            }
        }
        const FrontierGraphStatus status = FrontierClassifyGraphStatus(
            event.status, event.networkOutcome);
        const std::size_t bucket = bucketFor(value);
        FrontierGraphLodPoint& point = m_buckets[static_cast<qsizetype>(bucket)];
        if (!m_used[static_cast<qsizetype>(bucket)]) {
            point.representative = event;
            m_used[static_cast<qsizetype>(bucket)] = true;
        } else if (eventCoordinate(event, m_query.axis) >=
                   eventCoordinate(point.representative, m_query.axis)) {
            point.representative = event;
        }
        ++point.members;
        switch (status) {
        case FrontierGraphStatus::Accepted: ++point.accepted; break;
        case FrontierGraphStatus::Rejected: ++point.rejected; break;
        case FrontierGraphStatus::Stale: ++point.stale; break;
        case FrontierGraphStatus::Pending: ++point.pending; break;
        }
        ++m_recordsMatched;
        return true;
    }

    void setBounds(double minimum, double maximum)
    {
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum) return;
        m_min = minimum;
        m_max = maximum;
        m_haveExplicitBounds = true;
    }

    QVector<FrontierGraphLodPoint> finish() const
    {
        QVector<FrontierGraphLodPoint> result;
        result.reserve(static_cast<qsizetype>(std::min(m_maxPoints, m_recordsMatched)));
        for (const bool used : m_used) {
            Q_UNUSED(used);
        }
        for (qsizetype index = 0; index < m_buckets.size(); ++index) {
            if (m_used.at(index)) result.append(m_buckets.at(index));
        }
        return result;
    }

    [[nodiscard]] std::uint64_t recordsMatched() const noexcept { return m_recordsMatched; }

private:
    std::size_t bucketFor(double value) const
    {
        if (m_haveExplicitBounds && m_max > m_min) {
            const double fraction = std::clamp((value - m_min) / (m_max - m_min), 0.0, 1.0);
            return std::min(m_maxPoints - 1,
                            static_cast<std::size_t>(fraction * m_maxPoints));
        }
        // A query with no upper bound cannot know its final scale up front.
        // A deterministic hash-like folding keeps it bounded; normal graph
        // queries provide explicit bounds and use the ordered path above.
        const auto integer = static_cast<std::uint64_t>(value);
        return static_cast<std::size_t>(integer % m_maxPoints);
    }

    FrontierGraphQuery m_query;
    std::size_t m_maxPoints{1};
    QVector<FrontierGraphLodPoint> m_buckets;
    QVector<bool> m_used;
    double m_min{0.0};
    double m_max{0.0};
    bool m_haveExplicitBounds{false};
    std::uint64_t m_recordsMatched{0};
};

void inferBounds(const QVector<FrontierGraphEvent>& events,
                 FrontierGraphXAxis axis,
                 double* minimum,
                 double* maximum)
{
    *minimum = std::numeric_limits<double>::max();
    *maximum = std::numeric_limits<double>::lowest();
    for (const FrontierGraphEvent& event : events) {
        const double value = eventCoordinate(event, axis);
        if (!std::isfinite(value) || value <= 0.0) continue;
        *minimum = std::min(*minimum, value);
        *maximum = std::max(*maximum, value);
    }
    if (*minimum == std::numeric_limits<double>::max() || *maximum <= *minimum) {
        *minimum = 0.0;
        *maximum = 1.0;
    }
}

void configureBounds(const FrontierGraphQuery& query, LodAccumulator* accumulator)
{
    if (query.axis == FrontierGraphXAxis::Time && query.maxTimeMs > query.minTimeMs) {
        accumulator->setBounds(query.minTimeMs, query.maxTimeMs);
    } else if (query.axis == FrontierGraphXAxis::Length && query.maxLength > query.minLength) {
        accumulator->setBounds(static_cast<double>(query.minLength),
                               static_cast<double>(query.maxLength));
    }
}

bool inQuery(const FrontierGraphEvent& event, const FrontierGraphQuery& query)
{
    if (query.axis == FrontierGraphXAxis::Time) {
        if (query.minTimeMs > 0 && event.timestampMs < query.minTimeMs) return false;
        if (query.maxTimeMs > query.minTimeMs && event.timestampMs > query.maxTimeMs) return false;
    } else {
        if (query.minLength > 0 && event.length < query.minLength) return false;
        if (query.maxLength > query.minLength && event.length > query.maxLength) return false;
    }
    return true;
}

} // namespace

QString FrontierGraphRangeLabel(FrontierGraphRange range)
{
    for (const FrontierGraphRangePreset& preset : FrontierGraphRangePresets()) {
        if (preset.range == range) return QString::fromLatin1(preset.label);
    }
    return QStringLiteral("All");
}

std::optional<qint64> FrontierGraphRangeDurationMs(FrontierGraphRange range)
{
    for (const FrontierGraphRangePreset& preset : FrontierGraphRangePresets()) {
        if (preset.range == range) return preset.durationMs;
    }
    return std::nullopt;
}

FrontierGraphStatus FrontierClassifyGraphStatus(QStringView status,
                                                QStringView networkOutcome)
{
    const QString outcome = lower(networkOutcome);
    const QString proof = lower(status);
    // A combined terminal journal result is intentionally counted once in the
    // rejected bucket.  The red renderer/tooltip still preserves the exact
    // "rejected_or_stale" text, while the aggregate remains a partition.
    if (outcome == QLatin1String("rejected_or_stale") ||
        proof == QLatin1String("rejected_or_stale")) {
        return FrontierGraphStatus::Rejected;
    }
    if (outcome.contains(QStringLiteral("stale"))) return FrontierGraphStatus::Stale;
    if (outcome.contains(QStringLiteral("reject"))) return FrontierGraphStatus::Rejected;
    if (proof.contains(QStringLiteral("stale"))) return FrontierGraphStatus::Stale;
    if (proof.contains(QStringLiteral("reject"))) return FrontierGraphStatus::Rejected;
    // These are protocol/proof states, not prose. Substring matching turns
    // "unverified", "not_exact", and "unaccepted" into false positives.
    const bool acceptedOutcome = outcome == QLatin1String("accepted") ||
        outcome == QLatin1String("accepted_pending_canonical") ||
        outcome == QLatin1String("confirmed");
    const bool exactProof = proof == QLatin1String("accepted") ||
        proof == QLatin1String("exact") || proof == QLatin1String("exact_pass") ||
        proof == QLatin1String("exact_gap") || proof == QLatin1String("exact_prime_gap") ||
        proof == QLatin1String("certified") || proof == QLatin1String("host_validated") ||
        proof == QLatin1String("verified");
    if (acceptedOutcome || exactProof) {
        return FrontierGraphStatus::Accepted;
    }
    return FrontierGraphStatus::Pending;
}

FrontierGraphAggregate FrontierAggregateGraphEvents(
    const QVector<FrontierGraphEvent>& events,
    const QSet<QString>& activeRunIds,
    bool currentOnly,
    qint64 sessionStartedUtcMs)
{
    FrontierGraphAggregate aggregate;
    for (const FrontierGraphEvent& event : events) {
        if (currentOnly &&
            (event.timestampMs < sessionStartedUtcMs ||
             !activeRunIds.contains(event.runId))) continue;
        switch (FrontierClassifyGraphStatus(event.status, event.networkOutcome)) {
        case FrontierGraphStatus::Accepted: ++aggregate.accepted; break;
        case FrontierGraphStatus::Rejected: ++aggregate.rejected; break;
        case FrontierGraphStatus::Stale: ++aggregate.stale; break;
        case FrontierGraphStatus::Pending: ++aggregate.pending; break;
        }
    }
    return aggregate;
}

FrontierGraphStatus FrontierGraphLodPoint::dominantStatus() const noexcept
{
    if (rejected > 0 && rejected >= stale && rejected >= accepted && rejected >= pending) {
        return FrontierGraphStatus::Rejected;
    }
    if (stale > 0 && stale >= accepted && stale >= pending) return FrontierGraphStatus::Stale;
    if (accepted >= pending) return FrontierGraphStatus::Accepted;
    return FrontierGraphStatus::Pending;
}

QVector<FrontierGraphLodPoint> FrontierGraphLodQuery(
    const QVector<FrontierGraphEvent>& events,
    const FrontierGraphQuery& query)
{
    if (events.isEmpty() || query.maxPoints == 0) return {};
    double minimum = 0.0;
    double maximum = 0.0;
    if (query.axis == FrontierGraphXAxis::Time && query.maxTimeMs > query.minTimeMs) {
        minimum = query.minTimeMs;
        maximum = query.maxTimeMs;
    } else if (query.axis == FrontierGraphXAxis::Length && query.maxLength > query.minLength) {
        minimum = static_cast<double>(query.minLength);
        maximum = static_cast<double>(query.maxLength);
    } else {
        inferBounds(events, query.axis, &minimum, &maximum);
    }
    LodAccumulator accumulator(query);
    accumulator.setBounds(minimum, maximum);
    for (const FrontierGraphEvent& event : events) {
        if (inQuery(event, query)) accumulator.add(event);
    }
    return accumulator.finish();
}

std::optional<FrontierGraphEvent> FrontierGraphEventFromJson(const QJsonObject& object)
{
    const std::optional<qint64> timestamp = timestampFromObject(object);
    if (!timestamp) return std::nullopt;
    const QJsonValue lengthValue = object.contains(QStringLiteral("exact_gap"))
        ? object.value(QStringLiteral("exact_gap"))
        : (object.contains(QStringLiteral("gap")) ? object.value(QStringLiteral("gap"))
                                                    : object.value(QStringLiteral("length")));
    const auto length = positiveInteger(lengthValue);
    if (!length) return std::nullopt;
    const QString status = firstString(object, {"proof_state", "status", "state"});
    const QString outcome = firstString(object, {"network_outcome", "networkOutcome", "outcome"});
    FrontierGraphEvent event;
    event.timestampMs = *timestamp;
    event.length = *length;
    event.merit = numberOrZero(object.value(QStringLiteral("merit_decimal")));
    if (event.merit <= 0.0) event.merit = numberOrZero(object.value(QStringLiteral("merit")));
    event.sourceId = firstString(object, {"source_id", "sourceId", "device_id", "gpu_uuid"});
    event.sourceKind = firstString(object, {"source_kind", "sourceKind", "kind"});
    event.runId = firstString(object, {"run_id", "runId", "source_run_id"});
    event.candidateId = firstString(object, {"candidate_id", "candidateId", "history_id"});
    event.status = status;
    event.networkOutcome = outcome;
    return event;
}

FrontierGraphDataQuery::~FrontierGraphDataQuery()
{
    m_indexFile.close();
}

bool FrontierGraphDataQuery::beginIndex(const QString& path, QString* error)
{
    m_indexFile.close();
    m_sparse.clear();
    m_indexedLines = 0;
    m_indexedBytes = 0;
    m_indexComplete = false;
    m_path = QDir::toNativeSeparators(path);
    m_indexFile.setFileName(m_path);
    if (!m_indexFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("graph history cannot be opened: %1").arg(m_path);
        return false;
    }
    return true;
}

bool FrontierGraphDataQuery::indexStep(std::uint64_t maxLines)
{
    if (m_indexComplete) return true;
    if (!m_indexFile.isOpen() &&
        !m_indexFile.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    maxLines = std::clamp<std::uint64_t>(maxLines, 1, 65536);
    for (std::uint64_t line = 0; line < maxLines && !m_indexFile.atEnd(); ++line) {
        const qint64 offset = m_indexFile.pos();
        const QByteArray bytes = m_indexFile.readLine();
        if (bytes.isEmpty()) break;
        QJsonParseError parse;
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
        if (parse.error == QJsonParseError::NoError && document.isObject()) {
            const auto event = FrontierGraphEventFromJson(document.object());
            if (m_indexedLines % m_sparseStride == 0) {
                m_sparse.append({static_cast<std::uint64_t>(std::max<qint64>(0, offset)),
                                 event ? event->timestampMs : 0});
            }
        } else if (m_indexedLines % m_sparseStride == 0) {
            m_sparse.append({static_cast<std::uint64_t>(std::max<qint64>(0, offset)), 0});
        }
        ++m_indexedLines;
        m_indexedBytes = static_cast<std::uint64_t>(std::max<qint64>(0, m_indexFile.pos()));
    }
    if (m_indexFile.atEnd()) {
        m_indexComplete = true;
        m_indexFile.close();
    }
    return true;
}

FrontierGraphDiskQueryResult FrontierGraphDataQuery::query(const FrontierGraphQuery& query) const
{
    FrontierGraphDiskQueryResult result;
    if (m_path.isEmpty() || query.maxPoints == 0) return result;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return result;

    qint64 startOffset = 0;
    if (query.axis == FrontierGraphXAxis::Time && query.minTimeMs > 0) {
        for (const SparseEntry& entry : m_sparse) {
            if (entry.timestampMs <= query.minTimeMs) startOffset = static_cast<qint64>(entry.byteOffset);
            else break;
        }
    }
    file.seek(std::max<qint64>(0, startOffset));
    LodAccumulator accumulator(query);
    if (query.axis == FrontierGraphXAxis::Time && query.maxTimeMs > query.minTimeMs) {
        accumulator.setBounds(query.minTimeMs, query.maxTimeMs);
    } else if (query.axis == FrontierGraphXAxis::Length && query.maxLength > query.minLength) {
        accumulator.setBounds(static_cast<double>(query.minLength),
                              static_cast<double>(query.maxLength));
    }
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        ++result.linesRead;
        QJsonParseError parse;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject()) continue;
        const auto event = FrontierGraphEventFromJson(document.object());
        if (!event || !inQuery(*event, query)) continue;
        if (accumulator.add(*event)) ++result.recordsMatched;
    }
    result.points = accumulator.finish();
    result.complete = true;
    return result;
}

QString FrontierGraphNetworkOutcomeId(const QString& runId,
                                      const QString& candidateId,
                                      const QString& outcome,
                                      const QDateTime& observedUtc)
{
    const QString material = QStringLiteral("%1|%2|%3|%4")
        .arg(runId, candidateId.toLower(), outcome.toLower(),
             observedUtc.toUTC().toString(Qt::ISODateWithMs));
    return QString::fromLatin1(
        QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QJsonObject FrontierGraphNetworkOutcomeObject(const FrontierGraphNetworkOutcome& value)
{
    return {
        {QStringLiteral("schema"), QStringLiteral("gapcoin-frontier-gap-network-outcome-v1")},
        {QStringLiteral("outcome_id"), value.outcomeId},
        {QStringLiteral("run_id"), value.runId},
        {QStringLiteral("candidate_id"), value.candidateId.toLower()},
        {QStringLiteral("outcome"), value.outcome.toLower()},
        {QStringLiteral("observed_utc"), value.observedUtc.toUTC().toString(Qt::ISODateWithMs)},
    };
}

std::optional<FrontierGraphNetworkOutcome> FrontierParseGraphNetworkOutcome(
    const QJsonObject& object)
{
    const QString schema = object.value(QStringLiteral("schema")).toString();
    if (schema != QLatin1String("gapcoin-frontier-gap-network-outcome-v1") &&
        schema != QLatin1String("gapminer-r4-gap-network-outcome-v1")) return std::nullopt;
    FrontierGraphNetworkOutcome value;
    value.outcomeId = object.value(QStringLiteral("outcome_id")).toString().toLower();
    value.runId = object.value(QStringLiteral("run_id")).toString().trimmed();
    value.candidateId = object.value(QStringLiteral("candidate_id")).toString().trimmed().toLower();
    value.outcome = object.value(QStringLiteral("outcome")).toString().trimmed().toLower();
    value.observedUtc = QDateTime::fromString(
        object.value(QStringLiteral("observed_utc")).toString(), Qt::ISODateWithMs).toUTC();
    static const QRegularExpression sha256(QStringLiteral("^[0-9a-f]{64}$"));
    const bool allowed = value.outcome == QLatin1String("accepted") ||
        value.outcome == QLatin1String("accepted_pending_canonical") ||
        value.outcome == QLatin1String("rejected") ||
        value.outcome == QLatin1String("stale") ||
        value.outcome == QLatin1String("rejected_or_stale");
    if (!sha256.match(value.outcomeId).hasMatch() || value.runId.isEmpty() ||
        value.candidateId.isEmpty() || value.candidateId.size() > 512 ||
        !value.observedUtc.isValid() || !allowed ||
        FrontierGraphNetworkOutcomeId(value.runId, value.candidateId, value.outcome,
                                      value.observedUtc) != value.outcomeId) return std::nullopt;
    return value;
}

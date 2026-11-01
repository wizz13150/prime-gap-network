#pragma once

#include <QDateTime>
#include <QFile>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QVector>

#include "graph_view.h"
#include "horizon_graph_ranges.h"

#include <cstddef>
#include <cstdint>
#include <optional>

// The observatory has two deliberately independent graph coordinates.  Time
// is useful for operational/trading-style windows; length is the scientific
// coordinate and never falls back to an ordinal or an arrival timestamp.
QString FrontierGraphRangeLabel(FrontierGraphRange range);
std::optional<qint64> FrontierGraphRangeDurationMs(FrontierGraphRange range);

enum class FrontierGraphStatus { Accepted, Rejected, Stale, Pending };

// Status is intentionally derived from the durable network outcome first and
// only then from the proof/status text.  This keeps a rejected or stale
// network result red even when a later replay says that its mathematical proof
// is exact.
FrontierGraphStatus FrontierClassifyGraphStatus(QStringView status,
                                                QStringView networkOutcome = {});

struct FrontierGraphEvent {
    qint64 timestampMs{0};
    std::uint64_t length{0};
    double merit{0.0};
    QString sourceId;
    QString sourceKind;
    QString runId;
    QString candidateId;
    QString status;
    QString networkOutcome;
};

struct FrontierGraphAggregate {
    std::uint64_t accepted{0};
    std::uint64_t rejected{0};
    std::uint64_t stale{0};
    std::uint64_t pending{0};

    [[nodiscard]] std::uint64_t total() const noexcept
    {
        return accepted + rejected + stale + pending;
    }

    [[nodiscard]] std::uint64_t terminalTotal() const noexcept
    {
        return accepted + rejected + stale;
    }

    [[nodiscard]] bool countAgrees() const noexcept
    {
        return total() == accepted + rejected + stale + pending;
    }

    [[nodiscard]] bool terminalCountAgrees(std::uint64_t expected) const noexcept
    {
        return terminalTotal() == expected;
    }
};

FrontierGraphAggregate FrontierAggregateGraphEvents(
    const QVector<FrontierGraphEvent>& events,
    const QSet<QString>& activeRunIds = {},
    bool currentOnly = false,
    qint64 sessionStartedUtcMs = 0);

struct FrontierGraphQuery {
    FrontierGraphXAxis axis{FrontierGraphXAxis::Time};
    qint64 minTimeMs{0};
    qint64 maxTimeMs{0};
    std::uint64_t minLength{0};
    std::uint64_t maxLength{0};
    std::size_t maxPoints{2048};
};

struct FrontierGraphLodPoint {
    FrontierGraphEvent representative;
    std::uint64_t members{0};
    std::uint64_t accepted{0};
    std::uint64_t rejected{0};
    std::uint64_t stale{0};
    std::uint64_t pending{0};

    [[nodiscard]] FrontierGraphStatus dominantStatus() const noexcept;
};

// Bounded LOD projection.  It preserves counts in the aggregate tier while
// returning at most maxPoints draw records.  A million-event archive therefore
// remains queryable without allocating a million paint primitives.
QVector<FrontierGraphLodPoint> FrontierGraphLodQuery(
    const QVector<FrontierGraphEvent>& events,
    const FrontierGraphQuery& query);

// Parse the compact event shape used by candidate/receipt JSONL.  Unknown
// fields are ignored; malformed or non-positive lengths are rejected.
std::optional<FrontierGraphEvent> FrontierGraphEventFromJson(const QJsonObject& object);

struct FrontierGraphDiskQueryResult {
    QVector<FrontierGraphLodPoint> points;
    std::uint64_t linesRead{0};
    std::uint64_t recordsMatched{0};
    bool complete{false};
};

// Streaming JSONL index/query path.  beginIndex()/indexStep() retain only a
// sparse byte/time index; query() scans the selected range and emits bounded
// LOD buckets.  No full history payload is retained by this object.
class FrontierGraphDataQuery final {
public:
    FrontierGraphDataQuery() = default;
    ~FrontierGraphDataQuery();

    FrontierGraphDataQuery(const FrontierGraphDataQuery&) = delete;
    FrontierGraphDataQuery& operator=(const FrontierGraphDataQuery&) = delete;

    bool beginIndex(const QString& path, QString* error = nullptr);
    bool indexStep(std::uint64_t maxLines = 4096);
    [[nodiscard]] bool indexComplete() const noexcept { return m_indexComplete; }
    [[nodiscard]] std::uint64_t indexedLines() const noexcept { return m_indexedLines; }
    [[nodiscard]] std::uint64_t indexedBytes() const noexcept { return m_indexedBytes; }
    [[nodiscard]] std::size_t sparseIndexSize() const noexcept { return m_sparse.size(); }
    [[nodiscard]] QString path() const { return m_path; }

    FrontierGraphDiskQueryResult query(const FrontierGraphQuery& query) const;

private:
    struct SparseEntry {
        std::uint64_t byteOffset{0};
        qint64 timestampMs{0};
    };

    QString m_path;
    QFile m_indexFile;
    QVector<SparseEntry> m_sparse;
    std::uint64_t m_indexedLines{0};
    std::uint64_t m_indexedBytes{0};
    std::uint64_t m_sparseStride{4096};
    bool m_indexComplete{false};
};

struct FrontierGraphNetworkOutcome {
    QString outcomeId;
    QString runId;
    QString candidateId;
    QString outcome;
    QDateTime observedUtc;
};

QString FrontierGraphNetworkOutcomeId(const QString& runId,
                                      const QString& candidateId,
                                      const QString& outcome,
                                      const QDateTime& observedUtc);
QJsonObject FrontierGraphNetworkOutcomeObject(const FrontierGraphNetworkOutcome& value);
std::optional<FrontierGraphNetworkOutcome> FrontierParseGraphNetworkOutcome(
    const QJsonObject& object);

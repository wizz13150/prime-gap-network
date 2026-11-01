#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <optional>

namespace pgn::mining {
enum class SourceCheckpointDialect { LegacyHigh10, ScoutGapBound };
struct SourceStopCheckpointEvidence {
 QByteArray checkpointBytes,sourceTail;
 QJsonObject manifest,lastExit;
 std::optional<bool> owned; // absent is NOT stopped
 QString expectedRun,checkpointLabel; // exact caller-declared label, no path lookup
 SourceCheckpointDialect dialect{SourceCheckpointDialect::LegacyHigh10};
 std::optional<quint32> expectedGapMinimum; // Explicit admitted Scout job, never inferred from bytes.
};
struct SourceCheckpointResume {
 QString run,start,end,length,positions,nextStart,remaining;
 QString tests,gaps,batches,counterTimeNs,checkpointLabel,checkpointSha256;
 SourceCheckpointDialect dialect{SourceCheckpointDialect::LegacyHigh10};
 std::optional<quint32> gapMinimum;
};
enum class SourceStopCheckpointStatus { Unavailable, Ready };
struct SourceStopCheckpointResult {
 SourceStopCheckpointStatus status{SourceStopCheckpointStatus::Unavailable};
 QString error;
 std::optional<SourceCheckpointResume> resume;
};
// Pure byte/declaration consistency for explicit legacy or Scout source layouts.
// Ready is NOT physical ownership, checkpoint fsync/power-cut durability, source
// authentication, storage confirmation, scientific proof or launch permission.
// The64 bytes contain no run ID: the caller must acquire/bind evidence freshly.
// sourceTail is bounded UTF8 (<=65536), not a complete journal; a truncated
// leading unrelated row is allowed, never a fragmented final counter/footer.
SourceStopCheckpointResult validateSourceStopCheckpoint(const SourceStopCheckpointEvidence&);
}

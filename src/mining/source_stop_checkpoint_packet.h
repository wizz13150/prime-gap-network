#pragma once
#include "mining/source_stop_checkpoint.h"
#include "mining/prime_gap_engine.h"

namespace pgn::mining {
struct SourceStopCheckpointPacket {
    std::optional<SourceStopCheckpointEvidence> evidence;
    QString error;
};
// Decodes the source-only PS capture envelope. No file/network/process actions.
// Expected identity/label come from the caller's selected producer profile,
// never from this packet itself. Fresh acquisition/close-barrier ownership and
// actual SQLite commit are still the application controller's obligations.
SourceStopCheckpointPacket decodeSourceStopCheckpointPacket(const QJsonObject& snapshot,
    const QString& expectedRun,const QString& expectedDevice,const QString& expectedLabel,
    const std::optional<PrimeGapEngineJob>& selectedEngine = {});
}

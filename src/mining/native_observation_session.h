#pragma once
#include "mining/source_session.h"
#include "mining/ssh_snapshot_transport.h"
#include "mining/completion_advance_gate.h"
#include "mining/native_source_identity.h"

namespace pgn::mining {
enum class NativeObservationState { Cold, Source, ReplayRequired };
struct NativeObservation {
    NativeObservationState state{NativeObservationState::Cold};
    QString run;
    qint64 nextOffset{},committedOffset{};
    QList<QByteArray> displayLines;
    std::optional<AdvanceTicket> advance;
};
// Single-threaded admission seam, not a scheduler or a control client. Frames
// come from decodeSshSnapshot; the caller explicitly selects the run/device.
// Manifest consistency is checked, NOT cryptographically authenticated here.
// Only the source/index boundary can issue a completion-gate ticket; that
// ticket still confers no process-control, booking or network authority.
class NativeObservationSession final {
public:
    NativeObservationSession(SourceHistory&,SourceMirror&,QString expectedDevice);
    NativeObservation consume(const SshObservationFrame&,const QString& selectedRun,
        qint64 monotonicNowMs,bool enabled,bool operatorPaused,const std::function<bool()>& cancelled={});
    // Snapshot/read path ONLY. Does not consume its chunk, bind an identity,
    // recover a mirror, claim a completion ticket or mutate session state.
    SourceConservation conservation(const SshObservationFrame&,const QString& selectedRun) const;
    AdvanceSettlement settle(const AdvanceTicket& ticket,AdvanceOutcome outcome,qint64 now){return gate_.settle(ticket,outcome,now);}
private:
    SourceHistory& history_;
    SourceMirror& mirror_;
    SourceSession source_;
    CompletionAdvanceGate gate_;
    QString device_,run_;
    QJsonObject identity_;
};
}

#pragma once
#include "mining/source_stop_checkpoint.h"
#include <QVector>

namespace pgn::mining {
struct MiningStopTarget {QString device,run,start,end;};
struct MiningStopRequest {QString id;MiningStopTarget target;};
enum class MiningShutdownPhase { Idle, Waiting, Held, Ready };
// In-process application-close policy, not a second job store or service.
// Caller first freezes succession, reconciles prepared transitions and obtains
// fresh verified physical inventory. No process is signalled by this object.
class MiningShutdownGate final {
public:
    QVector<MiningStopRequest> begin(const QVector<MiningStopTarget>&,bool inventoryVerified,bool transitionsReconciled);
    void reset(); // Explicit new inventory/retry only; never an automatic loop.
    bool acknowledgeStop(const QString& id,bool applied,const QString& detail={});
    bool observeCheckpoint(const QString& id,const SourceStopCheckpointEvidence&);
    // Caller must actually commit this EXACT validated tuple to the existing
    // application storage; an ACK/exit/snapshot alone is not a storage receipt.
    bool storageCommitted(const QString& id,const SourceCheckpointResume& committedTuple,bool committed);
    std::optional<SourceCheckpointResume> checkpoint(const QString& id)const;
    MiningShutdownPhase phase()const;
    QString error()const;
private:
    struct Entry {MiningStopRequest request;std::optional<SourceCheckpointResume> proof;bool saved{},conflict{};QString error;};
    Entry* find(const QString&);const Entry* find(const QString&)const;
    QVector<Entry> entries_;bool begun_{};QString failure_;
};
}

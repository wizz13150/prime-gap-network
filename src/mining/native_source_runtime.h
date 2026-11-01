#pragma once
#include "mining/ssh_snapshot_transport.h"
#include <QObject>
#include <QJsonObject>
#include <functional>
#include <memory>

namespace pgn::mining {
struct NativeSourceRuntimeConfig {
    SshObservationEndpoint endpoint;
    QString device,historyDatabase,mirrorDirectory; // explicit existing parent/directory
    int startupTimeoutMs{5000},idleTimeoutMs{15000},snapshotTimeoutMs{30000};
};
enum class NativeSourceAttemptStatus {Complete,Failed,TimedOut,Cancelled,ReplayRequired};
struct NativeSourcePublication {
    QString requestId,run;
    quint64 epoch{};
    QJsonObject snapshot,nativeIndex,conservation; // nativeIndex empty: dedicated async callback below
    qint64 coalescedFrames{}; // rich publications ONLY; no raw/SQLite loss
    std::optional<qint64> resourceReceiptNs; // LOCAL pre-admission frame receipt
};
struct NativeSourceAttempt {
    QString requestId,run,error;
    quint64 epoch{};
    NativeSourceAttemptStatus status{NativeSourceAttemptStatus::Failed};
    std::optional<qint64> nextOffset,committedOffset; // absent is not zero
    SshObservationResult transport;
};
struct NativeSourceInventory {
    QString requestId,error;
    quint64 epoch{},sourceEpoch{}; // actual acquisition and admitted SOURCE incarnation
    SshObservationResult transport;
    std::optional<SshObservationFrame> frame; // no frame unless clean snapshot EOF0
    QJsonObject conservation; // read-only; unknown if SOURCE not attached
    // This endpoint observes 3090 only. No finite3060 idle/zero is fabricated.
};
struct NativeSourceLineage {
    QString requestId,run;
    quint64 epoch{},sourceEpoch{};
    QJsonObject proof; // logical proof or Held; NEVER physical Ready/quit authority
};
enum class NativeSourceProgressStage {FrameReceived,SourceAdmitted,IndexBegin,IndexDone,IndexHeld};
struct NativeSourceProgress {
    NativeSourceProgressStage stage{NativeSourceProgressStage::FrameReceived};
    QString requestId,run,error;
    quint64 sourceEpoch{};
    qint64 offset{},nextOffset{},elapsedMs{}; // exact FIRST-frame offsets; monotonic since its receipt
    std::optional<qint64> pendingBytes; // absent remains unknown, never zero
    QJsonObject json() const; // bounded metadata only: no RAW, snapshot or invented wall clock
};
enum class NativeSourceTraceStage {InitializeBegin,InitializeDone,InitializeHeld,IndexBegin,IndexDone,IndexHeld,InventoryQueued,InventoryEntered,InventoryTransport,InventoryDelivered};
struct NativeSourceTrace {
    NativeSourceTraceStage stage{NativeSourceTraceStage::InventoryQueued};
    QString requestId,run,error;
    quint64 epoch{},sourceEpoch{};
    qint64 monotonicMs{};
    std::optional<SshObservationTrace> transport;
    std::optional<qint64> offset,nextOffset; // exact FIRST source frame; absent during initialization
    QJsonObject json() const;
};
struct NativeSourceRuntimeCallbacks {
    std::function<void(const NativeSourcePublication&)> publication;
    std::function<void(const NativeSourceAttempt&)> sourceFinished;
    std::function<void(const NativeSourceInventory&)> inventory;
    std::function<void()> stopped;
    std::function<void(const NativeSourceLineage&)> lineage;
    // Optional diagnostics: FIRST SOURCE frame receipt/admission plus FIRST
    // actual asynchronous graph audit, when nativeIndex rendering is requested.
    // Coalesced graph request may name a later frame; offsets stay original to
    // each stage, and elapsed remains since the first SOURCE receipt.
    // Independent queued creating-thread delivery, not the coalesced rich
    // publication mailbox and never an application-writer operation. Consumers
    // must not treat it as ownership, admission, index completeness or control.
    std::function<void(const NativeSourceProgress&)> progress;
    // Optional bounded lifecycle, independently queued to the creating thread,
    // never the rich mailbox/application writer. At most four SOURCE storage
    // events (initialization + FIRST frame index) and nine inventory events per
    // attempt; no default logging, RAW, new read or authority/freshness claim.
    std::function<void(const NativeSourceTrace&)> trace;
    // Dedicated asynchronous display snapshot, all+current schema unchanged.
    // Historical coherent snapshot, NEVER latest/RAW conservation/Ready proof.
    // complete=false/error is explicit; absent graph data is never zero.
    std::function<void(const QJsonObject&)> nativeIndex;
};
// ONE in-process storage worker owns SourceHistory/Mirror/ObservationSession
// and its Stream SOURCE client. A separate in-process actor owns the ONE
// readonly Snapshot inventory transport, so storage cannot starve its IO/timers.
// Immutable acquisition returns to the storage worker for readonly conservation
// with pinned sourceEpoch/cancellation, then is rechecked on owner delivery. A
// display audit never runs there: one readonly graph actor has one active snapshot
// and one latest render request. Its past snapshots never confer SOURCE Ready.
// Stop interrupts its SQLite VM and waits for all three actors to release.
// No Node, observer executable, daemon, control/start/stop-miner or automatic retry.
// API/callbacks creating thread only. Consumer commit is synchronous on worker;
// OS pipe provides source backpressure. UI mailbox has at most one event and
// latest publication; all raw/scientific records remain indexed in SQLite.
enum class NativeSourceReadKind { LiveStream, CompletedSnapshot };
class NativeSourceRuntime final:public QObject {
public:
    static constexpr qsizetype PublicationLimit=524288;
    NativeSourceRuntime(NativeSourceRuntimeConfig,NativeSourceRuntimeCallbacks,QObject* parent=nullptr);
    ~NativeSourceRuntime() override;
    // Optional immutable actual manifest is a pre-consume identity filter,
    // NOT an admission/ownership proof. A mismatching frame cannot enroll data.
    // CompletedSnapshot is one ordinary finite snapshot/EOF, not a standing
    // live stream, a retry, a coverage certificate or a larger stream deadline.
    bool startSource(const QString& requestId,const QString& selectedRun,const QJsonObject& expectedManifest={},
                     NativeSourceReadKind kind=NativeSourceReadKind::LiveStream);
    bool inspectInventory(const QString& requestId,const QString& expectedRun={});
    // Explicit close/resume boundary ONLY, never per UI poll. Same worker,
    // real held-Win32 RAII + existing readonly verifier. SOURCE pipe backpressures
    // during work; no fresh inventory/source restart may enter until callback.
    // False means not enqueued/no callback, including when no SOURCE attempt
    // has ever been admitted (source epoch0). It is never a positive proof.
    bool verifyLineage(const QString& requestId,const QString& bookingDatabase,const QJsonObject& finalSnapshot);
    // Cancel ONLY the currently admitted lineage verification, via its atomic
    // epoch token at the verifier's existing checkpoints. False with none;
    // never poisons a later verification or touches SOURCE/client/sourceEpoch.
    // The same correlated lineage callback still settles with ready=false and
    // error="SOURCE lineage cancelled"; no positive proof survives the cancel.
    bool cancelLineage();
    // Stops only the owned SOURCE SSH client, retaining source/session state.
    bool cancelSource();
    // Final async cleanup of all owned threads, including interruptible display
    // reader; no restart after stop. Wait for stopped() before
    // application quit. Destructor requests cleanup but never kills a worker.
    // Initial SQLite index audit may delay cleanup: no hard stop-latency claim.
    void stop();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}

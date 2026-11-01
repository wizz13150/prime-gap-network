#pragma once
#include "booking/source_registry.h"
#include "booking/initial_start_admission.h"
#include "booking/next_boundary_admission.h"
#include "mining/next_work_policy.h"
#include "mining/source_stop_checkpoint.h"
#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <memory>
#include <optional>

namespace pgn::booking {
struct Booking {QString id,start,end,state,kind,device,createdUtc;};
struct MiningSessionMetadata {
    pgn::mining::MiningSessionMode mode{pgn::mining::MiningSessionMode::Unselected};
    bool automaticAfterManual{};
};
enum class ArmQueuedStatus { Held,Armed };
struct ArmQueuedResult {
    ArmQueuedStatus status{ArmQueuedStatus::Held};
    QString error;
    std::optional<InitialStartDispatch> dispatch; // informational ARMED only, never launch permission
    bool replayed{};
};
struct StoredSourceResume {
    pgn::mining::SourceCheckpointResume resume;
    QByteArray checkpointBytes;
};
struct CheckpointResumeEpoch {
    QString requestUUID,device,bookingId,previousRun,run,observationUUID;
    std::optional<QString> predecessor; // unchanged completed-job relation, NOT previousRun
    // Immutable original tuple/bytes, including original counterTimeNs. These
    // are cumulative producer baselines, NOT newly observed deltas or proof
    // that the new process actually loaded these bytes or conserved old logs.
    StoredSourceResume checkpoint;
    QString rawConservationRequirement{"LINEAGE_RAW_CONSERVATION_REQUIRED"};
    QJsonObject recoveryEvidence; // explicit persisted-prefix recovery, never a fake clean parent exit
};
enum class CheckpointResumeReadStatus { Unavailable,Absent,Present };
struct CheckpointResumeEpochRead {
    CheckpointResumeReadStatus status{CheckpointResumeReadStatus::Unavailable};
    QString error;
    std::optional<CheckpointResumeEpoch> epoch;
    QByteArray receiptUtf8; // exact canonical transport bytes, no re-encoder needed
};
enum class CheckpointResumeAdmissionStatus { Held,Prepared,ReconcilePrepared };
struct CheckpointResumeAdmission {
    CheckpointResumeAdmissionStatus status{CheckpointResumeAdmissionStatus::Held};
    QString error;
    std::optional<CheckpointResumeEpoch> epoch;
    QByteArray receiptUtf8;
    bool replayed{};
};
enum class SourceRunManifestReadStatus { Unavailable,Absent,Present };
struct SourceRunManifestRead {
    SourceRunManifestReadStatus status{SourceRunManifestReadStatus::Unavailable};
    QString error;
    QJsonObject manifest;
    QString blockStartedUtc; // earliest actual birth in the verified same-booking resume ancestry
    QByteArray receiptUtf8;
};
struct InitialStartObservedBase {QString run,birth,start,end;};
enum class InitialStartAdmissionReadStatus { Unavailable,Absent,Present };
struct InitialStartAdmissionRead {
    InitialStartAdmissionReadStatus status{InitialStartAdmissionReadStatus::Unavailable};
    QString error,requestUUID,observationUUID;
    InitialStartResult admission;
    QByteArray receiptUtf8;
    std::optional<InitialStartObservedBase> base;
};
struct ExactBlockSelection {QString start,end,zoneStart,zoneEnd;};
struct BookingEvent {qint64 sequence{};QString bookingId,start,end,device,kind,fromState,toState,eventKind,observedUtc;};
struct BookingEventPage {QVector<BookingEvent> rows;qint64 through{},total{};bool more{};QString error;};
// Separate read-only connection for bounded, asynchronous lifecycle inspection.
// through pins the append-only sequence; before uses descending keyset pages.
BookingEventPage readBookingEvents(const QString& file,qint64 before=0,qint64 through=0,
                                  const QString& bookingId={},int limit=100);
// Local durable intent, NOT a community lease. No RPC/hash/miner subscription.
// Only lifecycle transitions are written; raw progress is display-only memory.
class BookingStore {
public:
    explicit BookingStore(const QString& file);
    ~BookingStore();
    bool valid() const;
    QString error() const;
    // Read-only on THIS store connection, one consistent statement. nullopt
    // means unavailable/unknown schema, types or states, never an empty queue.
    // false is only the absence of PREPARED dispatches, not physical OFF or
    // permission to close; uncertain remote commands still require handling.
    std::optional<bool> hasPreparedDispatches() const;
    // One read snapshot of existing metadata, no default-row insertion. Missing
    // keys mean OFF/Unselected; invalid values/device/DB mean unavailable.
    std::optional<MiningSessionMetadata> miningSessionMetadata(const QString& device) const;
    std::optional<bool> automaticAfterManual(const QString& device) const;
    // Explicit user/controller transitions only. Never call from a poll or
    // constructor to infer Auto from an empty queue. Setters preserve each other.
    QString setAutomaticAfterManual(const QString& device,bool enabled);
    QString setMiningSessionMode(const QString& device,pgn::mining::MiningSessionMode mode);
    // Caller MUST first validate fresh stop evidence through RootGate. This
    // stores exact byte/tuple consistency, NOT run ownership/authentication,
    // remote fsync or permission to resume. No active-run pointer is inferred.
    // Same GPU/run forbids range/label rebind and any counter regression;
    // exact replay is idempotent without UPDATE. Other metadata/jobs unchanged.
    QString saveResumeCheckpoint(const QString& device,const pgn::mining::SourceCheckpointResume& resume,
                                 const QByteArray& checkpointBytes);
    std::optional<StoredSourceResume> savedResumeCheckpoint(const QString& device,const QString& run) const;
    // Caller pays fresh STOP/checkpoint/physical identity admission. This only
    // reserves a new execution epoch in SAME SQLite, not a launch or new work.
    // Old RUNTIME must be PAUSED; its bounds/checkpoint/dates remain unchanged.
    // Dispatch's run is the sole active pointer; predecessor is NOT rewritten.
    // Same committed request returns reconciliation without UPDATE; never use
    // it as an unconditional second launch token.
    CheckpointResumeAdmission prepareCheckpointResume(const QString& requestUUID,const QString& device,
        const QString& bookingId,const QString& previousRun,const QString& newRun,
        const QString& expectedCheckpointSha,const QString& observationUUID,
        const QJsonObject& recoveryEvidence = {});
    // Absent differs from corrupt/unavailable. Reads only the immutable local
    // epoch receipt, not physical ownership or current-launch authorization.
    CheckpointResumeEpochRead savedCheckpointResumeEpoch(const QString& device,const QString& newRun) const;
    // Caller pays fresh remote ownership/STOP and known-producer validation.
    // Preserve the ACTUAL process manifest, not a range-derived template or a
    // newly stamped observation. Immutable canonical16KiB SAME-SQLite record;
    // exact replay is read-only, including archived/retired execution epochs.
    // A missing legacy origin cannot be adopted after its run was retired.
    QString saveSourceRunManifest(const QString& device,const QJsonObject& actualManifest);
    SourceRunManifestRead savedSourceRunManifest(const QString& device,const QString& run) const;
    // Explicit INITIAL_START only, no worker/SSH call. Same SQLite atomic
    // mode+booking+PREPARED+command receipt. Replay/pending returns reconciliation,
    // NEVER a new launch token. Interrupted is ResumeRequired, not a new run.
    InitialStartResult prepareInitialStart(const QString& requestUUID,const QString& device,
        const InitialStartScope&,const SourceRegistry&,const InitialBoundaryEvidence&);
    // READ ONLY consistency of a committed INITIAL_START/PREPARED receipt in
    // SAME SQLite, not another prepare/selection, launch token or retry grant.
    // Prepared/replayed=false in the returned admission describes DB state for
    // the pure command encoder; it NEVER means "not already sent". The caller
    // must enforce at-most-once submission and reconcile an uncertain outcome.
    // v1 receipt does NOT persist base. observedBase (including explicit null)
    // MUST come from caller-owned fresh request/observation correlation, never
    // a frame alone. Present checks its stored SOURCE identity if supplied;
    // it cannot authenticate freshness, link observation->base in SQLite, or
    // prove remote absence from null. No writes/default rows/history retiming.
    InitialStartAdmissionRead savedInitialStartAdmission(const QString& requestUUID,const QString& device,
        const QString& observationUUID,const std::optional<InitialStartObservedBase>& observedBase) const;
    // Current prepared-resume state + immutable epoch from ONE read snapshot.
    // Unlike savedCheckpointResumeEpoch, also checks LOCAL/dispatch/old PAUSED
    // physical tuple, saved checkpoint, actual manifest/base and ancestry.
    // Prepared/replayed=false is consistency only, NEVER never-sent/retry or
    // physical freshness/load/RAW-conservation proof. Caller correlates base,
    // consumes at-most-once identity and reconciles every uncertain outcome.
    CheckpointResumeAdmission savedPreparedCheckpointResumeAdmission(const QString& requestUUID,const QString& device,
        const QString& newRun,const QString& observationUUID,const InitialStartObservedBase& observedBase) const;
    // Continuation ONLY after caller-paid fresh SOURCE/conservation/lineage
    // and recordQueuedSourceCompletion. Session provenance is independent of
    // automatic selection. Same pending tuple means reconciliation, not retry.
    NextBoundaryAdmission prepareNextAtBoundary(const QString& device,const NextBoundarySource& predecessor,
        const InitialStartScope&,const SourceRegistry&,const NextBoundaryEvidence&);
    // One read snapshot of current PREPARED/NULL-run + exact SOURCE predecessor.
    // Present is local consistency, never a fresh observation/never-sent token.
    PreparedNextAdmissionRead savedPreparedNextAdmission(const QString& device,const QString& bookingId,
        const NextBoundarySource& predecessor) const;
    // Explicit UI arm of an EXISTING LOCAL reservation, never a new booking,
    // a selected-next override, PREPARED claim or launch. Caller uses its ONE
    // writer/lifecycle latch and pays current producer/profile and fresh scope
    // + registry acquisition. qualifiedScope.blockWidth is that caller-paid
    // finite capacity, NOT current binding.length or remote authentication.
    // Numeric/zone ordering remains prepareNextAtBoundary's responsibility.
    ArmQueuedResult armQueuedIntent(const QString& bookingId,const QString& device,
        const InitialStartScope& qualifiedScope,const SourceRegistry& currentRegistry);
    // Caller pays current producer/physical observation. ACK is NOT an input.
    // Typed owned RUNNING/STOPPING only; first OFF/partial remains explicitly
    // Held. Runtime, actual manifest and LOCAL/dispatch commit atomically.
    ObservedDispatchReconcileResult reconcileObservedDispatch(const QJsonObject& actualSnapshot);
    // Historical actual recovery epoch only: OFF/exit0, explicit immutable proof.
    // Caller validates the complete fresh stopped capture; this never launches.
    ObservedDispatchReconcileResult reconcileRecoveredDispatch(const QJsonObject& actualSnapshot,
        const pgn::mining::SourceCheckpointResume& actualStoppedSeal,const QByteArray& exactCheckpoint);
    QVector<Booking> active() const;
    QVector<Booking> history(int limit=64) const;
    // Exact union of every finished physical range, independent of history
    // pagination. Adjacent ranges on one device collapse without losing bounds.
    QVector<Booking> completedRanges() const;
    QString completedPositions() const;
    QString queue(const SourceRegistry&,const QString& start,const QString& end,const QString& device);
    int availableBlocks(const SourceRegistry&,const QString& start,const QString& end,
                        const QString& zoneStart,const QString& zoneEnd) const;
    QString queueAvailableBlocks(const SourceRegistry&,const QString& start,const QString& end,
                                 const QString& zoneStart,const QString& zoneEnd,const QString& device);
    int availableSelection(const SourceRegistry&,const QVector<ExactBlockSelection>&,QString* reason=nullptr) const;
    QString queueSelection(const SourceRegistry&,const QVector<ExactBlockSelection>&,const QString& device);
    QString releaseSelection(const QVector<ExactBlockSelection>&);
    QString release(const QString& id);
    QString releaseRange(const QString& start,const QString& end);
    QString reorderQueuedZones(const QVector<ExactBlockSelection>& zones,const QStringList& expectedQueuedIds);
    QString resetQueueOrder();
    QString observeRuntime(const QString& run,const QString& start,const QString& end,const QString& device,bool running);
    QString recordSourceCompletion(const QString& run,const QString& start,const QString& end,const QString& device);
    // Caller MUST validate fresh SOURCE completion + conservation + the exact
    // manifest queued_intent first. This transaction checks local consistency,
    // not physical ownership/authentication. No intent is inferred or created.
    // PREPARED/run NULL may recover a lost launch receipt; STARTED/run NULL
    // refuses. Exact completed replay performs no UPDATE, including audit rows.
    QString recordQueuedSourceCompletion(const QString& run,const QString& start,const QString& end,
                                         const QString& device,const std::optional<QString>& queuedIntent);
private:
    ObservedDispatchReconcileResult reconcileObservedDispatchInternal(const QJsonObject&,
        const pgn::mining::SourceCheckpointResume*,const QByteArray&);
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}

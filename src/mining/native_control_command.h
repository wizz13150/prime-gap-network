#pragma once
#include "booking/booking_store.h"
#include "mining/ssh_snapshot_transport.h"
#include "mining/native_finite_device.h"
#include "mining/prime_gap_engine.h"
#include <stdexcept>

namespace pgn::mining {
enum class NativeControlAction { Initial, Resume, AdvanceQueued, StopObservedIdentity,
    Finite3060Snapshot, Finite3060Start, FiniteObservedRunStop, Configured3060StopLatch,
    Configured3090Profile, Configured3090StopLatch };
enum class NativeControlErrorCode { InvalidContract, UnsupportedAbi };
class NativeControlError final : public std::runtime_error {
public:
    NativeControlError(NativeControlErrorCode code,const char* message):std::runtime_error(message),code_(code){}
    NativeControlErrorCode code() const noexcept{return code_;}
private: NativeControlErrorCode code_;
};
struct NativeObservedBase {QString run,birth,start,end;};
struct NativeQueuedAdvance {
    QString device,bookingId,start,end,previousRun,previousStart,previousEnd;
    std::optional<QString> previousBookingId;
};
struct NativeStopIdentity {QString device,run,pid,birth,imageSha256;};
enum class NativeControlOutcome { Unconfirmed, RequestReported, AlreadyRunningReported, Held, ObservationReported,
    ConfiguredProfileReported, ConfiguredOffLatchReported };
enum class Configured3090OffReason { ApplicationClose, ExplicitCardOff };
struct NativeQualifiedSourceCapacity {
    QString policy,sdk,scopeStart,scopeEnd,maximumLength,guard,qualificationSha256;
    QString sourceManifestSha256;
    std::optional<PrimeGapEngineJob> engineJob;
    bool nativePlanRequired{};
};
// Acquired configured-profile evidence, never an ownership/OFF sample. The
// WHOLE binding DTO can be compared with a separately acquired inventory;
// bindingSha256 is the digest of original bytes, not a Qt JSON re-encoding.
struct NativeConfiguredProfileObservation {
    QString protocol,device,executable,imageSha256,bindingSha256;
    QJsonObject binding;
    std::optional<NativeQualifiedSourceCapacity> sourceCapacity;
};
struct NativeControlResult {
    NativeControlOutcome outcome{NativeControlOutcome::Unconfirmed};
    QString status,error,requestId,run;
    QJsonObject packet;
    std::optional<NativeFiniteObservation> finiteObservation;
    std::optional<NativeConfiguredProfileObservation> configuredProfile;
    // None of the wire statuses is physical ownership, stopped/checkpoint
    // durability, SOURCE conservation, transaction validity or permission.
    bool requiresFreshObservation() const noexcept{return true;}
};

// PURE command/response seam. No QProcess, retries, store mutation or launch.
// The caller must supply current SAME-SQLite admission/readback and serialize
// it with its application lifecycle. Public DTOs/JSON are NOT authenticated.
// Productive native-control.ps1 and SEPARATE fixed bounded3060 controller ABI.
// NOT generic devices, worker/recover or automatic allocation. Finite responses
// are observations without an action-request echo, never RequestReported ACKs.
class NativeControlCommand final {
public:
    static NativeControlCommand initial(const SshObservationEndpoint&,const booking::InitialStartResult&,
        const QByteArray& nativeReceiptUtf8,const std::optional<NativeObservedBase>&);
    static NativeControlCommand resume(const SshObservationEndpoint&,const booking::CheckpointResumeAdmission&,
        const booking::CheckpointResumeEpochRead&,const NativeObservedBase&);
    static NativeControlCommand advanceQueued(const SshObservationEndpoint&,const NativeQueuedAdvance&);
    // Explicit capability from the caller's actual observed snapshot. This is
    // compatibility metadata, NOT remote attestation. Absent/unknown fails
    // UnsupportedAbi. Old controllers reject the distinct action/parameters;
    // NEVER fall back to legacy stop or retarget an observed epoch.
    static NativeControlCommand stopObservedIdentity(const SshObservationEndpoint&,const NativeStopIdentity&,
                                                     const QString& observedProtocol);
    // Extract ONLY the exact declared current process and unique executable
    // pin from an acquired typed snapshot. Caller still pays fresh acquisition;
    // remote observed-stop rechecks actual process/binding/image under its gate.
    static NativeControlCommand stopObservedSnapshot(const SshObservationEndpoint&,const SshObservationFrame&);
    static NativeControlCommand finite3060Snapshot(const SshObservationEndpoint&);
    static NativeControlCommand finite3060Start(const SshObservationEndpoint&,const QString& requestUUID);
    // Exact expected run CAS in the existing remote controller, which then
    // rechecks its own current PID/birth/path/image pin. No PID-CAS claim here.
    static NativeControlCommand finiteObservedRunStop(const SshObservationEndpoint&,const QString& requestUUID,
                                                     const QString& expectedRun);
    // Separate explicit OFF latch for THIS configured card/profile only. It
    // may cancel a pending launch with null manifest; not observed-run stop,
    // process authentication, physical completion or permission to quit.
    static NativeControlCommand configured3060StopLatch(const SshObservationEndpoint&,const QString& requestUUID,
                                                       Configured3060OffReason);
    static NativeControlCommand configured3090Profile(const SshObservationEndpoint&);
    // Distinct capability, no fallback from failed observed-identity CAS.
    // Only sets desired-OFF. Does not signal a PID or prove physical exit.
    static NativeControlCommand configured3090StopLatch(const SshObservationEndpoint&,const QString& requestUUID,
        Configured3090OffReason,const NativeConfiguredProfileObservation&);
    NativeControlAction action() const noexcept{return action_;}
    const SshObservationCommand& ssh() const noexcept{return ssh_;}
    NativeControlResult decode(const QByteArray& stdoutBytes,bool normalExit,int exitCode,
                              const QByteArray& stderrBytes={}) const noexcept;
    static constexpr qsizetype ResponseLimit=2097152,StderrLimit=8192;
private:
    explicit NativeControlCommand(NativeControlAction action):action_(action){}
    void finish(const SshObservationEndpoint&,const QString& typedScript);
    NativeControlAction action_;SshObservationCommand ssh_;
    QString request_,run_,device_,booking_,start_,end_,previousStart_,previousEnd_;
    NativeStopIdentity stop_;
    std::optional<QString> previousBooking_;
    std::optional<NativeConfiguredProfileObservation> configured_;
    QString configuredReason_;
};
}

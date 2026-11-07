#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>

namespace pgn::mining {
enum class SshObservationAction { Snapshot, Stream, Hardware };
struct SshObservationEndpoint {
    QString executable,keyFile,host,user;
    quint16 port{}; // Explicit DIRECT endpoint; ssh_config aliases are disabled.
    QString runtimeRoot,gpuLeaseRoot,hostKeyAlias; // explicit optional transport2 scope, never inferred from wire packets
};
struct SshObservationRequest {
    SshObservationAction action{SshObservationAction::Snapshot};
    QString expectedRun; // Empty only for initial offset0; otherwise32lowerhex.
    qint64 offset{};
    int startupTimeoutMs{5000},idleTimeoutMs{15000},snapshotTimeoutMs{30000};
    // Process-identity STOP preflight only. Do not wait for a driver metrics
    // query to stop an exactly identified process; false remains the default.
    bool noDriver{};
    // Finite READ lease only; completed-source catch-up may request fewer
    // frames. Never controls GPU lifetime or weakens the transport deadlines.
    int streamFrames{150};
};
struct SshObservationCommand { QString executable;QStringList arguments; };
struct SshObservationFrame {
    QJsonObject packet; // Opaque manifest/provenance: NOT authenticated by transport.
    qint64 offset{},nextOffset{};
    QByteArray chunk;
    std::optional<qint64> pendingBytes; // absent is UNKNOWN, never implicit0
};
enum class SshObservationStatus { Complete, Failed, TimedOut, Cancelled };
struct SshObservationResult {
    SshObservationStatus status{SshObservationStatus::Failed};
    QString error;QByteArray standardError;std::optional<int> exitCode;
    qint64 completeFrames{}; // Prefix only unless status==Complete.
};
bool isSshHostStartupResourceFailure(const SshObservationResult&);

enum class SshObservationTraceStage {RequestStarted,ProcessStarted,FirstStdout,ProcessFinished,StartupDeadline,IdleDeadline,SnapshotDeadline,Settled};
struct SshObservationTrace {
    SshObservationTraceStage stage{SshObservationTraceStage::RequestStarted};
    SshObservationAction action{SshObservationAction::Snapshot};
    QString expectedRun;
    qint64 offset{},monotonicMs{},elapsedMs{};
    std::optional<qint64> processId,stdoutBytes;
    std::optional<int> exitCode;
    std::optional<SshObservationStatus> status;
    QJsonObject json() const;
};
SshObservationCommand makeSshObservationCommand(const SshObservationEndpoint&,const SshObservationRequest&);
// Explicit, validated direct endpoint; callers own and quote the remote command.
SshObservationCommand makeSshConnectionCommand(const SshObservationEndpoint&,const QString& remoteCommand);
QString nativeRemoteRuntimeRoot(const SshObservationEndpoint&);
QString nativeControlScriptForEndpoint(const SshObservationEndpoint&,const QString& generatedScript);
SshObservationFrame decodeSshSnapshot(const QByteArray& document);
// Creating thread only, ONE attempt per instance, no reconnect/control actions.
// Caller consumes frames promptly; transport never accumulates source history.
class SshSnapshotTransport final : public QObject {
public:
    static constexpr qsizetype FrameLimit=262144,ChunkLimit=65536,StderrLimit=8192;
    // A disconnected SSH client must never leave an immortal remote reader.
    // This limits only a read lease, never mining, history or durable cursors.
    static constexpr int StreamFrameLease=150;
    using FrameConsumer=std::function<bool(const SshObservationFrame&)>;
    using Completion=std::function<void(const SshObservationResult&)>;
    // Optional fixed lifecycle only (at most six events/attempt), never per
    // stream frame. Timestamps are LOCAL event receipt, not remote/OS arrival.
    // No command, endpoint, key, RAW or stderr is exposed. Sink exceptions are
    // ignored; diagnostics never confer observation/control authority.
    using Trace=std::function<void(const SshObservationTrace&)>;
    explicit SshSnapshotTransport(QObject* parent=nullptr);
    ~SshSnapshotTransport() override;
    void start(const SshObservationEndpoint&,const SshObservationRequest&,FrameConsumer,Completion,Trace={});
    void cancel();
private:
    class Impl;std::unique_ptr<Impl> impl_;
};
}

#pragma once
#include "mining/native_control_command.h"
#include <QObject>
#include <functional>
#include <memory>
#include <optional>

namespace pgn::mining {
struct NativeControlTimeouts {int startupMs{},deadlineMs{};}; // explicit; zero is invalid
enum class NativeControlTransportStatus { CompleteResponse, Failed, TimedOut, Cancelled };
struct NativeControlTransportResult {
    NativeControlTransportStatus status{NativeControlTransportStatus::Failed};
    NativeControlResult control; // Unconfirmed on every local failure
    QString error;
    QByteArray standardError; // bounded prefix if stderrOverflow==true
    bool stderrOverflow{};
    std::optional<int> localExitCode;
    // A locally vanished SSH client does NOT cancel/undo a remote command.
    // Even a complete request/latch response needs independent observation.
    bool remoteOutcomeUnknown() const noexcept{return true;}
    bool requiresFreshObservation() const noexcept{return true;}
    bool physicalApplied() const noexcept{return false;}
};

// Integrated QtCore utility, NOT a service or a submission/retry coordinator.
// Creating thread only, including destruction. ONE submission per instance.
// Completion is queued once after local cleanup, and is suppressed if owner
// is destroyed first. Deleting this transport inside completion is supported.
class NativeControlTransport final : public QObject {
public:
    using Completion=std::function<void(const NativeControlTransportResult&)>;
    static constexpr int MaximumStartupMs=30000,MaximumDeadlineMs=120000;
    explicit NativeControlTransport(QObject* parent=nullptr);
    ~NativeControlTransport() override;
    void start(const NativeControlCommand&,NativeControlTimeouts,Completion);
    void cancel();
private:
    class Impl;std::unique_ptr<Impl> impl_;
};
}

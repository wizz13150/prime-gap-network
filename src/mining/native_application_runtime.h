#pragma once
#include "mining/native_runtime_configuration.h"
#include "mining/native_control_transport.h"
#include <QObject>
#include <functional>
#include <memory>
namespace pgn::mining {
class NativeBridge;
struct NativeApplicationRuntimeConfig {
    NativeRuntimeConfiguration transport;
    QString sourceRegistryDirectory;
    NativeControlTimeouts controlTimeouts{5000,30000};
    int maximumObservationAgeMs{30000};
    bool timingAudit{false}; // no default logging/SOURCE diagnostic installation
};
// ONE in-process composition of the already qualified source, inventory,
// command, queue and module components. No Node, daemon, second allocator,
// default binding, arbitrary START or automatic command retry.
// Current source-capacity acquisition seam remains closed: initial observation
// and SOURCE composition are testable, but arm/next mutations are Held until
// the actual configured-profile qualification is joined, not merely declared.
class NativeApplicationRuntime final:public QObject {
public:
    NativeApplicationRuntime(NativeApplicationRuntimeConfig,NativeBridge*,QObject* parent=nullptr);
    ~NativeApplicationRuntime() override;
    // Local attachment followed by actual read-only discovery. True is NOT
    // connected, owned, started, physically idle or permission to close.
    bool start();
    bool submit(const QJsonObject&);
    // Observation cleanup ONLY, after caller's physical close gate. Await
    // onStopped before quitting; this never writes STOP or kills a GPU.
    void stop();
    bool stopped()const;
    QString managedSourceDevice()const; // explicit creating-runtime scope, no physical/ownership claim
    std::function<void()> onStopped;
    std::function<void(const QString&)> onStatus;
    std::function<void(const QJsonObject&)> onPacket; // exact instrumentation
    // UI-thread, outside ONE writer. Opt-in bounded metadata only: at most63
    // lifecycle events + explicit timing_limit event64 per runtime instance.
    // Sink exceptions cannot alter observation, admission, age or shutdown.
    std::function<void(const QJsonObject&)> onTiming;
private:
    class Impl;
    std::unique_ptr<Impl> d_;
};
}

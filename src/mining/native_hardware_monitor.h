#pragma once
#include "mining/ssh_snapshot_transport.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace pgn::mining {
class NativeBridge;
// Read-only vendor telemetry. This object has no source reader, booking writer,
// allocator, process-control command or authority to start/stop a GPU worker.
class NativeHardwareMonitor final:public QObject {
public:
    NativeHardwareMonitor(SshObservationEndpoint,NativeBridge*,QObject* parent=nullptr);
    ~NativeHardwareMonitor() override;
    bool start();
    void stop();
    std::function<void(bool,const QString&)> onAvailability;
private:
    void poll();
    SshObservationEndpoint endpoint_;
    QPointer<NativeBridge> bridge_;
    QPointer<SshSnapshotTransport> transport_;
    QTimer timer_;
    bool started_{},stopping_{};
};
}

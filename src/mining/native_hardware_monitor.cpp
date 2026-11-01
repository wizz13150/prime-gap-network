#include "mining/native_hardware_monitor.h"
#include "mining/native_bridge.h"
#include "mining/source_age.h"

namespace pgn::mining {
NativeHardwareMonitor::NativeHardwareMonitor(SshObservationEndpoint endpoint,NativeBridge* bridge,QObject* parent)
    :QObject(parent),endpoint_(std::move(endpoint)),bridge_(bridge),timer_(this){
    timer_.setInterval(5000);
    connect(&timer_,&QTimer::timeout,this,[this]{poll();});
}
NativeHardwareMonitor::~NativeHardwareMonitor(){stop();}
bool NativeHardwareMonitor::start(){
    if(started_||stopping_||!bridge_)return false;
    // Hardware packets use the bridge's creating-lifetime guard. Every control
    // request is refused; attaching observations is not mining readiness.
    if(!bridge_->attachNativeObservation(this))return false;
    started_=true;timer_.start();poll();return true;
}
void NativeHardwareMonitor::poll(){
    if(stopping_||transport_||!bridge_)return;
    auto* transport=new SshSnapshotTransport(this);transport_=transport;
    const QPointer<NativeHardwareMonitor> guard(this);
    SshObservationRequest request;request.action=SshObservationAction::Hardware;request.snapshotTimeoutMs=15000;
    transport->start(endpoint_,request,[guard](const SshObservationFrame& frame){
        if(!guard||guard->stopping_||!guard->bridge_)return false;
        const auto& packet=frame.packet;const bool valid=packet.value("driver_valid")==true;
        if(valid){
            const auto received=receiptMonotonicNs();
            guard->bridge_->acceptNativeRuntimePacket(guard,{{"type","resource_snapshot"},{"driver_valid",true},
                {"observed_utc",packet.value("observed_utc")},{"gpus",packet.value("gpus")}},received);
        }
        if(guard&&!guard->stopping_&&guard->onAvailability)
            guard->onAvailability(valid,valid?QString{}:QStringLiteral("Vendor observations unavailable"));
        return guard&&!guard->stopping_;
    },[guard,transport](const SshObservationResult& result){
        transport->deleteLater();if(!guard||guard->stopping_)return;
        guard->transport_.clear();
        if(result.status!=SshObservationStatus::Complete&&guard->onAvailability)
            guard->onAvailability(false,result.error.left(400));
    });
}
void NativeHardwareMonitor::stop(){
    if(stopping_)return;stopping_=true;timer_.stop();
    // Destroy the owned client while callbacks' captured state is still alive.
    // It can cancel only this finite SSH/vendor read, never a compute owner.
    auto client=transport_;transport_.clear();
    if(client){client->cancel();delete client;}
}
}

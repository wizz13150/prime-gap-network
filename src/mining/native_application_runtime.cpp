#include "mining/native_application_runtime.h"
#include "mining/native_scientific_context.h"
#include "mining/raw_counter_coalescing.h"
#include "mining/native_application_control.h"
#include "mining/native_application_inventory.h"
#include "mining/native_queued_boundary_control.h"
#include "mining/engine_module_registry.h"
#include "mining/native_bridge.h"
#include "mining/source_decimal.h"
#include "mining/source_age.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pgn::mining {
namespace {
qint64 monotonicMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
QString transportStatus(NativeControlTransportStatus status){switch(status){case NativeControlTransportStatus::CompleteResponse:return "complete_response";case NativeControlTransportStatus::Failed:return "failed";case NativeControlTransportStatus::TimedOut:return "timed_out";case NativeControlTransportStatus::Cancelled:return "cancelled";}return "unknown";}
const QString Constant="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
QString nonce(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
bool runId(const QString& value){static const QRegularExpression p("^[0-9a-f]{32}$");return value.size()==32&&p.match(value).hasMatch();}
bool uuid(const QString& value){const auto id=QUuid::fromString(value);return !id.isNull()&&id.toString(QUuid::WithoutBraces)==value;}
void need(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
booking::InitialStartScope scopeOf(const QJsonObject& snapshot){
    const auto binding=snapshot.value("binding").toObject(),scope=binding.value("test_scope").toObject();
    const auto a=scope.value("start"),z=scope.value("end");
    need(binding.value("uuid")==Constant&&(scope.value("enabled").isUndefined()||scope.value("enabled")==true)&&scope.value("order")=="ascending"&&a.isString()&&z.isString()&&
         a.toString().size()<=160&&z.toString().size()<=160&&source_decimal::canonical(a.toString())&&source_decimal::canonical(z.toString())&&source_decimal::less(a.toString(),z.toString()),
         "Actual qualified ascending execution scope unavailable; no fallback range");
    // This is the current qualified A73 Zone1000 ABI, NOT universal capacity
    // inferred from free VRAM, card name, or the last fragment's length.
    return {a.toString(),z.toString(),a.toString(),z.toString(),a.toString(),"1000000000000000"};
}
}
class NativeApplicationRuntime::Impl {
public:
    NativeApplicationRuntime* owner;
    NativeApplicationRuntimeConfig config;
    QPointer<NativeBridge> bridge;
    // Declaration order guarantees all components die before their SAME
    // store and ONE writer; worker shutdown is awaited by inventory lifetime.
    NativeApplicationWriter writer;
    booking::BookingStore store;
    EngineModuleRegistry modules;
    std::unique_ptr<NativeApplicationControl> control;
    std::unique_ptr<NativeQueuedBoundaryControl> queue;
    std::unique_ptr<NativeApplicationInventory> inventory;
    QPointer<NativeControlTransport> finite;
    QPointer<NativeControlTransport> profileClient;
    QPointer<SshSnapshotTransport> hardware;
    QTimer hardwareTimer;
    bool hardwareBusy{};
    std::optional<qint64> lastHardwareReceiptNs;
    std::optional<NativeConfiguredProfileObservation> acquiredProfile;
    QString profileError;
    bool profileBusy{};
    QTimer finiteTimer;
    QTimer sourceReconnectTimer;
    unsigned sourceReconnectFailures{};
    bool started{},stopping{},stopped{},sourceStopped{},inventoryBusy{},sourceActive{},sourcePublicationQueued{},finiteBusy{},finiteRequested{},pendingInitialFinite{};
    QString pendingStartRequest,pendingStartInventory;
    std::optional<bool> explicitDesired;
    // Closed until actual configured-profile SDK/pins/capacity acquisition is
    // joined to the exact inventory binding. Device name/test_scope alone is
    // NOT capacity evidence. No public setter can manufacture this capability.
    bool constantCapacityQualified{};
    booking::SourceRegistry acquiredRegistry;
    QString attachedRun,pendingSourceRun,attachedSourceRequest;
    NativeSourceReadKind pendingSourceKind{NativeSourceReadKind::LiveStream};
    QJsonObject attachedManifest,pendingSourceManifest;
    std::optional<NativeSourceAttempt> lastSourceAttempt;
    QSet<QString> sourceBoundaryReads;
    QString armRequest,armBooking;
    QSet<QString> boundaryRuns;
    QString completedBoundaryPending,completedBoundaryRequest,completedBoundaryInFlight;
    bool completedBoundaryQueued{},completedBoundaryNeedsDriver{},coldDiscoveryNeedsDriver{};
    QSet<QString> seenControlRequests;
    QSet<QString> configuredOffRequests;
    std::optional<NativeSourcePublication> latestPublication;
    qint64 coalescedPublications{};
    unsigned timingEvents{};
    Impl(NativeApplicationRuntime* o,NativeApplicationRuntimeConfig c,NativeBridge* b)
        :owner(o),config(std::move(c)),bridge(b),store(config.transport.paths.bookingDatabase),modules(config.transport.paths.moduleRegistryDatabase){
        requireOwner();need(b&&b->thread()==o->thread(),"Unified runtime requires same-thread bridge");
        need(store.valid(),"Same application booking SQLite unavailable");
        need(QFileInfo(config.sourceRegistryDirectory).isAbsolute(),"Explicit source registry directory required");
        const QPointer<NativeApplicationRuntime> guard(owner);
        sourceReconnectTimer.setSingleShot(true);
        QObject::connect(&sourceReconnectTimer,&QTimer::timeout,owner,[guard]{
            if(!guard)return;auto& self=*guard->d_;
            if(self.stopping||self.writer.frozen()||self.sourceActive||!self.pendingSourceRun.isEmpty()||!self.lastSourceAttempt||
               self.lastSourceAttempt->run!=self.attachedRun||self.lastSourceAttempt->requestId!=self.attachedSourceRequest)return;
            // Read-only replay from the SOURCE worker's committed cursor. The
            // immutable manifest/first-frame/offset gates remain mandatory.
            // No inventory success, physical exit, launch or STOP is inferred.
            const auto request=nonce();
            if(self.inventory->source().startSource(request,self.attachedRun,self.attachedManifest,NativeSourceReadKind::LiveStream)){
                self.attachedSourceRequest=request;self.sourceActive=true;
                self.status("SOURCE reconnecting from durable cursor; physical mining state not inferred");
            }
        });
        control=std::make_unique<NativeApplicationControl>(writer,store,config.transport.endpoint,config.controlTimeouts,config.maximumObservationAgeMs);
        queue=std::make_unique<NativeQueuedBoundaryControl>(writer,store,config.transport.endpoint,config.controlTimeouts,config.maximumObservationAgeMs);
        control->onPacket=[guard](const auto& packet){if(guard)guard->d_->controlPacket(packet);};
        queue->onPacket=[guard](const auto& packet){if(guard)guard->d_->queuePacket(packet);};
        NativeApplicationInventoryCallbacks callbacks;
        callbacks.phase=[guard](const auto& packet){if(guard)guard->d_->deliver(packet);};
        callbacks.packet=[guard](const auto& packet){if(guard){auto& self=*guard->d_;self.inventoryBusy=false;
            const bool completedRead=packet.value("request_id")==self.completedBoundaryRequest&&!self.completedBoundaryRequest.isEmpty();
            if(completedRead){
                const auto run=std::exchange(self.completedBoundaryInFlight,{});self.completedBoundaryRequest.clear();
                if(packet.value("accepted")==true)self.boundaryRuns.insert(run);
                else if(packet.value("retry_read_after_driver_recovery")==true&&run==self.attachedRun&&!self.stopping&&!self.writer.frozen()){
                    // Admission of a READ was not successful observation. Keep
                    // intent, but wait for real vendor recovery; no tight retry.
                    self.completedBoundaryPending=run;self.completedBoundaryNeedsDriver=true;
                }
            }
            if(!completedRead&&packet.value("type")=="native_work_boundary_inventory"&&packet.value("accepted")!=true&&
               packet.value("retry_read_after_driver_recovery")==true&&!self.stopping&&!self.writer.frozen())self.coldDiscoveryNeedsDriver=true;
            if(packet.value("request_id")==self.armRequest&&packet.value("accepted")!=true){
                const auto booking=std::exchange(self.armBooking,{});self.armRequest.clear();
                self.emitPacket({{"type","dispatch"},{"booking_id",booking},{"accepted",false},{"error","Actual reservation arm inventory unavailable; intent preserved"}});
            }
            auto output=packet;
            if(packet.value("type")=="application_start_inventory"&&packet.value("accepted")==true&&!self.constantCapacityQualified){
                output["accepted"]=false;output["error"]="Actual source SDK/pins/capacity qualification unavailable; no new prepared work; "+self.profileError.left(240);
            }
            self.emitPacket(output);if(guard){guard->d_->schedulePendingStart();guard->d_->scheduleCompletedBoundary();}}};
        callbacks.startObserved=[guard](const auto& request,const auto& id,const auto& frame){if(guard){auto& self=*guard->d_;self.qualifyCapacity(frame.packet);if(self.constantCapacityQualified)self.control->rememberStartInventory(request,id,frame);
            // An already-running start observation is still the actual source
            // attachment boundary. It must not require another START/poll.
            if(frame.packet.value("owned")==true)self.observeSource(frame);
        }};
        callbacks.closeInitialObserved=[guard](const auto& value){if(guard)guard->d_->control->rememberCloseInitial(value);};
        callbacks.beforeRefresh=[guard](const auto& request,const auto& id,auto done){if(guard)guard->d_->acquireProfile([done=std::move(done)]{done({});},request,id);else done("Runtime owner unavailable");};
        callbacks.publication=[guard](const auto& publication){if(guard)guard->d_->publication(publication);};
        callbacks.nativeIndex=[guard](const QJsonObject& index){
            // Exact graph publication owns no control writer slot. The source
            // reader has already checked its epoch and coherent SQL snapshot;
            // it cannot mark SOURCE conserved, dispatched or Ready to close.
            if(!guard||guard->d_->stopping)return;
            if(index.value("complete")!=true&&!index.value("error").toString().isEmpty()){
                guard->d_->status("Native graph index held: "+index.value("error").toString().left(300));
                if(!guard)return;
            }
            guard->d_->deliver(index);
        };
        callbacks.sourceFinished=[guard](const auto& attempt){if(guard)guard->d_->sourceFinished(attempt);};
        callbacks.sourceBoundaryRead=[guard](const auto& request,const auto& id,const auto& frame){return guard?guard->d_->sourceBoundaryRead(request,id,frame):QString{};};
        callbacks.boundary=[guard](const auto& request,const auto& id,const auto& frame,const auto& conservation){if(guard)guard->d_->boundary(request,id,frame,conservation);};
        callbacks.stopped=[guard]{if(guard){guard->d_->sourceStopped=true;guard->d_->maybeStopped();}};
        if(config.timingAudit)callbacks.timing=[guard](const QJsonObject& source){if(guard)guard->d_->timing(source.value("schema")=="pgn-native-inventory-timing-1"?"inventory":"source_runtime",source.value("stage").toString(),{{"source",source}});};
        NativeApplicationInventoryConfig acquisition;
        acquisition.source={config.transport.endpoint,Constant,config.transport.paths.sourceHistoryDatabase,config.transport.paths.sourceMirrorDirectory};
        acquisition.bookingDatabase=config.transport.paths.bookingDatabase;acquisition.finiteTimeouts=config.controlTimeouts;acquisition.timingAudit=config.timingAudit;acquisition.finiteManaged=!config.transport.sourceOnly;
        inventory=std::make_unique<NativeApplicationInventory>(writer,std::move(acquisition),std::move(callbacks));
        finiteTimer.setParent(o);finiteTimer.setInterval(1000);
        QObject::connect(&finiteTimer,&QTimer::timeout,o,[guard]{if(guard)guard->d_->observeFinite();});
        hardwareTimer.setInterval(5000);
        QObject::connect(&hardwareTimer,&QTimer::timeout,o,[guard]{if(guard)guard->d_->observeHardware();});
    }
    void requireOwner()const{need(QThread::currentThread()==owner->thread(),"Unified runtime requires creating thread");}
    bool timing(const QString& component,const QString& stage,QJsonObject fields={}){
        if(!config.timingAudit||!owner->onTiming||timingEvents>=64)return true;
        ++timingEvents;
        if(timingEvents==64)fields={{"suppressed_at_least",1},{"suppressed_count_complete",false},{"limit",64}};
        fields.insert("schema","pgn-native-application-timing-1");fields.insert("type","native_timing");fields.insert("sequence",int(timingEvents));fields.insert("component",timingEvents==64?"audit":component);fields.insert("stage",timingEvents==64?"timing_limit":stage);
        // This is LOCAL diagnostic receipt time, never source observed_utc.
        fields.insert("received_monotonic_ms",monotonicMs());const QPointer<NativeApplicationRuntime> guard(owner);
        QMetaObject::invokeMethod(owner,[guard,fields=std::move(fields)]{
            if(!guard)return;const auto callback=guard->onTiming;if(callback)try{callback(fields);}catch(...){}
            // No owner/member access after the optional public sink.
        },Qt::QueuedConnection);return true;
    }
    void maybeStopped(){if(stopping&&sourceStopped&&!finiteBusy&&!profileBusy&&!hardwareBusy&&!stopped){stopped=true;const auto cb=owner->onStopped;if(cb)cb();}}
    void observeHardware(){
        if(stopping||hardwareBusy||writer.frozen())return;
        // Vendor-only telemetry owns no control writer/capability. A slow
        // admission or SOURCE read cannot suppress the physical dashboard.
        // SOURCE already carries these driver observations while running.
        // Idle hardware must not disappear when there is no producer interval.
        const auto now=receiptMonotonicNs();
        if(lastHardwareReceiptNs&&*lastHardwareReceiptNs>=0&&now>=*lastHardwareReceiptNs&&now-*lastHardwareReceiptNs<5000000000LL)return;
        hardwareBusy=true;const QPointer<NativeApplicationRuntime> guard(owner);
        auto* transport=new SshSnapshotTransport(owner);hardware=transport;
        SshObservationRequest request;request.action=SshObservationAction::Hardware;request.snapshotTimeoutMs=15000;
        transport->start(config.transport.endpoint,request,[guard](const SshObservationFrame& frame){
            if(!guard||guard->d_->stopping)return false;auto& self=*guard->d_;
            // Dedicated read-only vendor observation. No source/control DTO,
            // log chunk, booking, capability or proof can be emitted here.
            const auto& packet=frame.packet;
            self.deliver({{"type","native_hardware_availability"},
                {"state",packet.value("driver_valid").isBool()?(packet.value("driver_valid").toBool()?"available":"driver_unavailable"):"unknown"},
                {"observed_utc",packet.value("observed_utc")}});
            if(!guard||guard->d_->stopping)return false;
            if(packet.value("driver_valid")==true){
                const auto received=receiptMonotonicNs();self.lastHardwareReceiptNs=received;
                if(self.completedBoundaryNeedsDriver&&!self.completedBoundaryPending.isEmpty()){
                    // This only wakes another fresh full inventory READ. It
                    // grants no Start, ownership, source completion or booking.
                    self.completedBoundaryNeedsDriver=false;self.scheduleCompletedBoundary();
                }
                if(self.coldDiscoveryNeedsDriver){
                    QMetaObject::invokeMethod(self.owner,[guard]{if(!guard)return;auto& state=*guard->d_;
                        if(state.coldDiscoveryNeedsDriver&&!state.stopping&&!state.writer.frozen()&&!state.inventoryBusy&&state.pendingStartRequest.isEmpty()){
                            state.coldDiscoveryNeedsDriver=false;
                            if(!state.discover())state.coldDiscoveryNeedsDriver=true;
                        }
                    },Qt::QueuedConnection);
                }
                self.deliver({{"type","resource_snapshot"},{"driver_valid",true},
                    {"observed_utc",packet.value("observed_utc")},{"gpus",packet.value("gpus")}},received);
            }
            return bool(guard);
        },[guard,transport](const SshObservationResult& result){
            transport->deleteLater();if(!guard)return;auto& self=*guard->d_;
            self.hardware.clear();self.hardwareBusy=false;
            if(!self.stopping&&result.status!=SshObservationStatus::Complete)
                self.deliver({{"type","native_hardware_availability"},{"state","unknown"},{"error",result.error.left(400)}});
            if(guard)guard->d_->maybeStopped();
        });
    }
    void status(const QString& text){const auto cb=owner->onStatus;if(cb)cb(text);}
    void deliver(const QJsonObject& packet,std::optional<qint64> resourceReceiptNs={}){
        const QPointer<NativeApplicationRuntime> guard(owner);const auto cb=owner->onPacket;
        if(bridge)bridge->acceptNativeRuntimePacket(owner,packet,resourceReceiptNs);if(guard&&cb)cb(packet);
    }
    void initialFiniteAfterDelivery(const QJsonObject& packet){
        const auto type=packet.value("type").toString();
        if(!pendingInitialFinite||(type!="native_work_boundary_inventory"&&type!="application_start_inventory"))return;
        // A genuine terminal constant packet, including Unknown/Held, has now
        // reached the actual consumer. Finite is independent, not a license
        // for that packet, and must not precede it in the initial writer FIFO.
        pendingInitialFinite=false;const QPointer<NativeApplicationRuntime> guard(owner);
        QTimer::singleShot(0,owner,[guard]{
            if(!guard||guard->d_->stopping||guard->d_->writer.frozen())return;
            // Still the SAME serialized writer: a newly admitted profile or
            // mutation cannot overlap this read. No polling/retry is added.
            guard->d_->observeFinite();
        });
    }
    void emitPacket(QJsonObject packet){
        const QPointer<NativeApplicationRuntime> guard(owner);
        // Only a positive admission can mutate PREPARED state in its consumer.
        // Correlated refusals/cancellations must still reach that consumer after
        // freeze; they cannot create work and must not vanish as frozen writes.
        const auto operation=packet.value("type")=="application_start_inventory"&&packet.value("accepted")==true?NativeApplicationOperation::Mutation:NativeApplicationOperation::Read;
        // The existing UI admission callbacks run inside this same writer,
        // not beside a remote mutation. Frozen close facts remain admissible.
        const bool admitted=writer.enqueue(operation,[guard,packet=std::move(packet)](auto done){
            if(guard)guard->d_->deliver(packet);
            // The public packet sink may delete the owner or request close.
            if(guard)guard->d_->initialFiniteAfterDelivery(packet);done({});
        },[guard](const auto& result){if(guard&&!result.entered)guard->d_->status("Native presentation held: "+result.error.left(300));});
        if(!admitted&&guard)status("Native presentation queue refused; raw SOURCE remains archived");
    }
    void paused(bool value){queue->setDesiredPaused(value||!constantCapacityQualified);}
    void qualifyCapacity(const QJsonObject& snapshot){
        constantCapacityQualified=false;
        if(acquiredProfile&&acquiredProfile->device==Constant&&acquiredProfile->binding==snapshot.value("binding").toObject()&&acquiredProfile->sourceCapacity){
            const auto& capacity=*acquiredProfile->sourceCapacity;const auto scope=acquiredProfile->binding.value("test_scope").toObject();
            const auto a=scope.value("start"),z=scope.value("end"),first=acquiredProfile->binding.value("start"),last=acquiredProfile->binding.value("end");
            const auto decimal=[](const QJsonValue& value){return value.isString()&&value.toString().size()<=160&&source_decimal::canonical(value.toString());};
            // The qualified SDK domain CONTAINS the application's Zone1000;
            // it is not the same object. Preserve both boundaries explicitly.
            constantCapacityQualified=capacity.maximumLength=="1000000000000000"&&(scope.value("enabled").isUndefined()||scope.value("enabled")==true)&&scope.value("order")=="ascending"&&
                decimal(a)&&decimal(z)&&decimal(first)&&decimal(last)&&source_decimal::less(a.toString(),z.toString())&&
                !source_decimal::less(a.toString(),capacity.scopeStart)&&!source_decimal::less(capacity.scopeEnd,z.toString())&&
                source_decimal::less(first.toString(),last.toString())&&!source_decimal::less(first.toString(),a.toString())&&!source_decimal::less(z.toString(),last.toString());
        }
        if(constantCapacityQualified)profileError.clear();
        else if(acquiredProfile)profileError="Exact profile/snapshot binding or declared scope does not match";
        paused(!explicitDesired.value_or(false));
    }
    // Called only while an inventory/outer read ALREADY owns this writer.
    // No nested enqueue-and-wait deadlock; the continuation starts a real
    // physical snapshot after the actual pins/profile read has settled.
    void acquireProfile(std::function<void()> continuation,const QString& request={},const QString& inventoryId={}){
        acquiredProfile.reset();constantCapacityQualified=false;profileError="Configured profile has not been acquired";paused(true);
        if(stopping||profileBusy){continuation();return;}profileBusy=true;
        const QPointer<NativeApplicationRuntime> guard(owner);
        auto completion=std::make_shared<std::function<void()>>(std::move(continuation));
        const auto resume=[completion]{const auto callback=std::exchange(*completion,{});if(callback)callback();};
        const auto began=config.timingAudit?monotonicMs():0;
        if(config.timingAudit&&!timing("profile","profile_start",{{"request_id",request},{"inventory_id",inventoryId}}))return;
        if(stopping){profileBusy=false;resume();maybeStopped();return;}
        try{auto* transport=new NativeControlTransport(owner);profileClient=transport;
            transport->start(NativeControlCommand::configured3090Profile(config.transport.endpoint),config.controlTimeouts,
                [guard,transport,resume,request,inventoryId,began](const auto& result){
                    transport->deleteLater();if(!guard)return;auto& self=*guard->d_;self.profileClient.clear();self.profileBusy=false;
                    if(result.status==NativeControlTransportStatus::CompleteResponse&&result.control.outcome==NativeControlOutcome::ConfiguredProfileReported&&result.control.configuredProfile){
                        self.acquiredProfile=*result.control.configuredProfile;
                        self.profileError.clear();
                    }else self.profileError=(!result.error.isEmpty()?result.error:!result.control.error.isEmpty()?result.control.error:transportStatus(result.status)).left(240);
                    if(self.config.timingAudit)self.timing("profile","profile_done",{{"request_id",request},{"inventory_id",inventoryId},{"elapsed_ms",monotonicMs()-began},{"transport_status",transportStatus(result.status)},{"local_exit_code",result.localExitCode?QJsonValue(*result.localExitCode):QJsonValue(QJsonValue::Null)},{"qualified_profile",bool(self.acquiredProfile)}});
                    if(!guard)return;
                    // Missing/unqualified metadata cannot suppress actual
                    // observation, invent capacity, or launch a fallback.
                    resume();if(guard)self.maybeStopped();
                });
        }catch(const std::exception& error){if(profileClient){profileClient->deleteLater();profileClient.clear();}profileBusy=false;
            profileError=QString::fromUtf8(error.what()).left(240);
            if(config.timingAudit)timing("profile","profile_done",{{"request_id",request},{"inventory_id",inventoryId},{"elapsed_ms",monotonicMs()-began},{"transport_status","local_refusal"},{"qualified_profile",false}});
            if(!guard)return;
            status("Native profile unavailable; source-capacity admission held: "+QString::fromUtf8(error.what()).left(200));resume();}
    }
    void rejectPendingStart(const QString& reason){
        if(pendingStartRequest.isEmpty())return;
        const auto request=std::exchange(pendingStartRequest,{}),id=std::exchange(pendingStartInventory,{});
        emitPacket({{"type","application_start_inventory"},{"request_id",request},{"inventory_id",id},{"accepted",false},{"error",reason}});
    }
    void schedulePendingStart(){
        if(pendingStartRequest.isEmpty()||inventoryBusy||stopping||writer.frozen())return;
        const QPointer<NativeApplicationRuntime> guard(owner);
        QMetaObject::invokeMethod(owner,[guard]{
            if(!guard)return;auto& self=*guard->d_;
            if(self.pendingStartRequest.isEmpty()||self.inventoryBusy||self.stopping||self.writer.frozen())return;
            const auto request=std::exchange(self.pendingStartRequest,{}),id=std::exchange(self.pendingStartInventory,{});
            if(!self.beginInventory(request,id,true)&&guard)
                guard->d_->emitPacket({{"type","application_start_inventory"},{"request_id",request},{"inventory_id",id},{"accepted",false},{"error","Queued fresh inventory admission unavailable; no work prepared"}});
        },Qt::QueuedConnection);
    }
    bool requestStartInventory(const QString& request,const QString& id){
        if(!uuid(request)||!uuid(id)||stopping||writer.frozen())return false;
        if(!inventoryBusy&&pendingStartRequest.isEmpty())return beginInventory(request,id,true);
        // One bounded READ intent, not a physical command retry. A busy local
        // inventory must not revoke the whole bridge/telemetry capability.
        if(pendingStartRequest.isEmpty()){
            pendingStartRequest=request;pendingStartInventory=id;return true;
        }
        if(pendingStartRequest==request&&pendingStartInventory==id)return true;
        emitPacket({{"type","application_start_inventory"},{"request_id",request},{"inventory_id",id},{"accepted",false},{"error","A fresh start inventory is already queued; no work prepared"}});
        return true; // Request handled and explicitly refused, not transport loss.
    }
    bool beginInventory(const QString& request,const QString& id,bool initial){
        if(stopping||inventoryBusy||writer.frozen()||!uuid(request)||!uuid(id))return false;
        acquiredRegistry=booking::loadSourceRegistry(config.sourceRegistryDirectory);inventoryBusy=true;
        const QPointer<NativeApplicationRuntime> guard(owner);
        if(config.timingAudit&&!timing("writer","inventory_queued",{{"request_id",request},{"inventory_id",id},{"purpose",initial?"start":"boundary"}}))return false;
        const bool accepted=writer.enqueue(NativeApplicationOperation::Read,[guard,request,id,initial](auto done){
            if(!guard){done("Runtime owner unavailable");return;}auto& self=*guard->d_;
            if(self.stopping||self.writer.frozen()){self.inventoryBusy=false;done("Inventory cancelled before acquisition");return;}
            if(self.config.timingAudit)self.timing("writer","inventory_entered",{{"request_id",request},{"inventory_id",id},{"purpose",initial?"start":"boundary"}});
            if(!guard){done("Runtime owner unavailable");return;}
            const auto phase=QJsonObject{{"type","application_inventory_phase"},{"request_id",request},{"inventory_id",id},
                {"purpose",initial?"start":"boundary"},{"phase","profile"},{"phase_id",nonce()},{"active",true}};
            self.deliver(phase);if(!guard){done("Runtime owner unavailable");return;}
            self.acquireProfile([guard,request,id,initial,phase,done]{
                if(!guard){done("Runtime owner unavailable");return;}auto& self=*guard->d_;
                auto ended=phase;ended["active"]=false;self.deliver(ended);if(!guard){done("Runtime owner unavailable");return;}
                if(self.stopping||self.writer.frozen()){self.inventoryBusy=false;done("Inventory cancelled after profile acquisition");return;}
                const bool entered=initial?self.inventory->inspectStart(request,id):self.inventory->inspectBoundary(request,id);
                if(!entered)self.inventoryBusy=false;done(entered?QString{}:QString("Physical inventory admission unavailable"));
            },request,id);
        },[guard](const auto& result){if(guard&&!result.error.isEmpty()){guard->d_->inventoryBusy=false;guard->d_->status("Native inventory held: "+result.error.left(300));if(guard)guard->d_->schedulePendingStart();}});
        if(!accepted)inventoryBusy=false;return accepted;
    }
    bool discover(const QString& completedRun={}){
        if(stopping||inventoryBusy||!pendingStartRequest.isEmpty()||writer.frozen())return false;
        // Pay registry I/O BEFORE acquiring the physical boundary. Its actual
        // provenance/date survives; it is not a network lease or re-dated cache.
        if(!explicitDesired){const auto metadata=store.miningSessionMetadata(Constant);
            if(metadata&&metadata->mode!=MiningSessionMode::Unselected){explicitDesired=true;paused(false);}}
        const auto request=nonce();
        if(!completedRun.isEmpty()){completedBoundaryRequest=request;completedBoundaryInFlight=completedRun;}
        const bool entered=beginInventory(request,nonce(),false);
        if(!entered&&completedBoundaryRequest==request){completedBoundaryRequest.clear();completedBoundaryInFlight.clear();}
        return entered;
    }
    void scheduleCompletedBoundary(){
        if(stopping||completedBoundaryPending.isEmpty()||completedBoundaryQueued||completedBoundaryNeedsDriver||!completedBoundaryRequest.isEmpty()||writer.frozen())return;
        completedBoundaryQueued=true;const QPointer<NativeApplicationRuntime> guard(owner);
        QTimer::singleShot(0,owner,[guard]{
            if(!guard)return;auto& self=*guard->d_;self.completedBoundaryQueued=false;
            if(self.stopping||self.writer.frozen()||self.completedBoundaryPending.isEmpty())return;
            const auto run=self.completedBoundaryPending;
            if(run!=self.attachedRun||self.boundaryRuns.contains(run)){self.completedBoundaryPending.clear();return;}
            // Inventory may still be settling the exact final SOURCE frame.
            // Retain the wake-up; its packet callback schedules it once idle.
            if(self.inventoryBusy||!self.pendingStartRequest.isEmpty())return;
            self.completedBoundaryPending.clear();
            if(!self.discover(run))self.completedBoundaryPending=run;
        });
    }
    static NativeSourceReadKind sourceReadKind(const QJsonObject& snapshot,const QString& run){
        const auto completion=snapshot.value("source_completion_observation").toObject();
        return snapshot.value("owned")==false&&snapshot.value("state")=="OFF"&&
            completion.value("run")==run&&completion.value("source_range_complete")==true
            ?NativeSourceReadKind::CompletedSnapshot:NativeSourceReadKind::LiveStream;
    }
    void attachSource(const QJsonObject& snapshot){
        const auto manifest=snapshot.value("manifest").toObject();const auto run=manifest.value("run").toString();
        if(!runId(run)||manifest.value("uuid")!=Constant||run==attachedRun)return;
        const auto kind=sourceReadKind(snapshot,run);
        if(sourceActive){pendingSourceRun=run;pendingSourceManifest=manifest;pendingSourceKind=kind;inventory->source().cancelSource();return;}
        const auto error=store.saveSourceRunManifest(Constant,manifest);
        if(!error.isEmpty()){status("Native SOURCE held: "+error.left(300));return;}
        const auto request=nonce();
        if(inventory->source().startSource(request,run,manifest,kind)){attachedRun=run;attachedSourceRequest=request;attachedManifest=manifest;sourceActive=true;}
        else status("Native SOURCE attachment busy; no second client or retry created");
    }
    QString sourceBoundaryRead(const QString& request,const QString& id,const SshObservationFrame& frame){
        requireOwner();const auto manifest=frame.packet.value("manifest").toObject();
        const bool finalClose=writer.frozen()&&writer.closeRequestId()==request;
        const bool acquiring=!writer.frozen()&&inventoryBusy;
        if(stopping||!uuid(request)||!uuid(id)||(!finalClose&&!acquiring)||
           frame.packet.value("owned")!=false||frame.packet.value("state")!="OFF"||
           manifest!=attachedManifest||manifest.value("run")!=attachedRun||!runId(attachedRun))return {};
        // Waiting for an existing genuine read is not a reconnect. Its first
        // frame must still match the immutable manifest before SOURCE admission.
        if(sourceActive)return attachedSourceRequest;
        if(!lastSourceAttempt||lastSourceAttempt->requestId!=attachedSourceRequest||lastSourceAttempt->run!=attachedRun||
           sourceBoundaryReads.contains(request))return {};
        // A clean transport EOF is not a claim that a stopped producer had
        // already written its footer. This fresh inventory supplies the exact
        // additional extent; enroll it through the same source actor first.
        const bool observedSuffix=lastSourceAttempt->status==NativeSourceAttemptStatus::Complete&&lastSourceAttempt->nextOffset&&
            frame.pendingBytes&&*frame.pendingBytes>=0&&frame.offset<=*lastSourceAttempt->nextOffset&&
            frame.nextOffset<=std::numeric_limits<qint64>::max()-*frame.pendingBytes&&
            frame.nextOffset+*frame.pendingBytes>*lastSourceAttempt->nextOffset;
        const bool closeRepair=finalClose&&(lastSourceAttempt->status==NativeSourceAttemptStatus::Failed||
            lastSourceAttempt->status==NativeSourceAttemptStatus::TimedOut);
        if(!observedSuffix&&!closeRepair)return {};
        sourceBoundaryReads.insert(request); // consumed BEFORE entry, including refusal
        const auto next=nonce();
        const auto kind=observedSuffix?NativeSourceReadKind::CompletedSnapshot:sourceReadKind(frame.packet,attachedRun);
        if(!inventory->source().startSource(next,attachedRun,attachedManifest,kind))return {};
        attachedSourceRequest=next;sourceActive=true;return next;
    }
    void reconcile(const QJsonObject& snapshot){
        qualifyCapacity(snapshot);
        const auto manifest=snapshot.value("manifest").toObject();if(!runId(manifest.value("run").toString()))return;
        if(snapshot.value("owned")==true){
            const auto result=store.reconcileObservedDispatch(snapshot);
            // A legacy/unrecognised tuple is not adopted. Actual telemetry is
            // still observable; this result cannot synthesize STARTED.
            if(!result.error.isEmpty())status("Native dispatch reconciliation held: "+result.error.left(300));
        }
        // First owned reconciliation creates RUNTIME + exact manifest +
        // STARTED atomically. Saving before it would require the very RUNTIME
        // it is supposed to create, deadlocking every new native launch.
        const auto error=store.saveSourceRunManifest(Constant,manifest);
        if(!error.isEmpty())status("Native actual manifest held: "+error.left(300));
    }
    void publication(const NativeSourcePublication& publication){
        if(stopping)return;
        if(publication.conservation.value("ready")==true)sourceReconnectFailures=0;
        if(publication.snapshot.value("driver_valid")==true&&publication.resourceReceiptNs)lastHardwareReceiptNs=publication.resourceReceiptNs;
        auto incoming=publication;
        if(latestPublication){
            try{carryRawCounterPair(latestPublication->snapshot,incoming.snapshot,NativeSourceRuntime::PublicationLimit-128);}
            catch(const std::exception& error){status("Native counter presentation held: "+QString::fromUtf8(error.what()).left(300));return;}
            ++coalescedPublications;
        }
        latestPublication=std::move(incoming);
        if(sourcePublicationQueued)return;sourcePublicationQueued=true;
        const QPointer<NativeApplicationRuntime> guard(owner);
        const bool admitted=writer.enqueue(NativeApplicationOperation::Read,[guard](auto done){
            if(!guard){done("Runtime owner unavailable");return;}auto& self=*guard->d_;self.sourcePublicationQueued=false;
            if(self.stopping||!self.latestPublication){done({});return;}
            auto value=std::move(*self.latestPublication);self.latestPublication.reset();
            if(self.coalescedPublications){value.snapshot["native_application_coalesced_frames"]=self.coalescedPublications;self.coalescedPublications=0;}
            self.reconcile(value.snapshot);if(!guard){done({});return;}
            auto packet=value.snapshot;packet["type"]="snapshot";self.deliver(packet,value.resourceReceiptNs);if(!guard){done({});return;}
            if(!value.nativeIndex.isEmpty()){auto index=value.nativeIndex;index["type"]="native_index";self.deliver(index);if(!guard){done({});return;}}
            const auto run=value.snapshot.value("manifest").toObject().value("run").toString();
            const bool terminal=value.snapshot.value("owned")==false&&value.snapshot.value("state")=="OFF"&&value.snapshot.value("pending_bytes").toInteger(-1)==0;
            // Once per actual run; this does not retry an uncertain launch.
            if(terminal&&value.conservation.value("ready")==true&&!self.boundaryRuns.contains(run)){
                self.completedBoundaryPending=run;self.scheduleCompletedBoundary();
            }
            done({});
        },[guard](const auto& result){if(guard&&!result.entered){guard->d_->sourcePublicationQueued=false;guard->d_->status("Native publication held; SOURCE database retained");}});
        if(!admitted)sourcePublicationQueued=false;
    }
    void sourceFinished(const NativeSourceAttempt& attempt){
        sourceActive=false;lastSourceAttempt=attempt;if(stopping)return;
        sourceReconnectTimer.stop();
        QString label;switch(attempt.status){case NativeSourceAttemptStatus::Complete:label="complete";break;case NativeSourceAttemptStatus::Failed:label="failed";break;case NativeSourceAttemptStatus::TimedOut:label="timed_out";break;case NativeSourceAttemptStatus::Cancelled:label="cancelled";break;case NativeSourceAttemptStatus::ReplayRequired:label="replay_required";break;}
        const QPointer<NativeApplicationRuntime> guard(owner);
        emitPacket({{"type","native_source_attempt"},{"request_id",attempt.requestId},{"run",attempt.run},{"epoch",QString::number(attempt.epoch)},
            {"status",label},{"error",attempt.error.left(400)},{"local_exit",attempt.transport.exitCode?QJsonValue(*attempt.transport.exitCode):QJsonValue(QJsonValue::Null)},
            {"next_offset",attempt.nextOffset?QJsonValue(*attempt.nextOffset):QJsonValue(QJsonValue::Null)},
            {"committed_offset",attempt.committedOffset?QJsonValue(*attempt.committedOffset):QJsonValue(QJsonValue::Null)},
            {"received_frames",attempt.transport.completeFrames},{"scope","LOCAL_SOURCE_TRANSPORT_ATTEMPT_NOT_PHYSICAL_CONTROL_OR_CONSERVATION"}});
        if(!guard||stopping)return;
        if(!pendingSourceRun.isEmpty()){
            const auto run=std::exchange(pendingSourceRun,{});
            const auto manifest=std::exchange(pendingSourceManifest,{});const auto request=nonce();
            const auto kind=std::exchange(pendingSourceKind,NativeSourceReadKind::LiveStream);
            if(run!=attachedRun&&inventory->source().startSource(request,run,manifest,kind)){attachedRun=run;attachedManifest=manifest;attachedSourceRequest=request;sourceActive=true;}return;
        }
        // An ended stream can reveal a NEW physical epoch, never authorize
        // restarting the same failed epoch or resending its control command.
        if(attempt.status==NativeSourceAttemptStatus::Complete&&!boundaryRuns.contains(attempt.run)){
            // One successful finite read may finish without an OFF frame.
            // Acquire the real boundary once; never infer exit/conservation,
            // retry a failed SOURCE/control, or dispatch from this receipt.
            completedBoundaryPending=attempt.run;scheduleCompletedBoundary();
        }
        else if(attempt.status==NativeSourceAttemptStatus::ReplayRequired)discover();
        else if(attempt.status!=NativeSourceAttemptStatus::Complete&&attempt.status!=NativeSourceAttemptStatus::Cancelled){
            status("Native SOURCE unavailable: "+attempt.error.left(300));
            // Only a transport outage is replayable. Corrupt RAW, manifest,
            // storage and proof failures do NOT enter an automatic retry loop.
            const bool outage=attempt.status==NativeSourceAttemptStatus::TimedOut||
                (attempt.status==NativeSourceAttemptStatus::Failed&&(attempt.transport.exitCode==255||isSshHostStartupResourceFailure(attempt.transport)));
            if(outage&&!writer.frozen()&&attempt.run==attachedRun&&attempt.requestId==attachedSourceRequest){
                sourceReconnectFailures=std::min(6U,sourceReconnectFailures+1);
                const int delay=std::min(30000,1000*(1<<int(sourceReconnectFailures-1)));
                sourceReconnectTimer.start(delay);
                emitPacket({{"type","native_source_reconnect"},{"run",attachedRun},{"delay_ms",delay},
                    {"committed_offset",attempt.committedOffset?QJsonValue(*attempt.committedOffset):QJsonValue(QJsonValue::Null)},
                    {"scope","READ_ONLY_REPLAY_NOT_PHYSICAL_CONTROL_OR_CONSERVATION"}});
            }
        }
    }
    void boundary(const QString& request,const QString& id,const SshObservationFrame& frame,const QJsonObject& conservation){
        if(stopping)return;
        qualifyCapacity(frame.packet);
        // A successful SOURCE lease can end while its last published frame is
        // still STOPPING. The later fresh inventory is physical observation,
        // NOT a new admitted SOURCE frame. Deliver only physical fields so the
        // card can leave STOPPING without importing counters/proof/Ready.
        if(frame.packet.value("owned")==false&&frame.packet.value("state")=="OFF"&&frame.packet.value("manifest").isObject()){
            QJsonObject physical{{"type","native_process_observation"}};
            for(const auto* key:{"schema","observed_utc","owned","state","operator_stop","manifest","binding","last_exit"})
                physical.insert(QLatin1String(key),frame.packet.value(QLatin1String(key)));
            const QPointer<NativeApplicationRuntime> guard(owner);deliver(physical);if(!guard)return;
        }
        if(frame.packet.value("owned")==false&&frame.packet.value("state")=="OFF"&&frame.packet.value("operator_stop")==true&&frame.packet.value("manifest").isNull()){
            const auto observed=std::exchange(configuredOffRequests,{});
            for(const auto& off:observed)emitPacket({{"type","native_control_observation"},{"device",Constant},{"request_id",off},{"snapshot",frame.packet}});
        }
        if(request==armRequest){
            auto age=std::make_shared<QElapsedTimer>();age->start();const auto booking=std::exchange(armBooking,{});armRequest.clear();
            const auto registry=acquiredRegistry;const QPointer<NativeApplicationRuntime> guard(owner);
            auto output=std::make_shared<QJsonObject>(QJsonObject{{"type","dispatch"},{"booking_id",booking},{"accepted",false}});
            const bool accepted=writer.enqueue(NativeApplicationOperation::Mutation,[guard,frame,registry,booking,age,output](auto done){
                if(!guard){done("Runtime owner unavailable");return;}auto& self=*guard->d_;
                need(!self.stopping&&!self.writer.frozen()&&age->elapsed()<=self.config.maximumObservationAgeMs,"Fresh reservation arm expired or closing");
                need(self.constantCapacityQualified,"Actual source SDK/pins/capacity guard not acquired; reservation unchanged");
                const auto scope=scopeOf(frame.packet);int matches=0;
                // SourceRange.row is CSV provenance INCLUDING its header,
                // not a zone identity. Match the actual qualified interval.
                for(const auto& row:registry.ranges)if(row.start==scope.executionStart&&row.end==scope.executionEnd&&row.status=="Open"&&row.owner.isEmpty())++matches;
                const bool adaptive=self.acquiredProfile&&self.acquiredProfile->sourceCapacity&&self.acquiredProfile->sourceCapacity->nativePlanRequired;
                if(adaptive)need(registry.valid(),"Fresh registry required for adaptive reservation; target membership is checked by the same store");
                else need(matches==1,"Reservation arm scope must match actual unique Open Zone1000");
                const auto result=self.store.armQueuedIntent(booking,Constant,scope,registry);
                need(result.status==booking::ArmQueuedStatus::Armed,"Native existing reservation could not be armed");
                (*output)["accepted"]=true;(*output)["detail"]="Existing local reservation armed in numeric queue; no future binding launched";done({});
            },[guard,output](const auto& result){if(guard){if(!result.error.isEmpty())(*output)["error"]=result.error.left(400);guard->d_->emitPacket(*output);}});
            if(!accepted){(*output)["error"]="Native writer refused reservation arm; existing intent preserved";emitPacket(*output);}return;
        }
        // submit starts the acquisition TTL NOW, not when another writer
        // operation eventually enters. It owns the subsequent SQL/encoding
        // cost and rechecks the same timer adjacent to the transport send.
        const auto sourceManifest=frame.packet.value("manifest").toObject();
        if(sourceManifest.value("queued_intent").isNull()&&sourceManifest.value("source_kind")=="NATIVE_PGN_SCIENCE"){
            // Account for a completed physical ticket segment independently
            // of planning its successor. A community registry/queue is not
            // allocation authority for a native chain-issued ticket.
            try{
                need(nativeScientificSearchOrigin(sourceManifest),"Native ticket source context unavailable");
                const auto receipt=verifiedSourceCompletionAtBoundary(frame,conservation);
                auto age=std::make_shared<QElapsedTimer>();age->start();const QPointer<NativeApplicationRuntime> guard(owner);
                const bool entered=writer.enqueue(NativeApplicationOperation::Mutation,[guard,sourceManifest,age](auto done){
                    if(!guard){done("Runtime owner unavailable");return;}auto& self=*guard->d_;
                    need(!self.stopping&&!self.writer.frozen()&&age->elapsed()<=self.config.maximumObservationAgeMs,"Ticket completion observation expired or closing");
                    done(self.store.recordSourceCompletion(sourceManifest.value("run").toString(),sourceManifest.value("start").toString(),sourceManifest.value("end").toString(),Constant));
                },[guard,receipt](const auto& result){if(guard){
                    if(!result.error.isEmpty()){guard->d_->status("Ticket completion held: "+result.error.left(300));return;}
                    guard->d_->emitPacket({{"type","source_completion"},{"receipt",receipt},{"community_updated",false}});
                    if(guard)guard->d_->status("Ticket segment completed · results retained");
                }});
                if(!entered)status("Ticket completion waiting for the application writer");
            }catch(const std::exception& error){status("Ticket completion held: "+QString::fromUtf8(error.what()).left(300));}
            observeSource(frame);return;
        }
        if(frame.packet.value("owned")==false&&frame.packet.value("state")=="OFF"&&conservation.value("ready")==true){
            try{queue->submit(request,id,frame,conservation,scopeOf(frame.packet),acquiredRegistry);}
            catch(const std::exception& error){status("Native next boundary held: "+QString::fromUtf8(error.what()).left(300));}
        }
        observeSource(frame);
    }
    void observeSource(const SshObservationFrame& frame){
        const QPointer<NativeApplicationRuntime> guard(owner);
        writer.enqueue(NativeApplicationOperation::Read,[guard,frame](auto done){
            if(!guard){done("Runtime owner unavailable");return;}auto& self=*guard->d_;
            if(self.stopping){done({});return;}
            self.reconcile(frame.packet);if(!guard){done({});return;}self.attachSource(frame.packet);if(!guard){done({});return;}
            done({});
        },[guard](const auto& result){if(guard&&!result.error.isEmpty())guard->d_->status("Native boundary held: "+result.error.left(300));});
    }
    void controlPacket(const QJsonObject& packet){
        const QPointer<NativeApplicationRuntime> deliveryGuard(owner);
        emitPacket(packet);
        if(!deliveryGuard)return;
        const bool latched=packet.value("type")=="control"&&packet.value("device")==Constant&&packet.value("desired_off_latched")==true&&uuid(packet.value("request_id").toString());
        if(latched)configuredOffRequests.insert(packet.value("request_id").toString());
        const auto result=packet.value("result").toObject();
        if(latched||(packet.value("type")=="control"&&packet.value("accepted")==true&&
            (result.value("status")!="ALREADY_OFF_OBSERVED"||result.value("requires_fresh_observation")==true))){
            const QPointer<NativeApplicationRuntime> guard(owner);
            QTimer::singleShot(0,owner,[guard]{if(guard){guard->d_->discover();guard->d_->observeFinite();}});
        }
    }
    void queuePacket(const QJsonObject& packet){
        const QPointer<NativeApplicationRuntime> guard(owner);
        emitPacket(packet);
        if(!guard)return;
        if(packet.value("type")=="queued_boundary"&&packet.value("status")=="Held"&&
           packet.value("reacquire_prepared_unsent")==true&&packet.value("transport_attempted")==false&&
           packet.value("run")==attachedRun&&!stopping&&!writer.frozen()){
            // Reacquire physical facts after a costly local INSERT, then use
            // its SAME saved dispatch. This capability exists only before
            // any transport attempt; an uncertain launch can never enter it.
            boundaryRuns.remove(attachedRun);completedBoundaryPending=attachedRun;
            scheduleCompletedBoundary();
        }
        if(packet.value("type")=="queued_boundary"&&packet.value("status")=="RequestReported"){
            const QPointer<NativeApplicationRuntime> guard(owner);QTimer::singleShot(0,owner,[guard]{if(guard)guard->d_->discover();});
        }
    }
    void observeFinite(){
        if(config.transport.sourceOnly||stopping||finiteBusy||writer.frozen())return;finiteBusy=true;
        const auto operation=config.timingAudit?nonce():QString{};
        const QPointer<NativeApplicationRuntime> guard(owner);
        if(config.timingAudit&&!timing("finite","finite_queued",{{"operation_id",operation}}))return;
        const bool admitted=writer.enqueue(NativeApplicationOperation::Read,[guard,operation](auto done){
            if(!guard){done("Runtime owner unavailable");return;}auto& self=*guard->d_;
            if(self.stopping||self.writer.frozen()){self.finiteBusy=false;done({});self.maybeStopped();return;}
            const auto began=self.config.timingAudit?monotonicMs():0;
            if(self.config.timingAudit)self.timing("finite","finite_start",{{"operation_id",operation}});
            if(!guard){done("Runtime owner unavailable");return;}
            if(self.stopping||self.writer.frozen()){self.finiteBusy=false;done({});self.maybeStopped();return;}
            auto* transport=new NativeControlTransport(guard);self.finite=transport;
            transport->start(NativeControlCommand::finite3060Snapshot(self.config.transport.endpoint),self.config.controlTimeouts,[guard,transport,done,operation,began](const auto& result){
                transport->deleteLater();if(!guard){done("Runtime owner unavailable");return;}auto& self=*guard->d_;self.finite.clear();self.finiteBusy=false;
                if(self.config.timingAudit)self.timing("finite","finite_done",{{"operation_id",operation},{"elapsed_ms",monotonicMs()-began},{"transport_status",transportStatus(result.status)},{"local_exit_code",result.localExitCode?QJsonValue(*result.localExitCode):QJsonValue(QJsonValue::Null)},{"known_observation",bool(result.control.finiteObservation)}});
                if(!guard){done("Runtime owner unavailable");return;}
                if(result.status==NativeControlTransportStatus::CompleteResponse&&result.control.finiteObservation){
                    const auto packet=result.control.finiteObservation->packet;self.emitPacket({{"type","device_control_snapshot"},{"device",QString::fromLatin1(Finite3060Device)},{"snapshot",packet}});
                    if(packet.value("desired_enabled")==false){self.finiteRequested=false;self.finiteTimer.stop();}
                }else self.status("Native finite observation unavailable; no idle/zero fabricated");
                done({});if(guard)self.maybeStopped();
            });
        },[guard](const auto& result){if(guard&&!result.entered){guard->d_->finiteBusy=false;guard->d_->maybeStopped();}});
        if(!admitted)finiteBusy=false;
    }
    bool submit(const QJsonObject& command){
        requireOwner();if(!started||stopping)return false;
        const auto action=command.value("action").toString();
        if(config.transport.sourceOnly&&command.value("device").toString().compare(QString::fromLatin1(Finite3060Device),Qt::CaseInsensitive)==0)return false;
        if(action=="application_close_prepare"){
            rejectPendingStart("Application close cancelled queued read; no work prepared");
            hardwareTimer.stop();if(hardware)hardware->cancel();
            pendingInitialFinite=false;explicitDesired=false;paused(true);finiteTimer.stop();
            const QPointer<NativeApplicationRuntime> guard(owner);const bool accepted=inventory->prepareClose(command.value("request_id").toString());
            // Cancel only OUR autonomous read client after the accepted freeze.
            // Inventory's separate fresh finite read remains mandatory.
            if(accepted&&guard&&finite)finite->cancel();return accepted;
        }
        if(action=="application_start_inventory"||action=="application_close_inventory"){
            if(action=="application_start_inventory")return requestStartInventory(command.value("request_id").toString(),command.value("inventory_id").toString());
            if(inventoryBusy)return false;inventoryBusy=true;
            const bool ok=inventory->inspectClose(command.value("request_id").toString(),command.value("inventory_id").toString());if(!ok)inventoryBusy=false;return ok;
        }
        if(action=="module_list"||action=="module_receipt"||action=="module_register"||action=="module_select"){
            const QPointer<NativeApplicationRuntime> guard(owner);
            return writer.enqueue(action=="module_list"||action=="module_receipt"?NativeApplicationOperation::Read:NativeApplicationOperation::Mutation,
                [guard,command](auto done){
                    if(guard){QJsonObject packet;
                        try{packet=handleModuleRegistryCommand(guard->d_->modules,command);}
                        catch(const std::exception& error){packet={{"type","module_registry"},{"request_id",command.value("request_id")},{"accepted",false},{"applied",false},{"error",QString::fromUtf8(error.what()).left(400)}};}
                        guard->d_->deliver(packet);
                    }done({});
                },
                [guard,command](const auto& result){if(guard&&!result.error.isEmpty())guard->d_->emitPacket({{"type","module_registry"},{"request_id",command.value("request_id")},{"accepted",false},{"applied",false},{"error",result.error.left(400)}});});
        }
        if(action=="dispatch"){
            const auto booking=command.value("booking_id").toString();
            if(inventoryBusy||writer.frozen()||!uuid(booking))return false;
            armRequest=nonce();armBooking=booking;
            if(beginInventory(armRequest,nonce(),false))return true;armRequest.clear();armBooking.clear();return false;
        }
        if(action=="start"||action=="stop"||action=="start_prepared"||action=="resume_prepared"){
            const auto device=command.value("device").toString();
            if((action=="start_prepared"||action=="resume_prepared")&&!constantCapacityQualified){
                emitPacket({{"type","control"},{"request_id",command.value("request_id")},{"device",device},{"accepted",false},{"applied",false},{"error","Actual qualified source capacity unavailable; existing PREPARED retained unsent"}});return false;
            }
            const auto request=command.value("request_id").toString();
            const bool novel=uuid(request)&&!seenControlRequests.contains(request);
            if(novel&&action=="stop"&&command.size()==3&&device==Constant){
                explicitDesired=false;paused(true);inventory->cancelLineage();
                if(profileClient)profileClient->cancel();
            }
            const bool accepted=control->submit(command);
            if(novel&&(accepted||(action=="stop"&&command.size()==3&&(device==Constant||device==QString::fromLatin1(Finite3060Device)))))seenControlRequests.insert(request);
            if(accepted&&(action=="start_prepared"||action=="resume_prepared")){explicitDesired=true;paused(false);}
            if(accepted&&device==QString::fromLatin1(Finite3060Device)){finiteRequested=action=="start";finiteTimer.start();}
            return accepted;
        }
        status("Unknown native application action refused; no legacy fallback");return false;
    }
};
NativeApplicationRuntime::NativeApplicationRuntime(NativeApplicationRuntimeConfig config,NativeBridge* bridge,QObject* parent)
    :QObject(parent),d_(std::make_unique<Impl>(this,std::move(config),bridge)){}
NativeApplicationRuntime::~NativeApplicationRuntime(){d_->requireOwner();d_->pendingInitialFinite=false;d_->stopping=true;d_->finiteTimer.stop();d_->hardwareTimer.stop();if(d_->hardware)d_->hardware->cancel();if(d_->finite)d_->finite->cancel();if(d_->profileClient)d_->profileClient->cancel();}
QString NativeApplicationRuntime::managedSourceDevice()const{return Constant;}
bool NativeApplicationRuntime::start(){
    d_->requireOwner();if(d_->started||d_->stopping||!d_->bridge)return false;const QPointer<NativeApplicationRuntime> guard(this);
    if(!d_->bridge->attachNativeRuntime(this,[guard](const auto& command){return guard&&guard->submit(command);}))return false;
    d_->started=true;d_->pendingInitialFinite=true;d_->discover();d_->hardwareTimer.start();d_->observeHardware();return true;
}
bool NativeApplicationRuntime::submit(const QJsonObject& command){return d_->submit(command);}
void NativeApplicationRuntime::stop(){
    d_->requireOwner();
    d_->sourceReconnectTimer.stop();
    if(d_->stopping)return;d_->pendingInitialFinite=false;d_->stopping=true;d_->explicitDesired=false;d_->paused(true);d_->finiteTimer.stop();d_->hardwareTimer.stop();if(d_->hardware)d_->hardware->cancel();if(d_->finite)d_->finite->cancel();if(d_->profileClient)d_->profileClient->cancel();d_->inventory->stop();
}
bool NativeApplicationRuntime::stopped()const{d_->requireOwner();return d_->stopped;}
}

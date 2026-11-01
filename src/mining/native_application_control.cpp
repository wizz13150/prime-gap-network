#include "mining/native_application_control.h"
#include "mining/native_application_inventory.h"
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QMetaType>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QUuid>
#include <limits>
#include <chrono>
#include <stdexcept>

namespace pgn::mining {
namespace {
const QString Constant="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",Finite=QString::fromLatin1(Finite3060Device);
qint64 monotonicMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool uuid(const QString& id){const auto u=QUuid::fromString(id);return !u.isNull()&&u.toString(QUuid::WithoutBraces)==id;}
bool utc(const QJsonValue& value){
    if(!value.isString()||value.toString().size()>40)return false;
    static const QRegularExpression pattern("^([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2})(?:\\.[0-9]{1,9})?Z$");
    const auto match=pattern.match(value.toString());if(!match.hasMatch()||match.capturedLength()!=value.toString().size())return false;
    const auto date=QDateTime::fromString(match.captured(1)+'Z',Qt::ISODate);
    return date.isValid()&&date.toUTC().toString("yyyy-MM-dd'T'HH:mm:ss")==match.captured(1);
}
bool ended(const QJsonObject& snapshot){
    const auto value=snapshot.value("last_exit");if(!value.isObject())return false;const auto exit=value.toObject();
    const auto code=exit.value("exit_code");
    return exit.value("run")==snapshot.value("manifest").toObject().value("run")&&
        code.toVariant().metaType().id()==QMetaType::LongLong&&code.toInteger()==0&&utc(exit.value("utc"));
}
QJsonObject reply(const QJsonObject& command){
    QJsonObject result{{"type","control"},{"accepted",false},{"applied",false}};
    for(const auto* key:{"request_id","device","action"}){const auto v=command.value(QLatin1String(key));result.insert(QLatin1String(key),v.isString()&&v.toString().size()<=128?v:QJsonValue(QJsonValue::Null));}
    return result;
}
}
class NativeApplicationControl::Impl {
public:
    NativeApplicationControl* owner;NativeApplicationWriter& writer;booking::BookingStore& store;
    SshObservationEndpoint endpoint;NativeControlTimeouts timeouts;
    std::unique_ptr<NativePreparedControl> prepared;
    int maximumAge;
    struct CloseTicket {NativeCloseInitialObservation observation;QSet<QString> consumedDevices;};
    std::optional<CloseTicket> closeTicket;
    struct Pending {
        QString id,device,action;QJsonObject packet,finiteSnapshot,coldObservation;
        NativeApplicationWriter::Done done;
        std::optional<SshObservationFrame> frame;
        quint64 generation{};
        QString closeId,inventoryId;quint64 inventoryEpoch{};
    };
    QHash<QString,std::shared_ptr<Pending>> attempts;QSet<QString> consumed;
    quint64 finiteGeneration{};bool finiteGenerationExhausted{};
    bool constantStartSeen{},finiteStartSeen{};
    bool mayObserveAlreadyOff(const QString& device)const{
        if(device==Constant?constantStartSeen:finiteStartSeen)return false;
        const auto pending=store.hasPreparedDispatches();return pending.has_value()&&!*pending;
    }
    void alreadyOff(const std::shared_ptr<Pending>& attempt,bool requiresFresh=false){
        QJsonObject result{{"status","ALREADY_OFF_OBSERVED"},{"scope","READ_ONLY_PROFILE_OBSERVATION_NOT_COMMAND_ACK"}};
        if(requiresFresh)result["requires_fresh_observation"]=true;
        attempt->packet["accepted"]=true;attempt->packet["physical_outcome"]="UNKNOWN";attempt->packet["result"]=result;
        attempt->done({});
    }
    bool rememberClose(const NativeCloseInitialObservation& value){
        requireOwner();const auto now=monotonicMs();
        if(!writer.frozen()||writer.closeRequestId()!=value.closeId||!uuid(value.closeId)||!uuid(value.inventoryId)||value.epoch==0||
           value.beganMonotonicMs<0||value.beganMonotonicMs>now||now-value.beganMonotonicMs>maximumAge)return false;
        if(closeTicket&&(closeTicket->observation.closeId!=value.closeId||value.epoch<=closeTicket->observation.epoch))return false;
        closeTicket=CloseTicket{value,{}};return true;
    }
    bool tryInitialAlreadyOff(const std::shared_ptr<Pending>& attempt){
        if(attempt->action!="stop"||!closeTicket||!writer.frozen())return false;
        auto& ticket=*closeTicket;const auto& value=ticket.observation;const auto now=monotonicMs();
        if(writer.closeRequestId()!=value.closeId||attempt->closeId!=value.closeId||attempt->inventoryId!=value.inventoryId||
           attempt->inventoryEpoch!=value.epoch||ticket.consumedDevices.contains(attempt->device))return false;
        ticket.consumedDevices.insert(attempt->device); // one opportunity, including a rejected predicate
        if(value.beganMonotonicMs>now||now-value.beganMonotonicMs>maximumAge||!mayObserveAlreadyOff(attempt->device))return false;
        try{
            if(attempt->device==Constant){
                const auto& snapshot=value.constant.packet;
                if(snapshot.value("schema")!="pgn-runtime-snapshot-1"||snapshot.value("owned")!=false||snapshot.value("state")!="OFF"||snapshot.value("operator_stop")!=true||
                   !snapshot.value("driver_valid").isBool()||!utc(snapshot.value("observed_utc"))||snapshot.value("binding").toObject().value("uuid")!=Constant)return false;
                if(snapshot.value("manifest").isNull())attempt->coldObservation=snapshot;
                else {NativeControlCommand::stopObservedSnapshot(endpoint,value.constant);if(!ended(snapshot))return false;}
                alreadyOff(attempt,!snapshot.value("manifest").isNull());return true;
            }
            // Re-project the ORIGINAL packet through the strict decoder; never
            // trust independently mutable bools in a caller-built typed DTO.
            const auto parsed=NativeControlCommand::finite3060Snapshot(endpoint).decode(QJsonDocument(value.finite.packet).toJson(QJsonDocument::Compact),true,0);
            if(parsed.outcome!=NativeControlOutcome::ObservationReported||!parsed.finiteObservation)return false;
            const auto& f=*parsed.finiteObservation;
            if(f.owned||f.desiredEnabled||(f.manifest&&(!f.lastExit||f.lastExit->run!=f.manifest->run||f.lastExit->exitCode!=0)))return false;
            attempt->finiteSnapshot=f.packet;alreadyOff(attempt);return true;
        }catch(const std::exception&){return false;}
    }
    bool cancelled(const Pending& attempt)const{return attempt.action=="start"&&(finiteGenerationExhausted||attempt.generation!=finiteGeneration);}
    void invalidateFinite(){if(finiteGeneration==std::numeric_limits<quint64>::max())finiteGenerationExhausted=true;else ++finiteGeneration;}
    void markCancelled(QJsonObject& packet)const{
        packet["accepted"]=false;packet["cancelled_generation"]=true;packet["physical_outcome"]="UNKNOWN";
        packet["error"]="Finite ON intent invalidated by explicit STOP; fresh observation required, no automatic retry";
    }
    Impl(NativeApplicationControl* o,NativeApplicationWriter& w,booking::BookingStore& store,SshObservationEndpoint e,NativeControlTimeouts t,int maximumAge)
        :owner(o),writer(w),store(store),endpoint(std::move(e)),timeouts(t),maximumAge(maximumAge){
        requireOwner();need(writer.thread()==owner->thread(),"ONE same-thread control writer required");
        prepared=std::make_unique<NativePreparedControl>(writer,store,endpoint,timeouts,maximumAge);
        const QPointer<NativeApplicationControl> guard(owner);
        prepared->onPacket=[guard](const auto& packet){if(guard)guard->d_->publish(packet);};
    }
    ~Impl(){for(const auto& attempt:attempts)if(attempt->done)attempt->done("Application control destroyed; remote outcome UNKNOWN, no retry");}
    void requireOwner()const{need(QThread::currentThread()==owner->thread(),"Control requires creating thread");}
    void publish(const QJsonObject& packet){const auto cb=owner->onPacket;if(cb)cb(packet);}
    void send(const std::shared_ptr<Pending>& attempt,const NativeControlCommand& command){
        if(cancelled(*attempt)){attempt->done("Finite ON invalidated before send; no command submitted");return;}
        const QPointer<NativeApplicationControl> guard(owner);auto* transport=new NativeControlTransport(owner);
        transport->start(command,timeouts,[guard,attempt,transport](const NativeControlTransportResult& result){
            transport->deleteLater();if(!guard)return;
            if(result.status==NativeControlTransportStatus::CompleteResponse&&result.control.outcome==NativeControlOutcome::ConfiguredOffLatchReported){
                attempt->packet["result"]=result.control.packet;attempt->packet["reported_outcome"]="ConfiguredOffLatchReported";
                attempt->packet["accepted"]=false;attempt->packet["desired_off_latched"]=true;attempt->packet["physical_outcome"]="UNKNOWN";
                attempt->packet["error"]="Configured desired-OFF latched only; physical state and uncertain remote starts require fresh reconciliation";
                attempt->done({});return;
            }
            const bool finite=result.control.outcome==NativeControlOutcome::ObservationReported&&result.control.finiteObservation.has_value();
            const bool report=result.control.outcome==NativeControlOutcome::RequestReported||result.control.outcome==NativeControlOutcome::AlreadyRunningReported;
            const bool accepted=result.status==NativeControlTransportStatus::CompleteResponse&&(finite||report);
            if(accepted){attempt->packet["accepted"]=true;attempt->packet["result"]=result.control.packet;
                attempt->packet["reported_outcome"]=finite?"ObservationReported":result.control.outcome==NativeControlOutcome::RequestReported?"RequestReported":"AlreadyRunningReported";
                if(finite)attempt->finiteSnapshot=result.control.finiteObservation->packet;}
            const auto error=accepted?QString{}:!result.error.isEmpty()?result.error:!result.control.error.isEmpty()?result.control.error:QString("Controller did not report a qualified response; outcome unconfirmed");
            if(attempt->done)attempt->done(error);
        });
    }
    void configuredStopConstant(const std::shared_ptr<Pending>& attempt,const QString& inventoryError){
        if(!inventoryError.isEmpty())attempt->packet["inventory_error"]=inventoryError.left(400);
        const QPointer<NativeApplicationControl> guard(owner);auto* transport=new NativeControlTransport(owner);
        transport->start(NativeControlCommand::configured3090Profile(endpoint),timeouts,[guard,attempt,transport](const NativeControlTransportResult& result){
            transport->deleteLater();if(!guard)return;
            try{
                need(result.status==NativeControlTransportStatus::CompleteResponse&&result.control.outcome==NativeControlOutcome::ConfiguredProfileReported&&result.control.configuredProfile.has_value(),"Configured3090 profile/capability unavailable; no legacy STOP fallback");
                guard->d_->send(attempt,NativeControlCommand::configured3090StopLatch(guard->d_->endpoint,attempt->id,
                    guard->d_->writer.frozen()?Configured3090OffReason::ApplicationClose:Configured3090OffReason::ExplicitCardOff,*result.control.configuredProfile));
            }catch(const std::exception& e){attempt->done(QString::fromUtf8(e.what()).left(400));}
        });
    }
    void stopConstant(const std::shared_ptr<Pending>& attempt){
        const QPointer<NativeApplicationControl> guard(owner);auto* transport=new SshSnapshotTransport(owner);
        SshObservationRequest read;read.noDriver=true;read.startupTimeoutMs=timeouts.startupMs;read.idleTimeoutMs=timeouts.deadlineMs;read.snapshotTimeoutMs=timeouts.deadlineMs;
        transport->start(endpoint,read,[guard,attempt](const SshObservationFrame& frame){if(!guard)return false;attempt->frame=frame;return true;},
            [guard,attempt,transport](const SshObservationResult& result){
                transport->deleteLater();if(!guard)return;
                try{
                    if(result.status!=SshObservationStatus::Complete||result.exitCode!=0||!attempt->frame){
                        guard->d_->configuredStopConstant(attempt,"Process inventory unavailable: "+result.error.left(300));return;
                    }
                    const auto& frame=*attempt->frame;const auto& snapshot=frame.packet;
                    // An already OFF, empty profile with a persisted STOP is a
                    // read-only observation, not a fabricated command receipt.
                    // Empty without STOP is NOT permission to use legacy stop.
                    if(snapshot.value("manifest").isNull()){
                        if(!(snapshot.value("schema")=="pgn-runtime-snapshot-1"&&snapshot.value("owned")==false&&snapshot.value("state")=="OFF"&&snapshot.value("operator_stop")==true&&
                            snapshot.value("driver_valid").isBool()&&utc(snapshot.value("observed_utc"))&&snapshot.value("binding").toObject().value("uuid")==Constant&&guard->d_->mayObserveAlreadyOff(Constant))){
                            guard->d_->configuredStopConstant(attempt,{});return;
                        }
                        attempt->coldObservation=snapshot;
                        guard->d_->alreadyOff(attempt);return;
                    }
                    // Validate the complete historical process identity first,
                    // including pins/capability. This is not a cold OFF sample
                    // and cannot settle a physical control request by itself.
                    const auto command=NativeControlCommand::stopObservedSnapshot(guard->d_->endpoint,frame);
                    if(snapshot.value("owned")==false&&snapshot.value("operator_stop")==true&&ended(snapshot)&&guard->d_->mayObserveAlreadyOff(Constant)){
                        guard->d_->alreadyOff(attempt,true);return;
                    }
                    guard->d_->send(attempt,command);
                }catch(const std::exception& e){if(attempt->done)attempt->done(QString::fromUtf8(e.what()).left(400));}
            });
    }
    void stopFinite(const std::shared_ptr<Pending>& attempt){
        const QPointer<NativeApplicationControl> guard(owner);auto* transport=new NativeControlTransport(owner);
        transport->start(NativeControlCommand::finite3060Snapshot(endpoint),timeouts,[guard,attempt,transport](const NativeControlTransportResult& result){
            transport->deleteLater();if(!guard)return;
            try{
                if(result.status!=NativeControlTransportStatus::CompleteResponse||!result.control.finiteObservation)throw std::runtime_error(("Finite STOP inventory unavailable: "+(!result.error.isEmpty()?result.error:result.control.error).left(300)).toStdString());
                need(result.status==NativeControlTransportStatus::CompleteResponse&&result.control.outcome==NativeControlOutcome::ObservationReported&&result.control.finiteObservation.has_value(),"Exact finite-card STOP inventory unavailable; no command sent");
                const auto& observation=*result.control.finiteObservation;
                if(!observation.owned&&!observation.desiredEnabled&&
                    (!observation.manifest||(observation.lastExit&&observation.lastExit->run==observation.manifest->run&&observation.lastExit->exitCode==0))&&guard->d_->mayObserveAlreadyOff(Finite)){
                    attempt->finiteSnapshot=observation.packet;guard->d_->alreadyOff(attempt);return;
                }
                if(observation.manifest){
                    guard->d_->send(attempt,NativeControlCommand::finiteObservedRunStop(guard->d_->endpoint,attempt->id,observation.manifest->run));return;
                }
                // Explicit user/card OFF capability, not a failed observed-run
                // fallback. The SAME writer has admitted this STOP operation.
                // This cancels a possible launch with explicit empty manifest.
                guard->d_->send(attempt,NativeControlCommand::configured3060StopLatch(guard->d_->endpoint,attempt->id,
                    guard->d_->writer.frozen()?Configured3060OffReason::ApplicationClose:Configured3060OffReason::ExplicitCardOff));
            }catch(const std::exception& e){if(attempt->done)attempt->done(QString::fromUtf8(e.what()).left(400));}
        });
    }
    bool submit(const QJsonObject& command){
        requireOwner();const auto action=command.value("action").toString();
        // Conservative lifetime latch, not evidence that a worker started.
        // Even a later timeout/removal cannot make a prior START uncertainly
        // in flight eligible for the read-only STOP optimization.
        if(action=="start_prepared"||action=="resume_prepared"){constantStartSeen=true;return prepared->submit(command);}
        auto packet=reply(command);
        try{
            need(QJsonDocument(command).toJson(QJsonDocument::Compact).size()<=4096,"Control command bound exceeded");
            QSet<QString> actual;for(auto i=command.begin();i!=command.end();++i)actual.insert(i.key());
            need(actual==QSet<QString>{"action","request_id","device"},"Exact native card-control keys required");
            need(command.value("request_id").isString()&&uuid(command.value("request_id").toString())&&command.value("device").isString(),"Canonical native card-control request required");
            need(action=="start"||action=="stop","Only explicit card ON/OFF or prepared starts supported");
            const auto device=command.value("device").toString(),id=command.value("request_id").toString();
            need(device==Constant||device==Finite,"Unqualified card profile refused");
            need(action!="start"||device==Finite,"Productive naked START forbidden: native SQLite prepared admission required");
            need(!consumed.contains(id),"Control request already consumed; acquire fresh identity, no retry");
            need(action!="start"||!finiteGenerationExhausted,"Finite start generation exhausted; unavailable without wrap");
            // This must precede enqueue: a full queue or a queued START must
            // not defer the user's local STOP boundary until remote ACK.
            if(action=="stop"){if(device==Constant)prepared->invalidateQueuedStarts();else invalidateFinite();}
            else finiteStartSeen=true;
            consumed.insert(id);auto attempt=std::make_shared<Pending>();attempt->id=id;attempt->device=device;attempt->action=action;attempt->packet=packet;attempts.insert(id,attempt);
            attempt->generation=finiteGeneration;
            if(action=="stop"&&closeTicket){attempt->closeId=closeTicket->observation.closeId;attempt->inventoryId=closeTicket->observation.inventoryId;attempt->inventoryEpoch=closeTicket->observation.epoch;}
            const QPointer<NativeApplicationControl> guard(owner);
            const auto operation=action=="stop"?NativeApplicationOperation::Stop:NativeApplicationOperation::Mutation;
            const bool queued=writer.enqueue(operation,[guard,attempt](auto done){
                if(!guard){done("Application control unavailable before entry; nothing submitted");return;}
                attempt->done=std::move(done);
                if(guard->d_->cancelled(*attempt)){attempt->done("Queued finite ON invalidated by explicit STOP; nothing sent");return;}
                if(guard->d_->tryInitialAlreadyOff(attempt))return;
                if(attempt->device==Constant)guard->d_->stopConstant(attempt);
                else if(attempt->action=="stop")guard->d_->stopFinite(attempt);
                else guard->d_->send(attempt,NativeControlCommand::finite3060Start(guard->d_->endpoint,attempt->id));
            },[guard,attempt](const NativeApplicationWriteResult& result){
                if(!guard)return;auto packet=attempt->packet;
                if(!result.entered||!result.error.isEmpty()){packet["accepted"]=false;packet["error"]=result.error.left(400);}
                if(guard->d_->cancelled(*attempt))guard->d_->markCancelled(packet);
                guard->d_->attempts.remove(attempt->id);guard->d_->publish(packet);if(!guard)return;
                if(packet.value("desired_off_latched")==true){
                    guard->d_->publish({{"type","status"},{"request_id",attempt->id},{"device",attempt->device},{"detail",packet.value("error")}});
                    if(!guard)return;
                }
                if(!attempt->finiteSnapshot.isEmpty())guard->d_->publish({{"type","device_control_snapshot"},{"snapshot",attempt->finiteSnapshot}});
                if(!guard)return;
                if(!attempt->coldObservation.isEmpty())guard->d_->publish({{"type","native_control_observation"},{"request_id",attempt->id},{"device",attempt->device},{"snapshot",attempt->coldObservation}});
            });
            if(!queued){attempts.remove(id);packet["error"]="ONE writer refused card-control admission; no command sent";publish(packet);}
            return queued;
        }catch(const std::exception& e){packet["error"]=QString::fromUtf8(e.what()).left(400);publish(packet);return false;}
    }
};
NativeApplicationControl::NativeApplicationControl(NativeApplicationWriter& writer,booking::BookingStore& store,SshObservationEndpoint endpoint,NativeControlTimeouts timeouts,int maximumAge,QObject* parent)
    :QObject(parent),d_(std::make_unique<Impl>(this,writer,store,std::move(endpoint),timeouts,maximumAge)){}
NativeApplicationControl::~NativeApplicationControl(){d_->requireOwner();}
bool NativeApplicationControl::rememberStartInventory(const QString& request,const QString& inventory,const SshObservationFrame& frame){d_->requireOwner();return d_->prepared->rememberStartInventory(request,inventory,frame);}
bool NativeApplicationControl::rememberCloseInitial(const NativeCloseInitialObservation& value){return d_->rememberClose(value);}
bool NativeApplicationControl::submit(const QJsonObject& command){return d_->submit(command);}
}

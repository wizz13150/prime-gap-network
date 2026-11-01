#include "mining/native_prepared_control.h"
#include "booking/decimal_range.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>
#include <QUuid>
#include <limits>
#include <stdexcept>

namespace pgn::mining {
namespace {
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool uuid(const QString& s){const auto u=QUuid::fromString(s);return !u.isNull()&&u.toString(QUuid::WithoutBraces)==s;}
QString field(const QJsonObject& object,const char* key){const auto v=object.value(QLatin1String(key));need(v.isString(),"Prepared control requires exact text fields");return v.toString();}
void keys(const QJsonObject& o,const QSet<QString>& expected){QSet<QString> actual;for(auto i=o.begin();i!=o.end();++i)actual.insert(i.key());need(actual==expected,"Prepared control keys differ from native ABI");}
std::optional<NativeObservedBase> commandBase(const QJsonObject& o,bool resume){
    const auto run=o.value(resume?"old_run":"base_run"),birth=o.value("base_birth"),start=o.value("base_start"),end=o.value("base_end");
    if(!resume&&run.isNull()&&birth.isNull()&&start.isNull()&&end.isNull())return {};
    need(run.isString()&&birth.isString()&&start.isString()&&end.isString(),"Observed base must be four strings or four explicit nulls");
    return NativeObservedBase{run.toString(),birth.toString(),start.toString(),end.toString()};
}
bool same(const std::optional<NativeObservedBase>& a,const std::optional<NativeObservedBase>& b){return (!a&&!b)||(a&&b&&a->run==b->run&&a->birth==b->birth&&a->start==b->start&&a->end==b->end);}
QJsonObject response(const QJsonObject& command){
    const auto echo=[&](const char* key)->QJsonValue{const auto value=command.value(QLatin1String(key));return value.isString()&&value.toString().size()<=128?value:QJsonValue(QJsonValue::Null);};
    return {{"type","control"},{"action",echo("action")},{"request_id",echo("request_id")},{"device",echo("device")},{"accepted",false},{"applied",false}};
}
void cancelled(QJsonObject& packet){
    packet["accepted"]=false;packet["cancelled_generation"]=true;packet["physical_outcome"]="UNKNOWN";
    packet["error"]="Start intent invalidated by explicit STOP; receipt retained, fresh observation/reconciliation required";
}
}
NativePreparedControl::NativePreparedControl(NativeApplicationWriter& writer,booking::BookingStore& store,SshObservationEndpoint endpoint,NativeControlTimeouts timeouts,int maximumAge,QObject* parent)
    :QObject(parent),writer_(writer),store_(store),endpoint_(std::move(endpoint)),timeouts_(timeouts),maximumAge_(maximumAge){
    owner();need(writer_.thread()==thread()&&store_.valid(),"Prepared controller requires ONE owning writer and valid SAME SQLite store");
    need(maximumAge>0&&maximumAge<=30000,"Explicit bounded observation lifetime required");
    need(timeouts.startupMs>0&&timeouts.startupMs<=NativeControlTransport::MaximumStartupMs&&timeouts.deadlineMs>=timeouts.startupMs&&timeouts.deadlineMs<=NativeControlTransport::MaximumDeadlineMs,"Prepared transport timeouts invalid");
    makeSshObservationCommand(endpoint_,{}); // direct endpoint validated without a process
}
NativePreparedControl::~NativePreparedControl(){owner();if(pendingDone_)pendingDone_("Prepared controller destroyed; remote outcome unknown, reconcile before retry");}
void NativePreparedControl::owner()const{need(QThread::currentThread()==thread(),"Prepared controller requires owning thread");}
void NativePreparedControl::publish(const QJsonObject& p){if(onPacket)onPacket(p);}
void NativePreparedControl::invalidateQueuedStarts(){
    owner();
    if(ticket_){consumed_.insert(ticket_->request);consumedInventories_.insert(ticket_->inventory);ticket_.reset();}
    age_.invalidate();
    if(generation_==std::numeric_limits<quint64>::max())generationExhausted_=true;
    else ++generation_;
}
bool NativePreparedControl::rememberStartInventory(const QString& request,const QString& inventory,const SshObservationFrame& frame){
    owner();ticket_.reset();
    try{
        need(!generationExhausted_&&!busy_&&!writer_.frozen()&&uuid(request)&&uuid(inventory)&&!consumed_.contains(request)&&!consumedInventories_.contains(inventory),"Fresh start correlation unavailable or already consumed");
        const auto& p=frame.packet;const auto binding=p.value("binding").toObject();
        need(p.value("schema")=="pgn-runtime-snapshot-1"&&p.value("state")=="OFF"&&p.value("owned").isBool()&&!p.value("owned").toBool()&&p.value("driver_valid")==true,"Fresh successful OFF observation required");
        need(binding.value("uuid").isString()&&binding.value("uuid").toString().compare(Device,Qt::CaseInsensitive)==0,"Start inventory device differs");
        const auto observed=p.value("observed_utc").toString();need(observed.endsWith('Z')&&QDateTime::fromString(observed,Qt::ISODateWithMs).isValid(),"Observation UTC missing");
        Ticket ticket{request,inventory,{}};const auto m=p.value("manifest");
        need(m.isNull()||m.isObject(),"Cold manifest must be explicit null, not missing");
        if(m.isObject()){
            const auto object=m.toObject();NativeObservedBase base{field(object,"run"),field(object,"birth"),field(object,"start"),field(object,"end")};
            static const QRegularExpression run("^[0-9a-f]{32}$");
            const auto match=run.match(base.run);
            need(match.hasMatch()&&match.capturedLength()==base.run.size()&&object.value("schema")=="pgn-native-process-1"&&object.value("uuid").toString().compare(Device,Qt::CaseInsensitive)==0,"Observed epoch identity invalid");
            need(base.birth.endsWith('Z')&&QDateTime::fromString(base.birth,Qt::ISODateWithMs).isValid(),"Observed epoch birth invalid");
            need(base.start.size()<=160&&base.end.size()<=160&&booking::canonicalUnsigned(base.start)==base.start&&booking::canonicalUnsigned(base.end)==base.end&&booking::compareUnsigned(base.start,base.end)<0,"Observed epoch range invalid");
            ticket.base=std::move(base);
        }
        ticket_=std::move(ticket);age_.restart();return true;
    }catch(const std::exception&){return false;}
}
bool NativePreparedControl::submit(const QJsonObject& command){
    owner();auto packet=response(command);
    try{
        need(QJsonDocument(command).toJson(QJsonDocument::Compact).size()<=4096,"Prepared command exceeds native4KiB ABI");
        const auto action=field(command,"action");const bool resume=action=="resume_prepared";
        need(resume||action=="start_prepared","Only prepared initial/resume controls supported");
        QSet<QString> expected{"action","request_id","device","observation_id","base_birth","base_start","base_end"};
        if(resume)expected.unite({"booking_id","old_run","new_run"});else expected.insert("base_run");keys(command,expected);
        const auto request=field(command,"request_id"),inventory=field(command,"observation_id"),device=field(command,"device");
        need(uuid(request)&&uuid(inventory)&&device.compare(Device,Qt::CaseInsensitive)==0,"Prepared control correlation/device invalid");
        need(!generationExhausted_&&!busy_&&!writer_.frozen()&&ticket_&&ticket_->request==request&&ticket_->inventory==inventory&&age_.isValid()&&age_.elapsed()<=maximumAge_&&!consumed_.contains(request),"Prepared command has no unused fresh inventory");
        const auto base=commandBase(command,resume);need(same(base,ticket_->base),"Command base differs from caller-owned fresh inventory");
        if(resume)need(base&&uuid(field(command,"booking_id")),"Resume booking/base required");
        const Ticket ticket=*ticket_;const auto generation=generation_;ticket_.reset();consumed_.insert(request);consumedInventories_.insert(inventory);busy_=true;
        auto outcome=std::make_shared<Outcome>();outcome->packet=packet;const QPointer<NativePreparedControl> guard(this);
        const bool queued=writer_.enqueue(NativeApplicationOperation::Mutation,[guard,ticket,command,resume,outcome,generation](auto done){
            if(!guard){done("Prepared controller disappeared before entry; prepared receipt retained");return;}
            guard->pendingDone_=done;
            need(!guard->generationExhausted_&&guard->generation_==generation,"Queued start invalidated by explicit STOP; prepared receipt retained");
            need(guard->age_.isValid()&&guard->age_.elapsed()<=guard->maximumAge_,"Queued prepared observation expired; acquire fresh inventory and reconcile");
            const auto typed=[&]{
                if(!resume){
                    std::optional<booking::InitialStartObservedBase> base;
                    if(ticket.base)base=booking::InitialStartObservedBase{ticket.base->run,ticket.base->birth,ticket.base->start,ticket.base->end};
                    const auto saved=guard->store_.savedInitialStartAdmission(ticket.request,Device,ticket.inventory,base);
                    if(saved.status!=booking::InitialStartAdmissionReadStatus::Present)throw std::runtime_error((QString("Current SAME SQLite initial admission held: ")+saved.error.left(320)).toStdString());
                    return NativeControlCommand::initial(guard->endpoint_,saved.admission,saved.receiptUtf8,ticket.base);
                }
                const auto& b=*ticket.base;
                const auto admission=guard->store_.savedPreparedCheckpointResumeAdmission(ticket.request,Device,field(command,"new_run"),ticket.inventory,{b.run,b.birth,b.start,b.end});
                if(admission.status!=booking::CheckpointResumeAdmissionStatus::Prepared||!admission.epoch)throw std::runtime_error((QString("Current SAME SQLite prepared resume held: ")+admission.error.left(320)).toStdString());
                const auto& e=*admission.epoch;
                need(e.requestUUID==ticket.request&&e.observationUUID==ticket.inventory&&e.bookingId==field(command,"booking_id")&&e.previousRun==field(command,"old_run"),"Resume command differs from immutable saved epoch");
                booking::CheckpointResumeEpochRead saved;saved.status=booking::CheckpointResumeReadStatus::Present;saved.epoch=e;saved.receiptUtf8=admission.receiptUtf8;
                return NativeControlCommand::resume(guard->endpoint_,admission,saved,*ticket.base);
            }();
            // Typed SQL validation/encoding can itself wait for locks or scan
            // history. Its cost does not renew the original observation age.
            need(!guard->generationExhausted_&&guard->generation_==generation,"Start invalidated during SQL admission; receipt retained, no command sent");
            need(guard->age_.isValid()&&guard->age_.elapsed()<=guard->maximumAge_,"Prepared observation expired during admission; no command sent, reconcile with fresh inventory");
            auto owned=std::make_unique<NativeControlTransport>(guard);auto* transport=owned.get();
            transport->start(typed,guard->timeouts_,[guard,transport,outcome,done](const auto& result){
                transport->deleteLater();if(!guard){done("Prepared owner lost; remote outcome unknown");return;}
                const bool reported=result.status==NativeControlTransportStatus::CompleteResponse&&(result.control.outcome==NativeControlOutcome::RequestReported||result.control.outcome==NativeControlOutcome::AlreadyRunningReported);
                outcome->packet["accepted"]=reported;
                if(reported){outcome->packet["result"]=result.control.packet;outcome->packet["reported_outcome"]=result.control.outcome==NativeControlOutcome::RequestReported?"RequestReported":"AlreadyRunningReported";}
                else outcome->packet["error"]=(!result.error.isEmpty()?result.error:!result.control.error.isEmpty()?result.control.error:QString("Remote control not confirmed; acquire fresh observation before retry")).left(400);
                // RequestReported is NOT physical application. The bridge still
                // waits for a fresh matching process/source snapshot.
                guard->pendingDone_={};done({});
            });
            owned.release();
        },[guard,outcome,generation](const auto& result){
            if(!guard)return;guard->pendingDone_={};guard->busy_=false;
            if(!result.error.isEmpty()){outcome->packet["accepted"]=false;outcome->packet["error"]=result.error;}
            if(guard->generationExhausted_||guard->generation_!=generation)cancelled(outcome->packet);
            guard->publish(outcome->packet);
        });
        if(!queued){busy_=false;need(false,"Application writer refused prepared control; receipt retained, no retry");}
        return true;
    }catch(const std::exception& e){packet["error"]=QString::fromUtf8(e.what()).left(400);publish(packet);return false;}
}
}

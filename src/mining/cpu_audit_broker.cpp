#include "mining/cpu_audit_broker.h"
#include <QPointer>
namespace pgn::mining {
CpuAuditBroker::CpuAuditBroker(QString file,QString genesis,QString address,CpuFleet* fleet,Services services,QObject* parent):
    QObject(parent),fleet_(fleet),services_(std::move(services)),journal_(file),publisher_(file+".publication",genesis,services_.rpc,services_.sign,this),genesis_(std::move(genesis)),address_(std::move(address)){
    clock_.start();timer_.setInterval(2000);connect(&timer_,&QTimer::timeout,this,[this]{pulse();});timer_.start();
}
void CpuAuditBroker::note(QString status){if(status_==status)return;status_=std::move(status);if(onStatus)onStatus(status_);}
void CpuAuditBroker::pulse(){
    if(stopping_||!fleet_||busy_||publisher_.busy())return;
    if(fleet_->enabledDevices().isEmpty()&&!journal_.exists()&&!publisher_.pending())return;
    const auto anchor=services_.observation();if(!anchor||!anchor->freshAt(QDateTime::currentDateTimeUtc())||anchor->genesis!=genesis_){note("Waiting for verified Core audit state");return;}
    if(anchor->synchronizing.value_or(true)||anchor->peers.value_or(0)<1){note("Waiting for Core synchronization before requesting or publishing audits");return;}
    if(!journal_.load()){note(journal_.error());return;}
    const QPointer<CpuAuditBroker> guard(this);
    if(actor_.isEmpty()||!actorEpoch_.matches(anchor->owner)){
        if(fleet_->enabledDevices().isEmpty()&&journal_.entries().isEmpty()&&!publisher_.pending())return;
        busy_=true;const auto epoch=anchor->owner;
        services_.identity(address_,[guard,epoch](QJsonValue value,QString error){
            if(!guard)return;guard->busy_=false;const auto current=guard->services_.observation();const auto identity=value.toObject();
            if(!error.isEmpty()||!current||!current->owner.matches(epoch)||identity.value("ismine")!=true||identity.value("scriptPubKey").toString().isEmpty()){guard->note("Owned wallet audit signer unavailable");return;}
            guard->actor_=identity.value("scriptPubKey").toString();guard->actorEpoch_=epoch;guard->pulse();
        });return;
    }
    if(publisher_.pending()){busy_=true;publisher_.recover([guard](auto receipt,auto error){if(guard)guard->published(receipt,error);});return;}
    const auto entries=journal_.entries();const auto enabled=fleet_->enabledDevices();++round_;
    if(!entries.isEmpty()&&(round_%2==0||enabled.isEmpty())){
        const auto keys=entries.keys();const auto id=keys[int((round_/2)%quint64(keys.size()))];maintain(id,entries.value(id).toObject());return;
    }
    if(clock_.elapsed()<nextAssignment_)return;
    if(!journal_.canRequestAssignment()){note("CPU audit outbox is full; completing retained work before requesting another assignment");return;}
    QString free;
    for(const auto& device:enabled){bool assigned=false;for(const auto& entry:entries)assigned=assigned||(entry.toObject().value("device")==device&&!entry.toObject().contains("answer"));if(!assigned&&fleet_->status(device).value("request_sha256").toString().isEmpty()){free=device;break;}}
    if(free.isEmpty())return;
    busy_=true;nextAssignment_=clock_.elapsed()+10000;note("Requesting a compatible network audit");
    publisher_.publish({"assign",address_},[guard](auto receipt,auto error){if(guard)guard->published(receipt,error);},cpuAuditProposalSupported);
}
void CpuAuditBroker::published(QJsonObject receipt,QString error){
    if(stopping_||!fleet_)return;
    if(!error.isEmpty()){busy_=false;note(error);return;}
    if(receipt.value("action")=="assign"){adopt(receipt);return;}
    const auto proposal=receipt.value("proposal").toObject();QString id;
    const auto entries=journal_.entries();for(auto i=entries.begin();i!=entries.end();++i){const auto row=i.value().toObject();if(row.value("request")==proposal.value("request_id")&&row.value("slot")==proposal.value("slot")&&row.value("attempt")==proposal.value("attempt")){id=i.key();break;}}
    if(id.isEmpty()||!journal_.recordPublication(id,receipt.value("action").toString(),receipt.value("block_hash").toString())){busy_=false;note("Confirmed audit publication retained until its private context is saved");return;}
    if(!publisher_.acknowledge(receipt.value("block_hash").toString())){busy_=false;note("Audit confirmation retained for acknowledgement retry");return;}
    const auto row=journal_.entry(id);if(receipt.value("action")=="commit")fleet_->acknowledgeAudit(row.value("device").toString(),row.value("request_sha256").toString());
    busy_=false;note("Audit event confirmed; independent verification remains separate");
}
void CpuAuditBroker::adopt(QJsonObject receipt){
    const auto proposal=receipt.value("proposal").toObject();const QPointer<CpuAuditBroker> guard(this);
    services_.details(proposal.value("request_id").toString(),[guard,receipt,proposal](QJsonObject snapshot,QString error){
        if(!guard)return;const auto anchor=guard->services_.observation();
        if(!error.isEmpty()||!anchor||snapshot.value("bestblockhash")!=anchor->tipHash||snapshot.value("reports").toArray().size()!=1||snapshot.value("audits").toArray().size()!=1||snapshot.value("leases").toArray().size()!=1){guard->busy_=false;guard->note("Waiting for a coherent confirmed audit snapshot");return;}
        QJsonObject job;for(const auto& value:snapshot.value("audits").toArray().first().toObject().value("jobs").toArray()){const auto item=value.toObject();if(item.value("slot")==proposal.value("slot")&&item.value("attempt")==proposal.value("attempt"))job=item;}
        const auto work=cpuAuditWork(*anchor,guard->genesis_,snapshot.value("reports").toArray().first().toObject(),snapshot.value("leases").toArray().first().toObject(),job,guard->actor_,QDateTime::currentDateTimeUtc());
        if(!work.work){guard->busy_=false;guard->note(work.error);return;}
        QString device;const auto entries=guard->journal_.entries();
        const auto existing=entries.value(CpuAuditJournal::key(*work.work)).toObject();
        if(!existing.isEmpty())device=existing.value("device").toString();
        else for(const auto& candidate:guard->fleet_->enabledDevices()){bool taken=false;for(const auto& value:entries)taken=taken||(value.toObject().value("device")==candidate&&!value.toObject().contains("answer"));if(!taken){device=candidate;break;}}
        if(device.isEmpty()){guard->busy_=false;guard->note("Confirmed audit retained until a CPU is enabled");return;}
        if(!guard->journal_.retainAssignment(device,guard->address_,*work.work)||!guard->journal_.recordPublication(CpuAuditJournal::key(*work.work),"assignment",receipt.value("block_hash").toString())){guard->busy_=false;guard->note(guard->journal_.error());return;}
        if(!guard->publisher_.acknowledge(receipt.value("block_hash").toString())){guard->busy_=false;guard->note("Audit assignment confirmation retained");return;}
        if(!guard->fleet_->attachAudit(device,*work.work))guard->note("CPU assignment retained; backend adoption is waiting");else guard->note("Confirmed audit adopted by CPU controller");
        guard->busy_=false;
    });
}
std::optional<CpuAuditWork> CpuAuditBroker::context(const QJsonObject& row,const QJsonObject& snapshot,QString& error)const{
    const auto anchor=services_.observation();if(!anchor||!anchor->freshAt(QDateTime::currentDateTimeUtc())||snapshot.value("bestblockhash")!=anchor->tipHash||snapshot.value("genesis")!=genesis_||snapshot.value("reports").toArray().size()!=1||snapshot.value("audits").toArray().size()!=1||(!row.contains("answer")&&snapshot.value("leases").toArray().size()!=1)){error="Waiting for a coherent audit snapshot";return {};}
    const auto report=snapshot.value("reports").toArray().first().toObject();if(report.value("request_id")!=row.value("request")||report.value("report_root")!=row.value("report_root")||report.value("ticket")!=row.value("service_request").toObject().value("ticket_id")){error="Audit report changed branch; local work retained";return {};}
    QJsonObject job;for(const auto& value:snapshot.value("audits").toArray().first().toObject().value("jobs").toArray()){const auto item=value.toObject();if(item.value("slot")==row.value("slot")&&item.value("attempt")==row.value("attempt"))job=item;}
    if(job.value("actor")!=row.value("actor")||job.value("ranges")!=row.value("service_request").toObject().value("partitions")){error="Audit assignment changed; local work retained";return {};}
    if(!row.contains("answer")){const auto result=cpuAuditWork(*anchor,genesis_,report,snapshot.value("leases").toArray().first().toObject(),job,row.value("actor").toString(),QDateTime::currentDateTimeUtc());error=result.error;return result.work;}
    CpuAuditWork work;work.core=anchor->owner;work.genesis=genesis_;work.tip=anchor->tipHash;work.requestId=row.value("request").toString();work.reportRoot=row.value("report_root").toString();work.actor=row.value("actor").toString();work.slot=row.value("slot").toInt();work.attempt=quint32(row.value("attempt").toInteger());work.requestSha256=row.value("request_sha256").toString();work.serviceRequest=row.value("service_request").toObject();return work;
}
void CpuAuditBroker::maintain(const QString& id,QJsonObject row){
    busy_=true;const QPointer<CpuAuditBroker> guard(this);
    const auto reader=row.contains("answer")&&services_.auditDetails?services_.auditDetails:services_.details;
    reader(row.value("request").toString(),[guard,id,row](QJsonObject snapshot,QString error){
        if(!guard)return;if(!error.isEmpty()){guard->busy_=false;guard->note(error);return;}
        const auto work=guard->context(row,snapshot,error);if(!work){guard->busy_=false;guard->note(error);return;}
        if(!row.contains("answer")){
            guard->fleet_->attachAudit(row.value("device").toString(),*work);guard->busy_=false;return;
        }
        // After a GUI restart the native service can still retain this closed
        // result. Rebind only that exact already-closed request, never reopen
        // a disclosed audit for computation.
        const auto device=row.value("device").toString();const auto native=guard->fleet_->status(device);
        if(native.value("result_ready")==true&&native.value("request_sha256")==work->requestSha256)guard->fleet_->attachAudit(device,*work);
        const auto audit=snapshot.value("audits").toArray().first().toObject();QJsonObject job;for(const auto& value:audit.value("jobs").toArray())if(value.toObject().value("slot")==row.value("slot"))job=value.toObject();
        if(job.value("state")=="failed"){guard->busy_=false;guard->note("Audit mismatch: original computation retained for investigation");return;}
        QString action,body;
        if(job.value("state")=="assigned"){action="commit";body=row.value("commitment").toString();}
        else if(job.value("state")=="committed"&&audit.contains("details_root")){action="reveal";body=QString::fromLatin1(QJsonDocument(row.value("answer").toObject()).toJson(QJsonDocument::Compact).toHex());}
        if(action.isEmpty()){guard->busy_=false;guard->note(job.value("state")=="passed"?"CPU audit passed; terminal context retained":"Audit commitment retained; waiting for independent disclosure");return;}
        guard->publisher_.publish({action,row.value("address").toString(),work->requestId,body,work->slot,qint64(work->attempt)},[guard](auto receipt,auto error){if(guard)guard->published(receipt,error);});
    });
}
void CpuAuditBroker::computed(const QString& device,const CpuAuditWork& work,const QJsonObject& result){
    if(stopping_||!fleet_)return;
    if(!journal_.retainComputation(work,result)){note(journal_.error());return;}
    fleet_->acknowledgeAudit(device,work.requestSha256);
    note("Complete CPU audit retained before network commitment");QTimer::singleShot(0,this,[this]{pulse();});
}
}

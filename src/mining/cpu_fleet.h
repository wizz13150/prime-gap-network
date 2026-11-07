#pragma once
#include "mining/cpu_controller_client.h"
#include "mining/cpu_audit_work.h"
#include <QHash>
#include <QSettings>
#include <memory>
#include <optional>
#include <algorithm>
#include <QtMath>
namespace pgn::mining {
// A device has one mutation in flight. A later user choice replaces only the
// desired budget, and is sent after the current backend acknowledgement.
class CpuFleet final:public QObject {
    struct Desired {QString request;bool enabled{};int workers{};};
    struct Inflight {Desired value;qint64 generation{};};
    struct Audit {CpuAuditWork work;bool openSent{},announced{},resultRequested{},delivered{},clearing{},failed{},validation{};};
    struct Device {CpuControllerClient* client{};CpuControlEndpoint endpoint;QJsonObject inventory,status;std::optional<Desired> desired;std::optional<Inflight> inflight;std::optional<Audit> audit;bool userEnabled{};};
    QHash<QString,std::shared_ptr<Device>> devices_;bool closing_{};
    void dispatch(const std::shared_ptr<Device>& device){
        if(closing_||device->inflight||!device->desired||!device->client->available())return;
        const auto value=*device->desired;
        // Store before send; responses are delivered asynchronously by Qt.
        device->inflight=Inflight{value,0};
        const auto generation=device->client->configure(value.enabled,value.workers);
        if(device->inflight)device->inflight->generation=generation;
    }
    void notifyClosed(){if(closing_&&allStopped()&&onCloseReady)onCloseReady();}
    void auditPulse(const std::shared_ptr<Device>& device){
        if(closing_||!device->audit||device->audit->failed||!device->client->available())return;
        auto& audit=*device->audit;const auto digest=device->status.value("request_sha256").toString();
        if(audit.clearing&&digest.isEmpty()){
            device->audit.reset();
            if(device->userEnabled&&!device->desired&&!device->inflight){
                device->desired=Desired{{},true,device->status.value("requested_threads").toInt(1)};dispatch(device);
            }
            return;
        }
        if(digest.isEmpty()){
            if(!audit.openSent){audit.openSent=true;if(!device->client->openAudit(audit.work.serviceRequest))audit.openSent=false;}
            return;
        }
        if(digest!=audit.work.requestSha256){
            audit.failed=true;if(onAuditError)onAuditError(device->endpoint.deviceId,"CPU retains a different assignment; no overwrite or implicit discard");return;
        }
        if(!audit.announced){audit.announced=true;if(!audit.validation&&onAuditAttached)onAuditAttached(device->endpoint.deviceId,audit.work);}
        if(device->status.value("result_ready")==true&&!audit.resultRequested&&!audit.delivered&&!audit.clearing){
            audit.resultRequested=true;if(!device->client->requestAuditResult(audit.work.requestSha256))audit.resultRequested=false;
        }
    }
public:
    std::function<void(QString,QJsonObject,QJsonObject,bool)> onTelemetry;
    std::function<void(QString,QString,bool,QString)> onControl;
    std::function<void()> onCloseReady;
    std::function<void(QString,CpuAuditWork)> onAuditAttached;
    std::function<void(QString,CpuAuditWork,QJsonObject)> onAuditComputation;
    std::function<void(QString,CpuAuditWork,QJsonObject)> onValidationComputation;
    std::function<void(QString,QString)> onAuditError;
    explicit CpuFleet(QObject* parent=nullptr):QObject(parent){}
    void add(CpuControlEndpoint endpoint){
        if(endpoint.deviceId.isEmpty()||devices_.contains(endpoint.deviceId))return;
        const auto saved=QSettings().value("mining/cpuThreads/"+endpoint.deviceId);
        if(saved.isValid()&&!endpoint.arguments.contains("--initial-threads"))endpoint.arguments.append(QStringList{"--initial-threads",QString::number(std::clamp(saved.toInt(),1,256))});
        auto device=std::make_shared<Device>();device->endpoint=endpoint;device->client=new CpuControllerClient(endpoint,this);devices_.insert(endpoint.deviceId,device);
        device->client->onInventory=[this,device](QJsonObject inventory){
            if(inventory.value("host").toString().compare(device->endpoint.host,Qt::CaseInsensitive)!=0){if(onControl&&device->desired)onControl(device->endpoint.deviceId,device->desired->request,false,"CPU host identity differs from its configured endpoint");return;}
            device->inventory=inventory;
            if(onTelemetry)onTelemetry(device->endpoint.deviceId,inventory,device->status,true);
        };
        device->client->onStatus=[this,device](QJsonObject status){
            device->status=status;
            auto presented=status;if(device->audit&&device->audit->validation)presented.insert("validation_only",true);
            if(onTelemetry&&!device->inventory.isEmpty())onTelemetry(device->endpoint.deviceId,device->inventory,presented,device->client->available());
            if(device->inflight){
                const auto sent=*device->inflight;const auto state=status.value("state").toString();
                const bool stable=state!="STOPPING"&&state!="RECONFIGURING";
                if(stable&&status.value("generation").toInteger()>=sent.generation){
                    const bool enabled=status.value("enabled").toBool();
                    const bool matches=enabled==sent.value.enabled&&status.value("requested_threads").toInt()==sent.value.workers&&
                        (enabled||(status.value("threads_active").toInt()==0&&status.value("worker_pids").toArray().isEmpty()));
                    if(matches){
                        QSettings().setValue("mining/cpuThreads/"+device->endpoint.deviceId,sent.value.workers);
                        const bool latest=device->desired&&device->desired->request==sent.value.request;
                        if(latest)device->desired.reset();
                        device->inflight.reset();
                        if(latest&&!sent.value.request.isEmpty()&&onControl)onControl(device->endpoint.deviceId,sent.value.request,true,state);
                        dispatch(device);
                    }else if(!status.value("error").isNull()||status.value("budget_refused").toBool()){
                        if(device->desired&&!device->desired->request.isEmpty()&&onControl)onControl(device->endpoint.deviceId,device->desired->request,false,status.value("error").toString("CPU budget was refused"));
                        device->desired.reset();device->inflight.reset();
                    }
                }
            }
            auditPulse(device);
        };
        device->client->onAuditResult=[this,device](QJsonObject result){
            if(!device->audit||device->audit->failed||device->audit->delivered||result.value("request_sha256")!=device->audit->work.requestSha256)return;
            device->audit->delivered=true;
            if(device->audit->validation){if(onValidationComputation)onValidationComputation(device->endpoint.deviceId,device->audit->work,result);}
            else if(onAuditComputation)onAuditComputation(device->endpoint.deviceId,device->audit->work,result);
        };
        device->client->onError=[this,device](QString error){
            if(device->audit&&(device->audit->resultRequested||!device->audit->announced)){
                device->audit->failed=true;if(onAuditError)onAuditError(device->endpoint.deviceId,error);
            }
            if(device->desired&&onControl)onControl(device->endpoint.deviceId,device->desired->request,false,error);
            device->desired.reset();device->inflight.reset();
            auto& status=device->status;status.insert("error",error);
            status.insert("controller_connected",false);
            if(status.value("state")!="UNAVAILABLE")status.insert("last_confirmed_state",status.value("state"));
            status.insert("state","UNAVAILABLE");
            if(onTelemetry&&!device->inventory.isEmpty())onTelemetry(device->endpoint.deviceId,device->inventory,status,false);
            notifyClosed();
        };
        device->client->onClosed=[this]{notifyClosed();};device->client->start();
    }
    bool control(const QString& id,const QString& request,bool enabled,int percent){
        const auto device=devices_.value(id);if(!device)return false;
        const int maximum=device->inventory.value("maximum_workers").toInt();
        if(closing_||maximum<1||!device->client->available()){if(onControl)onControl(id,request,false,"CPU controller is not available");return true;}
        const int workers=std::clamp(qRound(maximum*percent/100.),1,maximum);
        device->userEnabled=enabled;
        device->desired=Desired{request,enabled,workers};dispatch(device);return true;
    }
    QStringList enabledDevices()const{QStringList result;for(auto i=devices_.cbegin();i!=devices_.cend();++i)if(i.value()->userEnabled&&i.value()->status.value("enabled")==true&&i.value()->client->available())result.append(i.key());result.sort();return result;}
    QJsonObject status(const QString& id)const{const auto device=devices_.value(id);return device?device->status:QJsonObject{};}
    bool attachAudit(const QString& id,const CpuAuditWork& work){return attach(id,work,false);}
    bool attachValidation(const QString& id,const CpuAuditWork& work){return attach(id,work,true);}
private:
    bool attach(const QString& id,const CpuAuditWork& work,bool validation){
        const auto device=devices_.value(id);if(closing_||!device||!device->client->available()||work.requestSha256.isEmpty())return false;
        if(device->audit){const auto& existing=*device->audit;return !existing.failed&&existing.validation==validation&&existing.work.requestSha256==work.requestSha256&&existing.work.requestId==work.requestId&&existing.work.slot==work.slot&&existing.work.attempt==work.attempt;}
        const auto digest=device->status.value("request_sha256").toString();if(!digest.isEmpty()&&digest!=work.requestSha256)return false;
        device->audit=Audit{work};device->audit->validation=validation;auditPulse(device);return !device->audit->failed;
    }
public:
    bool acknowledgeAudit(const QString& id,const QString& digest){
        const auto device=devices_.value(id);if(!device||!device->audit||!device->audit->delivered||device->audit->work.requestSha256!=digest||closing_)return false;
        device->audit->clearing=true;
        if(!device->client->acknowledgeAuditResult(digest)){device->audit->clearing=false;return false;}
        return true;
    }
    bool allStopped()const{for(const auto& device:devices_)if(!device->client->stopped())return false;return true;}
    bool requestClose(){
        // A stopped process may still have a reconnect timer. Close every
        // client before reporting the fleet drained, including inactive ones.
        if(!closing_){closing_=true;for(const auto& device:devices_)device->client->close();}
        return allStopped();
    }
};
}

#pragma once
#include "mining/cpu_controller_client.h"
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QtMath>
#include <algorithm>
#include <memory>
#include <optional>
namespace pgn::mining {
// GUI owns each exact local/SSH CPU endpoint; the logical miner supplies work.
class CpuNormalResources final:public QObject {
    struct Bridge {QPointer<QProcess> miner;QString identity;};
    struct Choice {QString request;bool enabled{};int percent{};};
    struct Resource {CpuControlEndpoint endpoint;CpuControllerClient* client{};QJsonObject inventory,status;QHash<qint64,Bridge> pending;QString control,reason,reasonCode;bool selected{},compatible{};int desiredThreads{1};std::optional<Choice> queued;};
    QHash<QString,std::shared_ptr<Resource>> resources_;QPointer<QProcess> miner_;bool closing_{},restored_{};
    QProcess* cpuMiner_{};QString cpuProgram_;QStringList cpuArguments_;QByteArray cpuBuffer_;
    bool launchCpuMiner(){
        if(closing_||cpuProgram_.isEmpty())return false;
        if(!cpuMiner_){
            cpuMiner_=new QProcess(this);
            connect(cpuMiner_,&QProcess::started,this,[this]{miner_=cpuMiner_;restored_=false;for(const auto& r:resources_)settled(r);});
            connect(cpuMiner_,&QProcess::readyReadStandardOutput,this,[this]{
                cpuBuffer_.append(cpuMiner_->readAllStandardOutput());
                if(cpuBuffer_.size()>4194304){cpuBuffer_.clear();cpuMiner_->write("{\"action\":\"quit\"}\n");return;}
                while(cpuBuffer_.contains('\n')){
                    const auto end=cpuBuffer_.indexOf('\n');const auto line=cpuBuffer_.left(end);cpuBuffer_.remove(0,end+1);
                    const auto frame=QJsonDocument::fromJson(line).object();
                    if(!packet(cpuMiner_,frame)&&onNormalFrame)onNormalFrame(frame);
                }
            });
            connect(cpuMiner_,&QProcess::readyReadStandardError,this,[this]{const auto error=cpuMiner_->readAllStandardError();if(!error.isEmpty()&&onNormalFrame)onNormalFrame({{"schema","PGN_HORIZON_MINING_VIEW_UNAVAILABLE_V1"},{"phase","Needs attention"},{"message",QString::fromUtf8(error).right(400)}});});
            connect(cpuMiner_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
                if(error==QProcess::FailedToStart){for(const auto& r:resources_)if(r->queued){const auto choice=*r->queued;r->queued.reset();refuse(r->endpoint.deviceId,choice.request,"CPU normal miner could not start: "+cpuMiner_->errorString());}}
            });
            connect(cpuMiner_,&QProcess::finished,this,[this](int,QProcess::ExitStatus){disconnected(cpuMiner_);if(closing_)for(const auto& r:resources_)r->client->close();if(closing_&&allStopped()&&onCloseReady)onCloseReady();});
        }
        if(cpuMiner_->state()!=QProcess::NotRunning)return true;
        cpuBuffer_.clear();cpuMiner_->setProgram(cpuProgram_);cpuMiner_->setArguments(cpuArguments_);cpuMiner_->start();return true;
    }
    void present(const std::shared_ptr<Resource>& r){if(onTelemetry&&!r->inventory.isEmpty())onTelemetry(r->endpoint.deviceId,r->inventory,r->status,r->compatible&&r->client->available());}
    static void answer(QProcess* miner,const QString& device,const QString& identity,const QJsonObject& value){
        if(!miner||miner->state()!=QProcess::Running)return;
        const QJsonObject reply{{"action","cpu_reply"},{"device_id",device},{"bridge_id",identity},{"response",value}};
        miner->write(QJsonDocument(reply).toJson(QJsonDocument::Compact)+'\n');
    }
    bool refuse(const QString& device,const QString& request,const QString& reason){if(onControl)onControl(device,request,false,reason);return true;}
    void settled(const std::shared_ptr<Resource>& r){if(r->queued){const auto choice=*r->queued;r->queued.reset();control(r->endpoint.deviceId,choice.request,choice.enabled,choice.percent);}}
public:
    using QObject::QObject;
    std::function<void(QString,QJsonObject,QJsonObject,bool)> onTelemetry;
    std::function<void(QString,QString,bool,QString)> onControl;
    std::function<void()> onCloseReady;
    std::function<void(QJsonObject)> onNormalFrame;
    bool configureNormalMiner(const QJsonObject& profile){
        if(profile.value("schema")!="PGN_LOCAL_CPU_NORMAL_PROFILE_V1"||profile.value("local")!=true)return false;
        const auto program=profile.value("program").toString();QStringList arguments;
        for(const auto& item:profile.value("network_arguments").toArray()){if(!item.isString())return false;arguments.append(item.toString());}
        if(program.isEmpty()||arguments.size()>64||!arguments.contains("--network")||!arguments.contains("--cpu-only")||arguments.contains("--gpu")||arguments.contains("--architecture")||arguments.contains("--import-work")||arguments.contains("--legacy-lease"))return false;
        if(program==cpuProgram_&&arguments==cpuArguments_)return true;
        if(cpuMiner_&&cpuMiner_->state()!=QProcess::NotRunning)return false;
        for(const auto& r:resources_)if(r->selected)return false;
        cpuProgram_=program;cpuArguments_=arguments;return true;
    }
    void add(CpuControlEndpoint endpoint){
        if(endpoint.deviceId.isEmpty()||resources_.contains(endpoint.deviceId))return;
        // Retain old checkpoints byte-for-byte; this ABI owns a fresh workspace.
        for(int i=0;i+1<endpoint.arguments.size();++i)if(endpoint.arguments[i]=="--workspace"&&!endpoint.arguments[i+1].endsWith("/normal-cycle-v2"))endpoint.arguments[i+1]+="/normal-cycle-v2";
        const QRegularExpression workspace("(--workspace +)([A-Za-z0-9_./:-]+)");
        for(auto& argument:endpoint.arguments){const auto match=workspace.match(argument);if(match.hasMatch()&&!match.captured(2).endsWith("/normal-cycle-v2"))argument.replace(match.capturedStart(2),match.capturedLength(2),match.captured(2)+"/normal-cycle-v2");}
        const auto saved=QSettings().value("mining/cpuThreads/"+endpoint.deviceId);
        if(saved.isValid()&&!endpoint.arguments.contains("--initial-threads"))endpoint.arguments.append(QStringList{"--initial-threads",QString::number(std::clamp(saved.toInt(),1,256))});
        auto r=std::make_shared<Resource>();r->endpoint=endpoint;r->client=new CpuControllerClient(endpoint,this);resources_.insert(endpoint.deviceId,r);
        r->client->onInventory=[this,r](QJsonObject inventory){
            r->inventory=inventory;
            const auto expected="CPU/"+inventory.value("host").toString().toUpper()+"/"+QString::number(inventory.value("processor_group").toInt());
            if(expected!=r->endpoint.deviceId||inventory.value("host").toString().compare(r->endpoint.host,Qt::CaseInsensitive)!=0){r->status.insert("error","CPU endpoint host/device identity mismatch");present(r);return;}
            if(inventory.value("control_version")!=2||inventory.value("normal_cycle_executor")!=true||inventory.value("standalone_audit_allocation")!=false){r->status.insert("error","CPU executor ABI requires normal-cycle version2; old audit-only runtime is inactive");r->status.insert("state","UNAVAILABLE");present(r);return;}
            r->compatible=true;present(r);
        };
        r->client->onStatus=[this,r](QJsonObject status){if(status.value("compute_started")==true){r->reason.clear();r->reasonCode.clear();}r->status=status;r->status.insert("normal_cycle_mode","qualification");r->status.insert("resource_selected",r->selected);r->status.insert("qualification_reason",r->reason);r->status.insert("qualification_reason_code",r->reasonCode);present(r);};
        r->client->onReply=[this,r](qint64 id,QJsonObject response){
            const auto found=r->pending.find(id);if(found==r->pending.end())return;const auto bridge=*found;r->pending.erase(found);
            if(!bridge.miner){if(response.value("ok")==true)r->selected=false;r->control.clear();if(onControl)onControl(r->endpoint.deviceId,bridge.identity,response.value("ok").toBool(),"CPU OFF boundary acknowledged");settled(r);return;}
            answer(bridge.miner,r->endpoint.deviceId,bridge.identity,response);
        };
        r->client->onError=[this,r](QString error){
            r->status.insert("error",error);r->status.insert("state","UNAVAILABLE");
            for(const auto& bridge:r->pending)answer(bridge.miner,r->endpoint.deviceId,bridge.identity,{{"ok",false},{"error",QJsonObject{{"message",error}}}});
            r->pending.clear();
            if(!r->control.isEmpty()&&onControl)onControl(r->endpoint.deviceId,r->control,false,error);r->control.clear();present(r);
            if(r->queued){if(onControl)onControl(r->endpoint.deviceId,r->queued->request,false,error);r->queued.reset();}
        };
        r->client->onClosed=[this]{if(closing_&&allStopped()&&onCloseReady)onCloseReady();};r->client->start();
    }
    void attachMiner(QProcess* miner){if(!miner_||miner_->state()!=QProcess::Running||miner_==miner){miner_=miner;restored_=false;}}
    void minerReady(QProcess* miner){
        if(miner_!=miner||restored_||miner->state()!=QProcess::Running)return;restored_=true;
        for(const auto& r:resources_)if(r->selected&&r->compatible&&r->client->available()&&r->control.isEmpty()){
            const auto affinity=r->client->affinityFor(r->desiredThreads);if(affinity.size()!=r->desiredThreads)continue;
            r->control="restore-resource:"+r->endpoint.deviceId;
            const QJsonObject command{{"action","cpu_configure"},{"id",r->control},{"device_id",r->endpoint.deviceId},{"inventory",r->inventory},{"enabled",true},{"threads",r->desiredThreads},{"affinity",affinity}};
            miner->write(QJsonDocument(command).toJson(QJsonDocument::Compact)+'\n');
        }
    }
    bool control(const QString& device,const QString& request,bool enabled,int percent){
        const auto r=resources_.value(device);if(!r)return device.startsWith("CPU/")?refuse(device,request,"CPU executor is unavailable"):false;
        if(closing_||!r->compatible||!r->client->available()||r->inventory.isEmpty())return refuse(device,request,"CPU normal-cycle controller is unavailable or requires version2");
        if(cpuMiner_&&cpuMiner_->state()==QProcess::Starting){r->queued=Choice{request,enabled,percent};return true;}
        if(!r->control.isEmpty()){r->queued=Choice{request,enabled,percent};return true;}
        const int maximum=r->inventory.value("maximum_workers").toInt();if(maximum<1)return refuse(device,request,"CPU thread budget is unavailable");
        const int workers=std::clamp(qRound(maximum*percent/100.),1,maximum);const auto affinity=r->client->affinityFor(workers);
        if(affinity.size()!=workers)return refuse(device,request,"CPU fixed affinity budget is unavailable");
        r->desiredThreads=workers;
        QSettings().setValue("mining/cpuThreads/"+device,workers);
        if(enabled&&!cpuProgram_.isEmpty()&&(!cpuMiner_||miner_!=cpuMiner_||cpuMiner_->state()!=QProcess::Running)){
            r->queued=Choice{request,enabled,percent};if(launchCpuMiner())return true;r->queued.reset();
            return refuse(device,request,"CPU normal miner is not ready");
        }
        if(!miner_||miner_->state()!=QProcess::Running){
            if(enabled)return refuse(device,request,"CPU inactive: connect the normal-ticket miner; CPU-only coverage and fixed24h capacity remain unqualified");
            r->selected=false;r->control=request;
            const auto id=r->client->ownedCommand("configure",{{"enabled",false},{"threads",workers},{"affinity",affinity},{"generation",0}});
            if(id<1){r->control.clear();return refuse(device,request,"CPU OFF could not reach its owned controller");}
            r->pending.insert(id,{nullptr,request});return true;
        }
        const QJsonObject command{{"action","cpu_configure"},{"id",request},{"device_id",device},{"inventory",r->inventory},{"enabled",enabled},{"threads",workers},{"affinity",affinity}};
        r->control=request;
        if(miner_->write(QJsonDocument(command).toJson(QJsonDocument::Compact)+'\n')<0){r->control.clear();return refuse(device,request,"Normal miner pipe is unavailable");}
        QSettings().setValue("mining/cpuThreads/"+device,workers);return true;
    }
    bool packet(QProcess* miner,const QJsonObject& packet){
        const auto schema=packet.value("schema").toString();
        if(schema!="PGN_HORIZON_CPU_COMMAND_V1"&&schema!="PGN_HORIZON_CPU_NORMAL_EVENT_V1")return false;
        const auto device=packet.value("device_id").toString();const auto r=resources_.value(device);
        if(schema=="PGN_HORIZON_CPU_COMMAND_V1"){
            const auto identity=packet.value("bridge_id").toString();
            if(miner_!=miner||!r||!r->compatible||!r->client->available()||identity.isEmpty()||identity.size()>128){answer(miner,device,identity,{{"ok",false},{"error",QJsonObject{{"message","CPU endpoint ownership/identity unavailable"}}}});return true;}
            auto command=packet.value("command").toObject();const auto action=command.take("action").toString();
            const auto id=r->client->ownedCommand(action,command);
            if(id<1){answer(miner,device,identity,{{"ok",false},{"error",QJsonObject{{"message","Owned CPU command refused"}}}});return true;}
            r->pending.insert(id,{miner,identity});return true;
        }
        if(!r||miner_!=miner)return true;
        if(packet.value("status").isObject())r->status=packet.value("status").toObject();
        r->status.insert("normal_cycle_mode","qualification");r->status.insert("publication_admitted",false);
        if(packet.value("resource_enabled").isBool())r->selected=packet.value("resource_enabled").toBool();
        r->status.insert("resource_selected",r->selected);
        if(packet.contains("message")){r->reason=packet.value("message").toString();r->reasonCode=packet.value("reason_code").toString();}
        r->status.insert("qualification_reason",r->reason);r->status.insert("qualification_reason_code",r->reasonCode);
        if(packet.value("type")=="control"){
            const auto request=packet.value("id").toString();
            if(request==r->control){r->control.clear();if(onControl&&!request.startsWith("restore-resource:"))onControl(device,request,packet.value("applied").toBool(),packet.value("message").toString("CPU qualification resource budget applied"));settled(r);}
        }
        present(r);return true;
    }
    void disconnected(QProcess* miner){
        if(miner_!=miner)return;miner_=nullptr;
        for(const auto& r:resources_){
            r->client->pause();r->pending.clear();
            if(!r->control.isEmpty()&&onControl)onControl(r->endpoint.deviceId,r->control,false,"Normal miner disconnected; CPU frontier retained");r->control.clear();
            settled(r);
        }
    }
    bool allStopped()const{if(cpuMiner_&&cpuMiner_->state()!=QProcess::NotRunning)return false;for(const auto& r:resources_)if(!r->client->stopped())return false;return true;}
    bool requestClose(){if(allStopped())return true;if(!closing_){closing_=true;if(cpuMiner_&&cpuMiner_->state()!=QProcess::NotRunning)cpuMiner_->write("{\"action\":\"quit\"}\n");else for(const auto& r:resources_)r->client->close();}return allStopped();}
};
}

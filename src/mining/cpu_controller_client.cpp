#include "mining/cpu_controller_client.h"
#include "mining/cpu_audit_result.h"
#include <QJsonDocument>
#include <QSet>
#include <QPointer>
#include <QRegularExpression>
#include <algorithm>
namespace pgn::mining {
CpuControllerClient::CpuControllerClient(CpuControlEndpoint endpoint,QObject* parent):QObject(parent),endpoint_(std::move(endpoint)){
    clock_.start();process_.setProcessChannelMode(QProcess::SeparateChannels);timer_.setInterval(250);
    reconnect_.setSingleShot(true);connect(&reconnect_,&QTimer::timeout,this,[this]{if(!closing_&&process_.state()==QProcess::NotRunning)start();});
    connect(&process_,&QProcess::readyReadStandardOutput,this,[this]{receive();});
    connect(&process_,&QProcess::readyReadStandardError,this,[this]{const auto bytes=process_.readAllStandardError();if(!bytes.isEmpty()){terminalDiagnostic_=QString::fromUtf8(bytes.right(1024)).trimmed();fail("CPU service diagnostic: "+terminalDiagnostic_);}});
    connect(&process_,&QProcess::errorOccurred,this,[this](auto error){fail("CPU controller process is unavailable");if(error==QProcess::FailedToStart)scheduleReconnect();});
    connect(&process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,auto status){receive();timer_.stop();ready_=false;healthy_=false;pending_.clear();if(closing_&&code==0&&status==QProcess::NormalExit){if(onClosed)onClosed();}else{fail(QString("CPU controller exited (%1); checkpoint retained").arg(code)+(terminalDiagnostic_.isEmpty()?QString{}:QString(": ")+terminalDiagnostic_));scheduleReconnect();}});
    connect(&timer_,&QTimer::timeout,this,[this]{
        bool expired=false;
        for(auto i=pending_.begin();i!=pending_.end();){
            if(!i->timedOut&&clock_.elapsed()-i->started>10000){
                expired=true;
                // Read-only observation can be retried. Never replay a mutation
                // whose acknowledgement was lost; keep its exact identity.
                if(i->action=="status"){retiredStatus_.append(i.key());while(retiredStatus_.size()>16)retiredStatus_.removeFirst();i=pending_.erase(i);continue;}
                i->timedOut=true;
            }
            ++i;
        }
        if(expired)fail("CPU controller response timed out; waiting for confirmed state");
        if(ready_&&!closing_){for(const auto& request:pending_)if(request.action=="status")return;send("status");}
    });
}
void CpuControllerClient::fail(const QString& message){healthy_=false;if(onError)onError(message);}
void CpuControllerClient::scheduleReconnect(){
    if(closing_||reconnect_.isActive())return;
    // Reopen only the idle inventory/status channel. Lost mutations are never
    // replayed: a new controller must report its confirmed, paused state first.
    reconnect_.start(std::min(30000,1000*(1<<std::min(reconnectAttempts_,5))));
    reconnectAttempts_=std::min(reconnectAttempts_+1,5);
}
void CpuControllerClient::start(){if(process_.state()!=QProcess::NotRunning)return;if(endpoint_.program.isEmpty()||endpoint_.deviceId.isEmpty()){fail("Explicit CPU controller transport required");return;}ready_=false;healthy_=false;closing_=false;pending_.clear();retiredStatus_.clear();terminalDiagnostic_.clear();buffer_.clear();process_.setProgram(endpoint_.program);process_.setArguments(endpoint_.arguments);process_.start();timer_.start();}
qint64 CpuControllerClient::send(QString action,QJsonObject fields){if(!ready_||process_.state()!=QProcess::Running||pending_.size()>=16){fail("CPU controller is not ready for another request");return -1;}const auto id=++sequence_;fields.insert("id",id);fields.insert("action",action);const auto bytes=QJsonDocument(fields).toJson(QJsonDocument::Compact);if(bytes.size()>32768){fail("CPU control request exceeds its size budget");return -1;}pending_.insert(id,{action,clock_.elapsed()});if(process_.write(bytes+'\n')<0){pending_.remove(id);fail("CPU control request could not be sent");return -1;}return id;}
void CpuControllerClient::receive(){
    buffer_.append(process_.readAllStandardOutput());if(buffer_.size()>262144){buffer_.clear();fail("CPU controller output exceeded its admitted buffer");return;}
    while(buffer_.contains('\n')){const auto at=buffer_.indexOf('\n');const auto line=buffer_.left(at);buffer_.remove(0,at+1);if(line.size()>32768){fail("CPU control response exceeds32KiB");continue;}QJsonParseError error;const auto value=QJsonDocument::fromJson(line,&error).object();if(error.error!=QJsonParseError::NoError){fail("CPU controller emitted invalid JSON");continue;}
        if(value.value("event")=="ready"){if(ready_||value.value("schema")!="PGN_CPU_JSONLINES_V1"||value.value("compute_started")!=false){fail("CPU inventory handshake did not preserve the idle contract");continue;}ready_=true;if(closing_)send("close",{{"drain",true}});else send("inventory");continue;}
        if(value.value("event")=="worker_error"){terminalDiagnostic_=value.value("message").toString().left(300);fail(terminalDiagnostic_);continue;}
        const auto id=value.value("id").toInteger(-1);const auto found=pending_.find(id);if(found==pending_.end()){if(retiredStatus_.removeOne(id))continue;fail("CPU response does not match an outstanding request");continue;}const auto request=*found;pending_.erase(found);
        if(onReply)onReply(id,value);
        if(value.value("ok")!=true){fail(value.value("error").toObject().value("message").toString("CPU request refused").left(300));}
        if(request.action=="result"&&value.value("ok")==true){
            const auto result=value.value("result").toObject();const auto error=cpuAuditResultError(result,request.expectedDigest);
            if(!error.isEmpty()){fail(error);continue;}
            if(onAuditResult)onAuditResult(result);
        }
        if(value.value("inventory").isObject()){const auto inventory=value.value("inventory").toObject();if(inventory.value("compute_started")!=false||inventory.value("maximum_workers").toInt()<0||inventory.value("available_cpus").toArray().isEmpty()){fail("CPU inventory is invalid or started unrequested work");continue;}inventory_=inventory;if(onInventory)onInventory(inventory_);}
        if(value.value("status").isObject()&&request.action!="evidence"){auto state=value.value("status").toObject();if(state.value("network_accepted")!=false){fail("Local CPU controller cannot grant network acceptance");continue;}healthy_=value.value("ok")==true;for(const auto& pending:pending_)if(pending.timedOut)healthy_=false;if(healthy_)reconnectAttempts_=0;state.insert("device_id",endpoint_.deviceId);state.insert("host",endpoint_.host);state.insert("controller_connected",available());if(onStatus)onStatus(state);}
    }
}
QJsonArray CpuControllerClient::affinityFor(int workers)const{
    QJsonArray result;if(workers<1||workers>inventory_.value("maximum_workers").toInt())return result;QSet<int> available,seen;for(const auto& value:inventory_.value("available_cpus").toArray())available.insert(value.toInt(-1));
    const auto groups=inventory_.value("topology").toArray();for(int pass=0;pass<2;++pass)for(const auto& group:groups){const auto cpus=group.toObject().value("cpus").toArray();for(int i=0;i<cpus.size();++i){const int cpu=cpus[i].toInt(-1);if((pass==0&&i!=0)||(pass==1&&i==0)||!available.contains(cpu)||seen.contains(cpu))continue;seen.insert(cpu);result.append(cpu);if(result.size()==workers)return result;}}
    return {};
}
bool CpuControllerClient::openAudit(const QJsonObject& request){if(request.value("schema")!="PGN_EXPLICIT_CPU_AUDIT_REQUEST_V1"){fail("An authenticated assigned CPU audit is required");return false;}return send("open",{{"request",request}})>0;}
qint64 CpuControllerClient::configure(bool enabled,int workers){const auto affinity=affinityFor(workers);if(affinity.size()!=workers){fail("Requested CPU budget cannot be mapped to dedicated available processors");return -1;}const auto generation=++generation_;send("configure",{{"enabled",enabled},{"threads",workers},{"affinity",affinity},{"generation",generation}});return generation;}
bool CpuControllerClient::requestAuditResult(const QString& digest){
 static const QRegularExpression hash("^[0-9a-f]{64}$");if(!hash.match(digest).hasMatch()){fail("Exact assigned CPU request digest required");return false;}
 const auto id=send("result");if(id>0)pending_[id].expectedDigest=digest;return id>0;
}
bool CpuControllerClient::acknowledgeAuditResult(const QString& digest){
 static const QRegularExpression hash("^[0-9a-f]{64}$");if(!hash.match(digest).hasMatch()){fail("Matching retained CPU result digest required");return false;}return send("ack_result",{{"request_sha256",digest}})>0;
}
bool CpuControllerClient::discardAudit(const QString& digest){
 static const QRegularExpression hash("^[0-9a-f]{64}$");if(!hash.match(digest).hasMatch()){fail("Matching paused CPU assignment digest required");return false;}return send("discard",{{"request_sha256",digest}})>0;
}
void CpuControllerClient::pause(){send("pause",{{"generation",++generation_}});}
void CpuControllerClient::close(){closing_=true;reconnect_.stop();if(process_.state()==QProcess::NotRunning){if(onClosed)onClosed();return;}if(ready_)send("close",{{"drain",true}});}
}

namespace pgn::mining {
qint64 CpuControllerClient::ownedCommand(QString action,QJsonObject fields){
    const QSet<QString> allowed{"status","execute","evidence","configure","pause","ack_result","discard"};
    if(!allowed.contains(action)){fail("Only normal-cycle owned executor commands may cross the miner bridge");return -1;}
    if(action=="configure"||action=="pause")fields.insert("generation",++generation_);
    return send(std::move(action),std::move(fields));
}
}

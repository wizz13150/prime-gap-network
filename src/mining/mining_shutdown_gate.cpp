#include "mining/mining_shutdown_gate.h"
#include "mining/source_decimal.h"
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <tuple>

namespace pgn::mining {
MiningShutdownGate::Entry* MiningShutdownGate::find(const QString& id){for(auto& e:entries_)if(e.request.id==id)return &e;return nullptr;}
const MiningShutdownGate::Entry* MiningShutdownGate::find(const QString& id)const{for(const auto& e:entries_)if(e.request.id==id)return &e;return nullptr;}
void MiningShutdownGate::reset(){entries_.clear();failure_.clear();begun_=false;}
QVector<MiningStopRequest> MiningShutdownGate::begin(const QVector<MiningStopTarget>& targets,bool inventory,bool reconciled){
    if(begun_)return {};begun_=true;
    if(!inventory||!reconciled){failure_="Shutdown requires fresh owned inventory and reconciled transitions";return {};}
    static const QRegularExpression device("^GPU-[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}$"),run("^[0-9a-f]{32}$");
    QSet<QString> devices;
    for(const auto& target:targets){
        if(!device.match(target.device).hasMatch()||!run.match(target.run).hasMatch()||
           target.start.size()>160||target.end.size()>160||!source_decimal::canonical(target.start)||!source_decimal::canonical(target.end)||!source_decimal::less(target.start,target.end)||devices.contains(target.device.toLower())){
            failure_="Shutdown target identity/range/inventory invalid";return {};
        }
        devices.insert(target.device.toLower());
    }
    const auto nonce=QUuid::createUuid().toString(QUuid::Id128);QVector<MiningStopRequest> requests;
    for(const auto& target:targets){MiningStopRequest request{"close-"+nonce+"-"+QString::number(requests.size()),target};entries_.push_back({request,{},{},{},{}});requests.push_back(request);}
    return requests;
}
bool MiningShutdownGate::acknowledgeStop(const QString& id,bool applied,const QString& detail){
    auto* e=find(id);if(!e)return false;
    // Physical checkpoint evidence can resolve a lost/uncertain ACK. A late
    // transport failure never erases that stronger observation.
    if(!applied&&!e->proof)e->error=detail.isEmpty()?"Stop acknowledgement unavailable; physical outcome unconfirmed":detail;
    return true;
}
bool MiningShutdownGate::observeCheckpoint(const QString& id,const SourceStopCheckpointEvidence& evidence){
    auto* e=find(id);if(!e||e->conflict)return false;
    const auto& target=e->request.target;
    if(evidence.expectedRun!=target.run||evidence.manifest.value("uuid").toString().compare(target.device,Qt::CaseInsensitive)!=0){e->error="Stopped checkpoint device/run mismatch";return false;}
    const auto result=validateSourceStopCheckpoint(evidence);
    if(result.status!=SourceStopCheckpointStatus::Ready||!result.resume){e->error=result.error;return false;}
    const auto& proof=*result.resume;
    if(proof.run!=target.run||proof.start!=target.start||proof.end!=target.end){e->error="Stopped checkpoint range changed";return false;}
    if(e->proof&&e->proof->checkpointSha256!=proof.checkpointSha256){e->conflict=true;e->saved=false;e->error="Conflicting checkpoint bytes for one stopped run";return false;}
    e->proof=proof;e->error.clear();return true;
}
bool MiningShutdownGate::storageCommitted(const QString& id,const SourceCheckpointResume& tuple,bool committed){
    auto* e=find(id);if(!e||e->conflict)return false;
    const auto fields=[](const SourceCheckpointResume& p){return std::tie(p.run,p.start,p.end,p.length,p.positions,p.nextStart,p.remaining,p.tests,p.gaps,p.batches,p.counterTimeNs,p.checkpointLabel,p.checkpointSha256);};
    if(!e->proof||fields(tuple)!=fields(*e->proof)){e->error="Storage receipt does not match a validated checkpoint tuple";return false;}
    e->saved=committed;e->error=committed?QString{}:"Resume state commit failed; application must not silently quit";return true;
}
std::optional<SourceCheckpointResume> MiningShutdownGate::checkpoint(const QString& id)const{const auto* e=find(id);return e?e->proof:std::nullopt;}
MiningShutdownPhase MiningShutdownGate::phase()const{
    if(!begun_)return MiningShutdownPhase::Idle;if(!failure_.isEmpty())return MiningShutdownPhase::Held;
    bool all=true;for(const auto& e:entries_){if(e.conflict||!e.error.isEmpty())return MiningShutdownPhase::Held;all&=e.saved;}
    return all?MiningShutdownPhase::Ready:MiningShutdownPhase::Waiting;
}
QString MiningShutdownGate::error()const{if(!failure_.isEmpty())return failure_;for(const auto& e:entries_)if(!e.error.isEmpty())return e.error;return {};}
}

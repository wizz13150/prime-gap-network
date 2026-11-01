#include "mining/mining_shutdown_gate.h"
#include <QCoreApplication>
#include <array>
#include <iostream>
#include <stdexcept>

using namespace pgn::mining;
namespace {
int checks=0;void require(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
const QString device="GPU-11111111-2222-3333-4444-555555555555",other="GPU-66666666-7777-8888-9999-aaaaaaaaaaaa",run(32,'a'),second(32,'b');
MiningStopTarget target(QString uuid=device,QString id=run){return {uuid,id,"184467440737095517160","184467440737095527160"};}
SourceStopCheckpointEvidence evidence(MiningStopTarget t=target(),quint64 positions=3000){
    const std::array<quint64,7> words{1000,10,10000,positions,9007199254740993ULL,0,4};QByteArray bytes("PGNCG001",8);
    for(const auto value:words)for(unsigned n=0;n<8;++n)bytes.append(char((value>>(8*n))&255));
    SourceStopCheckpointEvidence e;e.checkpointBytes=bytes;e.expectedRun=t.run;e.owned=false;e.checkpointLabel="C:/PGN/runtime/current-job/checkpoint";
    e.manifest={{"schema","pgn-native-process-1"},{"uuid",t.device},{"run",t.run},{"start",t.start},{"end",t.end}};e.lastExit={{"run",t.run},{"exit_code",0}};
    e.sourceTail="[pgn-counters] time_ns=9007199254740995 positions="+QByteArray::number(positions)+" gaps=0 tests=9007199254740993 batches=4 processed_gaps=0\n[phase0-gpu] stopped positions="+QByteArray::number(positions)+" checkpoint="+e.checkpointLabel.toUtf8()+"\n";return e;
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        MiningShutdownGate g;require(g.phase()==MiningShutdownPhase::Idle,"idle is not ready-to-quit proof");
        require(g.begin({},false,true).isEmpty()&&g.phase()==MiningShutdownPhase::Held,"empty but unverified physical inventory cannot close silently");
        g.reset();require(g.begin({},true,false).isEmpty()&&g.phase()==MiningShutdownPhase::Held,"unresolved prepared work prevents close even if no running target seen");
        g.reset();require(g.begin({},true,true).isEmpty()&&g.phase()==MiningShutdownPhase::Ready,"verified idle application may quit without invented checkpoint");
        g.reset();auto invalid=target();invalid.end=invalid.start;
        require(g.begin({target(),invalid},true,true).isEmpty()&&g.phase()==MiningShutdownPhase::Held,"all targets validated before any stop request");
        g.reset();require(g.begin({target(),target(device.toLower(),second)},true,true).isEmpty()&&g.phase()==MiningShutdownPhase::Held,"duplicate physical device refused case-insensitively");
        g.reset();auto requests=g.begin({target(),target(other,second)},true,true);
        require(requests.size()==2&&requests[0].id!=requests[1].id&&g.phase()==MiningShutdownPhase::Waiting,"one unique correlated stop request per verified owned target");
        require(g.begin({target()},true,true).isEmpty(),"duplicate window close cannot resend stop requests");
        require(!g.acknowledgeStop("stale",true)&&!g.observeCheckpoint("stale",evidence()),"unknown/stale callback ignored");
        require(g.acknowledgeStop(requests[0].id,true)&&g.phase()==MiningShutdownPhase::Waiting,"positive command ACK alone never authorizes quit");
        auto wrong=evidence();wrong.owned.reset();require(!g.observeCheckpoint(requests[0].id,wrong)&&g.phase()==MiningShutdownPhase::Held,"missing owned=false is not confirmed stop");
        wrong=evidence();wrong.manifest["uuid"]=other;require(!g.observeCheckpoint(requests[0].id,wrong),"another device checkpoint cannot settle request");
        wrong=evidence();wrong.expectedRun=second;require(!g.observeCheckpoint(requests[0].id,wrong),"another expected run cannot settle request");
        require(g.observeCheckpoint(requests[0].id,evidence())&&g.phase()==MiningShutdownPhase::Waiting,"valid physical declaration tuple still waits for storage");
        auto proof=g.checkpoint(requests[0].id);require(proof&&proof->tests=="9007199254740993","exact source counters survive guard");
        auto bad=*proof;bad.positions="2999";
        require(!g.storageCommitted(requests[0].id,bad,true)&&g.phase()==MiningShutdownPhase::Held,"same hash but wrong committed state is not accepted");
        require(g.storageCommitted(requests[0].id,*proof,false)&&g.phase()==MiningShutdownPhase::Held,"storage failure never permits silent application quit");
        require(g.storageCommitted(requests[0].id,*proof,true)&&g.phase()==MiningShutdownPhase::Waiting,"one saved device cannot hide another pending device");
        require(g.acknowledgeStop(requests[1].id,false,"SSH outcome uncertain")&&g.phase()==MiningShutdownPhase::Held,"uncertain stop is held without retry loop");
        require(g.observeCheckpoint(requests[1].id,evidence(target(other,second)))&&g.phase()==MiningShutdownPhase::Waiting,"fresh exact evidence resolves lost transport ACK");
        auto secondProof=g.checkpoint(requests[1].id);require(secondProof&&secondProof->run==second,"second stopped state remains independently correlated");
        require(!g.storageCommitted(requests[1].id,*proof,true),"identical checkpoint bytes from another run do not prove this store commit");
        require(g.storageCommitted(requests[1].id,*secondProof,true)&&g.phase()==MiningShutdownPhase::Ready,"all exact checkpoints stored permits quit");
        require(g.acknowledgeStop(requests[0].id,false,"late failure")&&g.phase()==MiningShutdownPhase::Ready,"late weaker ACK cannot erase stored physical proof");
        const auto old=requests[0].id;g.reset();requests=g.begin({target()},true,true);
        require(requests.size()==1&&requests[0].id!=old&&!g.acknowledgeStop(old,true),"explicit new inventory uses different nonce; old callback stale");
        require(!g.storageCommitted(requests[0].id,*proof,true)&&g.phase()==MiningShutdownPhase::Held,"storage callback before new proof cannot close a new attempt");
        require(g.observeCheckpoint(requests[0].id,evidence()),"new exact proof accepted");
        require(!g.observeCheckpoint(requests[0].id,evidence(target(),4000))&&g.phase()==MiningShutdownPhase::Held,"conflicting checkpoint bytes held, original not overwritten");
        require(g.checkpoint(requests[0].id)->positions=="3000"&&!g.storageCommitted(requests[0].id,*proof,true),"conflict cannot be cleared by copying old storage ACK");
        require(g.error().contains("Conflicting"),"conflict exposed, never reported as successful shutdown");
        std::cout<<"Mining shutdown gate: "<<checks<<" checks, 0 failures; pure policy, no process/GPU/storage operation\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Mining shutdown gate failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}

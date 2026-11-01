#include "mining/completion_advance_gate.h"
#include "mining/source_decimal.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace pgn::mining;
namespace {
int checks=0;void require(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
const QString device="GPU-11111111-2222-3333-4444-555555555555",run(32,'a'),Booking="11111111-2222-4333-8444-555555555555";
QJsonObject packet(QString observed="2026-10-14T18:00:00.000000001Z",QString sourceRun=run){
    QJsonObject result{{"state","OFF"},{"owned",false},{"operator_stop",false},{"pending_bytes",0},{"observed_utc",observed},
        {"manifest",QJsonObject{{"run",sourceRun},{"uuid",device},{"queued_intent",Booking},{"start","199900000000000000000"},{"end","199901000000000000000"}}},
        {"last_exit",QJsonObject{{"run",sourceRun},{"exit_code",0}}},
        {"completion_candidate",QJsonObject{{"schema","pgn-source-range-completion-1"},{"run",sourceRun},{"start","199900000000000000000"},{"end","199901000000000000000"},{"source_positions","1000000000000000"},{"source_range_complete",true},{"source_exit_code",0}}}};
    // TESTONLY peer-verifier DTO, not execution of a RAW verifier/image/GPU.
    result.insert("resume_lineage",QJsonObject{{"schema","pgn-source-resume-lineage-1"},{"ready",true},{"error",""},{"terminal","complete"},
        {"run",sourceRun},{"booking_id",Booking},{"start","199900000000000000000"},{"end","199901000000000000000"},
        {"epochs",1},{"chain_sha256",QString(64,'d')},{"scope","LOCAL_RAW_LINEAGE_COUNTER_DELTAS_NOT_INDEPENDENT_PRIME_COVERAGE"}});
    return result;
}
void nested(QJsonObject& p,const char* object,const char* field,const QJsonValue& value){auto obj=p.value(object).toObject();obj.insert(field,value);p.insert(object,obj);}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        const auto valid=packet();require(completeSourceMatches(valid,run),"exact 1e15 completion");
        require(sourceLineageMatches(valid,"complete")&&!sourceResumeLineageMatches(valid,"complete"),"legacy A73 one-journal ready proof is generic, not a fabricated resume");
        auto bare=valid;bare.remove("resume_lineage");CompletionAdvanceGate formerlyBare(device);
        require(!completeSourceMatches(bare,run)&&!formerlyBare.claim(bare,run,0,true,false,false),"nonresumed complete footer without exact RAW proof cannot complete or claim");
        require(formerlyBare.claim(valid,run,0,true,false,false).has_value(),"refused bare proof consumes no ticket or observation freshness");
        const auto unchanged=valid;require(completeSourceMatches(valid,run)&&completeSourceMatches(valid,run)&&valid==unchanged,"ready exact replay remains pure and unmodified");
        for(int poison=0;poison<15;++poison){auto bad=valid;auto proof=bad.value("resume_lineage").toObject();auto m=bad.value("manifest").toObject();
            switch(poison){case 0:proof.insert("ready",false);break;case 1:proof.insert("ready","true");break;case 2:proof.insert("error","Lineage held");break;
            case 3:proof.insert("run",QString(32,'b'));break;case 4:proof.insert("booking_id","not-uuid");break;case 5:proof.insert("start","1");break;
            case 6:proof.insert("end","2");break;case 7:proof.insert("epochs",0);break;case 8:proof.insert("epochs",1.5);break;case 9:proof.insert("epochs","1");break;
            case 10:proof.insert("chain_sha256",QString(64,'D'));break;case 11:proof.insert("scope","INDEPENDENT_COVERAGE");break;case 12:proof.insert("terminal","stopped");break;
            case 13:proof.insert("schema","unknown");break;case 14:m.remove("queued_intent");break;}
            bad.insert("manifest",m);bad.insert("resume_lineage",proof);require(!completeSourceMatches(bad,run),"ordinary completion cannot bypass exact peer RAW ABI");
        }
        auto heldV2=valid;auto heldProof=heldV2.value("resume_lineage").toObject();heldProof.insert("ready",false);heldProof.insert("error","terminal group held");
        heldProof.insert("source_boundary_protocol","pgn-source-boundary-v2");heldProof.insert("source_boundary_groups","1");heldV2.insert("resume_lineage",heldProof);
        heldV2.insert("binding",QJsonObject{{"executable","PUBLIC-TESTONLY/A73.exe"},{"pins",QJsonArray{QJsonObject{{"path","PUBLIC-TESTONLY/A73.exe"},{"sha256","A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43"}}}}});
        require(!completeSourceMatches(heldV2,run),"A73-looking binding and complete footer cannot downgrade v2 Held RAW proof");
        auto grammar=valid;auto gp=grammar.value("resume_lineage").toObject();gp.insert("source_boundary_protocol","pgn-source-boundary-v2");gp.insert("source_boundary_groups","1");grammar.insert("resume_lineage",gp);
        require(!completeSourceMatches(grammar,run),"even ready-shaped exact v2 grammar cannot license an unqualified immutable run profile");
        const QJsonObject v2Profile{{"schema","pgn-run-source-profile-1"},{"protocol","pgn-source-boundary-v2"},
            {"image_sha256","74a0fdd01ccf242fa3ca8714e4765c23022987c6785889cd8b1ecfa3aa27472b"},
            {"executable","C:/PGN/native/20261015-source-boundary-v2/bin/pgn-continuous-gpu.exe"}};
        auto qualified=grammar;nested(qualified,"manifest","source_profile",v2Profile);
        require(completeSourceMatches(qualified,run),"exact qualified v2 immutable profile plus ready RAW DTO admits SOURCE_PRP completion only");
        auto v2WithoutGrammar=valid;nested(v2WithoutGrammar,"manifest","source_profile",v2Profile);
        require(!completeSourceMatches(v2WithoutGrammar,run),"known v2 profile cannot accept absent or legacy grammar");
        auto qualifiedHeld=heldV2;nested(qualifiedHeld,"manifest","source_profile",v2Profile);
        require(!completeSourceMatches(qualifiedHeld,run),"qualified image never overrides Held RAW evidence");
        for(const auto& value:QJsonArray{QJsonValue::Null,0,"0","01","-1","18446744073709551616"}){
            auto bad=qualified;nested(bad,"resume_lineage","source_boundary_groups",value);
            require(!completeSourceMatches(bad,run),"known image still requires positive exact bounded completed v2 group count");
        }
        auto invalidProfile=valid;auto invalidManifest=invalidProfile.value("manifest").toObject();invalidManifest.insert("source_profile",QJsonValue::Null);invalidProfile.insert("manifest",invalidManifest);require(!completeSourceMatches(invalidProfile,run),"malformed present profile cannot downgrade to historical absence through peer ABI");
        for(const auto& value:QJsonArray{QJsonValue::Null,0,"0","01","-1","18446744073709551616"}){auto bad=grammar;auto proof=gp;proof.insert("source_boundary_groups",value);bad.insert("resume_lineage",proof);require(!completeSourceMatches(bad,run),"full completion needs positive canonical bounded v2 group count");}
        auto partial=grammar;gp.insert("terminal","stopped");gp.insert("source_boundary_groups","0");partial.insert("resume_lineage",gp);
        require(!sourceLineageMatches(partial,"stopped")&&!completeSourceMatches(partial,run),"v2 STOP groups0 conserves grammar but cannot license an unqualified image");
        require(source_decimal::difference("1"+QString(1000,'0'),"1")==QString(1000,'9'),"unbounded exact borrow chain, no integer-library dependency");
        require(source_decimal::difference("0","0")=="0","exact zero difference");
        require(!completeSourceMatches(valid,QString{}),"standalone range match also requires exact run identity");
        auto resumed=valid;auto manifest=resumed.value("manifest").toObject();
        const QString booking="11111111-2222-4333-8444-555555555555";
        manifest.insert("resume_previous_run",QString(32,'b'));manifest.insert("checkpoint_input_sha256",QString(64,'c'));manifest.insert("queued_intent",booking);resumed.insert("manifest",manifest);
        require(!completeSourceMatches(resumed,run),"cumulative resumed footer alone cannot close raw lineage");
        const QJsonObject lineage{{"schema","pgn-source-resume-lineage-1"},{"ready",true},{"error",""},{"terminal","complete"},{"run",run},{"booking_id",booking},
            {"start",manifest.value("start")},{"end",manifest.value("end")},{"epochs",2},{"chain_sha256",QString(64,'d')},{"scope","LOCAL_RAW_LINEAGE_COUNTER_DELTAS_NOT_INDEPENDENT_PRIME_COVERAGE"}};
        resumed.insert("resume_lineage",lineage);require(completeSourceMatches(resumed,run),"exact peer logical-lineage ABI accepted, not independent prime proof");
        auto stopped=resumed;auto stoppedProof=lineage;stoppedProof.insert("terminal","stopped");stopped.insert("resume_lineage",stoppedProof);
        require(sourceResumeLineageMatches(stopped,"stopped")&&!completeSourceMatches(stopped,run),"stopped lineage is resumable provenance, never completed coverage");
        for(int poison=0;poison<13;++poison){auto bad=resumed;auto proof=lineage;auto m=manifest;
            switch(poison){case 0:proof.remove("ready");break;case 1:proof.insert("ready","true");break;case 2:proof.insert("terminal","stopped");break;
            case 3:proof.insert("run",QString(32,'b'));break;case 4:proof.insert("booking_id","not-a-UUID");m.insert("queued_intent","not-a-UUID");break;
            case 5:proof.insert("start","1");break;case 6:proof.insert("epochs",1);break;case 7:proof.insert("epochs",2.5);break;
            case 8:proof.insert("epochs","2");break;case 9:proof.insert("chain_sha256",QString(64,'G'));break;case 10:proof.insert("scope","INDEPENDENT_COVERAGE");break;
            case 11:m.remove("checkpoint_input_sha256");break;case 12:m.insert("resume_previous_run",run);break;}
            bad.insert("manifest",m);bad.insert("resume_lineage",proof);require(!completeSourceMatches(bad,run),"wrong/partial/stopped/raw lineage cannot fabricate SOURCE closure");
        }
        CompletionAdvanceGate newline(device);require(!newline.claim(packet("2026-10-14T18:00:00.000000001Z\n"),run,0,true,false,false),"UTC newline is not a fresh exact observation");
        CompletionAdvanceGate gate(device);auto first=gate.claim(valid,run,0,true,false,false);require(first&&first->attempt==1,"first completed observation");
        require(!gate.claim(valid,run,0,true,false,false),"single in-flight ticket");
        auto state=gate.settle(*first,AdvanceOutcome::UncertainTransport,10);require(!state.stale&&state.retryable&&!state.held,"only uncertain result schedules bounded retry");
        const auto fresh=packet("2026-10-14T18:00:00.000000002Z");
        require(!gate.claim(fresh,run,10009,true,false,false),"backoff boundary exact");
        require(!gate.claim(valid,run,10010,true,false,false),"same observation never retries even at due time");
        auto second=gate.claim(fresh,run,10010,true,false,false);require(second&&second->attempt==2,"nanosecond-new observation accepted");
        require(gate.settle(*first,AdvanceOutcome::Success,10010).stale,"late old attempt cannot settle new attempt");
        state=gate.settle(*second,AdvanceOutcome::UncertainTransport,10010);require(state.retryable,"second uncertainty bounded");
        auto third=gate.claim(packet("2026-10-14T18:00:00.000000003Z"),run,20010,true,false,false);require(third&&third->attempt==3,"third last attempt");
        state=gate.settle(*third,AdvanceOutcome::UncertainTransport,20010);require(state.held&&!state.retryable,"uncertainty budget holds exact epoch");
        require(!gate.claim(packet("2026-10-14T18:00:00.000000004Z"),run,999999,true,false,false),"no fourth attempt");
        gate.reset();require(gate.settle(*third,AdvanceOutcome::Success,999999).stale,"manual reset invalidates delayed result");
        auto accepted=gate.claim(valid,run,0,true,false,false);require(bool(accepted),"manual reset permits new deliberate epoch");
        require(gate.settle(*accepted,AdvanceOutcome::Success,0).held,"success never duplicates");
        gate.reset();accepted=gate.claim(valid,run,0,true,false,false);require(gate.settle(*accepted,AdvanceOutcome::Rejected,0).held,"invariant/source rejection never retries");
        for(int which=0;which<24;++which){auto p=valid;switch(which){
            case 0:p["state"]="RUNNING";break;case 1:p["owned"]=true;break;case 2:p.remove("owned");break;
            case 3:p["operator_stop"]=true;break;case 4:p.remove("operator_stop");break;case 5:p["pending_bytes"]=1;break;
            case 6:p["pending_bytes"]="0";break;case 7:nested(p,"manifest","uuid","GPU-99999999-2222-3333-4444-555555555555");break;
            case 8:nested(p,"manifest","run",QString(32,'b'));break;case 9:nested(p,"last_exit","run",QString(32,'b'));break;
            case 10:nested(p,"last_exit","exit_code",1);break;case 11:nested(p,"last_exit","exit_code","0");break;
            case 12:nested(p,"completion_candidate","source_range_complete",false);break;case 13:nested(p,"completion_candidate","source_exit_code",1);break;
            case 14:nested(p,"completion_candidate","source_positions","999999999999999");break;case 15:nested(p,"completion_candidate","source_positions",1e15);break;
            case 16:nested(p,"completion_candidate","start","199900000000000000001");break;case 17:nested(p,"manifest","start","0199900000000000000000");break;
            case 18:p["observed_utc"]="2026-02-31T18:00:00Z";break;case 19:p["observed_utc"]="2026-10-14T18:00:00.0000000001Z";break;
            case 20:p["observed_utc"]="2026-10-14T18:00:00+00:00";break;case 21:p.remove("observed_utc");break;
            case 22:nested(p,"completion_candidate","schema","wrong");break;case 23:nested(p,"completion_candidate","run",QString(32,'b'));break;
        }CompletionAdvanceGate closed(device);require(!closed.claim(p,run,0,true,false,false),"malformed/unconfirmed source cannot advance");}
        for(int which=0;which<4;++which){CompletionAdvanceGate closed(device);require(!closed.claim(valid,run,which==3?-1:0,which!=0,which==1,which==2),"disabled/STOP/partial/invalid clock held");}
        CompletionAdvanceGate narrow(device,1000,1);auto ticket=narrow.claim(valid,run,0,true,false,false);require(ticket&&narrow.settle(*ticket,AdvanceOutcome::UncertainTransport,0).held,"configured one-attempt policy");
        CompletionAdvanceGate overflow(device);ticket=overflow.claim(valid,run,0,true,false,false);require(overflow.settle(*ticket,AdvanceOutcome::UncertainTransport,std::numeric_limits<qint64>::max()).held,"retry clock overflow fails closed");
        CompletionAdvanceGate epoch(device);ticket=epoch.claim(valid,run,0,true,false,false);const QString nextRun(32,'c');const auto next=epoch.claim(packet("2026-10-14T18:00:01Z",nextRun),nextRun,10,true,false,false);require(next&&epoch.settle(*ticket,AdvanceOutcome::Success,10).stale,"new completed run invalidates old in-flight epoch");
        std::cout<<"COMPLETION_ADVANCE_GATE "<<checks<<" checks / 0 failures; exact completion, explicit STOP/ownership, nano-UTC freshness, bounded typed uncertainty; no reservation/control/process calls\n";return 0;
    }catch(const std::exception& error){std::cerr<<"COMPLETION_ADVANCE_GATE failed after "<<checks<<": "<<error.what()<<'\n';return 1;}
}

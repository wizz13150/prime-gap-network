#include "mining/native_bridge.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <iostream>
#include <stdexcept>
namespace pgn::mining {
struct NativeBridgeTestAccess {
 static int pending(const NativeBridge& b){return b.pending_.size();}
 static void observer(NativeBridge& b,bool value){b.observationOnly_=value;}
};
}
using namespace pgn::mining;
namespace {
int checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",Run(32,'a');
QJsonObject source(){
 return {{"type","snapshot"},{"schema","pgn-runtime-snapshot-1"},{"state","STOPPING"},{"owned",true},
  {"observed_utc","2026-10-16T16:31:37.000Z"},{"source_clock_ns","3500000000"},{"pending_bytes",0},
  {"manifest",QJsonObject{{"schema","pgn-native-process-1"},{"run",Run},{"uuid",Device},{"pid",4242},
   {"birth","2026-10-16T15:39:08.0169669Z"},{"start","1000"},{"end","2000"}}},
  {"lines",QJsonArray{"[pgn-counters] time_ns=1000000000 positions=10 gaps=1 tests=100 batches=1 processed_gaps=1",
    "[pgn-counters] time_ns=2000000000 positions=30 gaps=2 tests=160 batches=2 processed_gaps=3"}}};
}
QJsonObject physical(){return {{"type","native_process_observation"},{"schema","pgn-runtime-snapshot-1"},
 {"owned",false},{"state","OFF"},{"operator_stop",true},{"observed_utc","2026-10-16T16:31:39.000Z"},
 {"manifest",source()["manifest"]},{"binding",QJsonObject{{"uuid",Device}}},
 {"last_exit",QJsonObject{{"run",Run},{"exit_code",0}}}};}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 NativeBridge bridge;QObject runtime,stranger;int sends{},acks{},sources{},resources{},gaps{},devices{};
 FrontierTelemetrySnapshot last;
 bridge.onDevice=[&](const auto& value){last=value;++devices;};
 bridge.onSourceSnapshot=[&](const auto&){++sources;};bridge.onResources=[&](const auto&){++resources;};
 bridge.onGap=[&](const auto&){++gaps;};bridge.onControl=[&](const auto&,const auto&,bool ok,const auto&){check(ok,"no fabricated negative ack");++acks;};
 check(bridge.attachNativeRuntime(&runtime,[&](const auto&){++sends;return true;}),"own local native runtime attached");
 bridge.acceptNativeRuntimePacket(&runtime,physical());check(devices==0&&acks==0,"physical inventory cannot adopt SOURCE identity");
 bridge.acceptNativeRuntimePacket(&runtime,source());check(devices>0&&last.raw.value("pgn_process_running")==true,"actual SOURCE last frame STOPPING");
 bridge.control(Device,"b5dc9351-2401-4fd6-8849-858b971ff7e8",false);QCoreApplication::processEvents();
 check(sends==1&&NativeBridgeTestAccess::pending(bridge)==1,"one explicit stop pending, no retry");
 const auto base=last;const auto before=devices;
 check(!bridge.acceptNativeRuntimePacket(&stranger,physical())&&devices==before,"unrelated owner cannot supply native observation");
 for(int variant=0;variant<9;++variant){auto wrong=physical();
  if(variant==0){auto m=wrong["manifest"].toObject();m["pid"]=4243;wrong["manifest"]=m;}
  if(variant==1){auto m=wrong["manifest"].toObject();m["birth"]="2026-10-16T15:40:00Z";wrong["manifest"]=m;}
  if(variant==2)wrong["last_exit"]=QJsonObject{{"run",QString(32,'b')},{"exit_code",0}};
  if(variant==3)wrong["last_exit"]=QJsonObject{{"run",Run},{"exit_code",1}};
  if(variant==4)wrong["owned"]=true;
  if(variant==5)wrong.remove("operator_stop");
  if(variant==6)wrong["observed_utc"]="invalid";
  if(variant==7)wrong["binding"]=QJsonObject{{"uuid","OTHER"}};
  if(variant==8)wrong["last_exit"]=QJsonObject{{"run",Run},{"exit_code","0"}};
  bridge.acceptNativeRuntimePacket(&runtime,wrong);
  check(devices==before&&acks==0&&NativeBridgeTestAccess::pending(bridge)==1,"mismatched physical tuple neither changes card nor settles stop");
 }
 NativeBridgeTestAccess::observer(bridge,true);bridge.acceptNativeRuntimePacket(&runtime,physical());
 check(devices==before&&acks==0,"observer cannot acquire physical-control capability");NativeBridgeTestAccess::observer(bridge,false);
 auto stopped=physical();stopped["lines"]=QJsonArray{"[pgn-counters] time_ns=9999999999 positions=999999 gaps=999 tests=999999 batches=999"};
 stopped["resume_lineage"]=QJsonObject{{"ready",true}};stopped["source_conservation"]=QJsonObject{{"ready",true}};
 bridge.acceptNativeRuntimePacket(&runtime,stopped);
 check(last.raw.value("pgn_process_running")==false&&last.state=="stopped"&&last.terminalSealed,"exact native final inventory leaves STOPPING");
 check(acks==1&&NativeBridgeTestAccess::pending(bridge)==0&&sends==1,"physical OFF settles only pending stop; no second command");
 check(last.rowsCompleted==base.rowsCompleted&&last.sequence==base.sequence&&last.observedUtc==base.observedUtc&&
  last.e2ePositionsPerSecond==base.e2ePositionsPerSecond&&last.powmPerSecond==base.powmPerSecond&&
  last.raw.value("pgn_source_time_ns")==base.raw.value("pgn_source_time_ns")&&last.raw.value("pgn_receipt_monotonic_ns")==base.raw.value("pgn_receipt_monotonic_ns"),
  "physical OFF preserves every raw rate/counter clock and freshness, even with injected lines");
 check(sources==1&&resources==0&&gaps==0,"physical OFF cannot publish SOURCE bytes, proof/Ready, GPU telemetry or gaps");
 std::cout<<"NATIVE_STOPPED_PROCESS_OBSERVATION "<<checks<<" checks / 0 failures; SELF only, no GPU or network\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

#include "mining/native_bridge.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>
#include <iostream>
#include <stdexcept>
namespace pgn::mining {struct NativeBridgeTestAccess {
 static void receive(NativeBridge& b,const QJsonObject& p){b.receive(p);}
 static void resources(NativeBridge& b,const QJsonObject& p){b.publishResources(p);}
};}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 2;unsigned checks=0;auto check=[&](bool yes){++checks;if(!yes)throw std::runtime_error("Native engine display bridge check "+std::to_string(checks));};
 using namespace pgn::mining;
 try{
  QFile file(QString::fromLocal8Bit(argv[1]));check(file.open(QIODevice::ReadOnly)&&file.size()<65536);const auto source=file.readAll();QJsonArray lines;quint64 lastClock=0;
  for(auto line:source.split('\n')){if(line.endsWith('\r'))line.chop(1);if(line.isEmpty())continue;lines.append(QString::fromUtf8(line));const auto m=QRegularExpression("^\\[pgn-counters\\] time_ns=([0-9]+)").match(QString::fromUtf8(line));if(m.hasMatch())lastClock=m.captured(1).toULongLong();}
  const QString gpu="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",run(32,'a');
  QJsonObject job{{"start","20000000000000000000000000000"},{"end","20000000000000002000000000000"},{"profile_k",64},{"minimum_gap","1332"}};
  QJsonObject manifest{{"run",run},{"uuid",gpu},{"start",job.value("start")},{"end",job.value("end")},{"birth","2026-10-30T00:00:00.000Z"},
   {"source_profile",QJsonObject{{"schema","pgn-run-source-profile-2"},{"protocol","PGN_NATIVE_GAP_RUNTIME_V1"},{"job",job}}}};
  const auto now=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  QJsonObject packet{{"type","snapshot"},{"schema","pgn-runtime-snapshot-1"},{"state","RUNNING"},{"observed_utc",now},{"source_clock_ns",QString::number(lastClock+1000000)},{"pending_bytes",0},{"manifest",manifest},{"lines",lines}};
  NativeBridge bridge;FrontierTelemetrySnapshot displayed;unsigned samples=0,gaps=0;
  bridge.onDevice=[&](const auto& value){displayed=value;++samples;};bridge.onGap=[&](const auto&){++gaps;};
  NativeBridgeTestAccess::receive(bridge,packet);
  check(samples>0&&gaps==8);check(displayed.numberBits==95);check(displayed.raw.value("pgn_native_min_gap")=="1332"&&displayed.raw.value("pgn_native_profile_k")==64);
  check(displayed.raw.value("pgn_cumulative_tests")=="5198622089");check(displayed.e2ePositionsPerSecond&&*displayed.e2ePositionsPerSecond>0);
  FrontierResourceTelemetrySnapshot resources;bridge.onResources=[&](const auto& value){resources=value;};
  const auto basic="0,"+gpu+",PUBLIC TEST GPU,0000:01:00.0,100,8192,24576,200,350,75,1500";
  for(const bool memory:{false,true})for(const bool throttle:{false,true}){
   const auto row=basic+(memory?",5001":"")+(throttle?",Not Active,Not Active,Not Active":"");
   NativeBridgeTestAccess::resources(bridge,{{"observed_utc",now},{"driver_valid",true},{"gpus",QJsonArray{row}}});
   check(resources.devices.size()==1&&resources.devices.first().smClockMhz==1500);
   check(memory?resources.devices.first().memoryClockMhz==5001:!resources.devices.first().memoryClockMhz.has_value());
   if(throttle)check(resources.devices.first().throttleClass=="NONE"&&resources.devices.first().throttleTelemetryAuthoritative);
  }
  std::cout<<"Existing Horizon native engine display bridge PASS "<<checks<<" checks; actual95counter data +legacy/new clock rows, no process/GPU\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

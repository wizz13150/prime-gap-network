#include "mining/native_bridge.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimeZone>
#include <iostream>
namespace pgn::mining {
struct NativeBridgeTestAccess {
 static void send(NativeBridge& bridge,const QJsonObject& packet){bridge.receive(packet);}
 static void feed(NativeBridge& bridge,const QByteArray& bytes){bridge.consumeBytes(bytes);}
};
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);pgn::mining::NativeBridge bridge;
 FrontierTelemetrySnapshot last;int samples=0,gaps=0,resources=0;bool gapUnknown=false;
 FrontierResourceTelemetrySnapshot hardware;
 bridge.onResources=[&](const auto& value){hardware=value;++resources;};
 bridge.onDevice=[&](const auto& sample){last=sample;++samples;};
 bridge.onGap=[&](const auto& event){++gaps;gapUnknown=!event.observedUtc.isValid();};
 const QString run(32,'a');const QDateTime now=QDateTime::currentDateTimeUtc();
 QJsonObject packet{{"type","snapshot"},{"state","RUNNING"},
   {"manifest",QJsonObject{{"run",run},{"start","100"},{"end","1000"}}},
   {"observed_utc",now.toString(Qt::ISODateWithMs)},
   {"source_clock_ns","10400000000"},{"pending_bytes",1000000000},
   {"lines",QJsonArray{
    "[pgn-counters] time_ns=10000000000 positions=0 gaps=0 tests=0 batches=0 processed_gaps=0",
    "[pgn-counters] time_ns=10200000000 positions=20000000000 gaps=2 tests=100 batches=1 processed_gaps=3"}}};
 pgn::mining::NativeBridgeTestAccess::send(bridge,packet);
 if(last.stale||!last.e2ePositionsPerSecond||*last.e2ePositionsPerSecond!=1e11||last.observedUtc.msecsTo(now)!=200){std::cerr<<"FAIL fresh counters hidden by historical backlog\n";return 1;}
 const int firstSamples=samples;
 if(resources!=0){std::cerr<<"FAIL absent driver data replaced hardware card\n";return 7;}
 pgn::mining::NativeBridgeTestAccess::send(bridge,packet);
 if(samples!=firstSamples||*last.e2ePositionsPerSecond!=1e11){std::cerr<<"FAIL duplicate or absent producer data updated calculation card\n";return 2;}
 packet["source_clock_ns"]="10600000000";
 packet["lines"]=QJsonArray{
  "[pgn-counters] time_ns=10400000000 positions=20000000000 gaps=2 tests=100 batches=1 processed_gaps=3",
  "[phase0-gpu] GAP merit=20.0 gap=800 lower=100 upper=900 verified=1"};
 pgn::mining::NativeBridgeTestAccess::send(bridge,packet);
 if(last.stale||*last.e2ePositionsPerSecond!=0||gaps!=1||!gapUnknown){std::cerr<<"FAIL raw zero or unknown discovery timestamp changed\n";return 3;}
 packet["lines"]=QJsonArray{};packet["observed_utc"]=now.addSecs(30).toString(Qt::ISODateWithMs);
 const int beforeEmpty=samples;const auto observed=last.observedUtc;
 pgn::mining::NativeBridgeTestAccess::send(bridge,packet);
 if(samples!=beforeEmpty||last.observedUtc!=observed){std::cerr<<"FAIL missing data replaced measurements or invented freshness\n";return 4;}
 pgn::mining::NativeBridgeTestAccess::send(bridge,QJsonObject{{"type","unavailable"},{"error","QA transport lost"}});
 if(samples!=beforeEmpty){std::cerr<<"FAIL transport loss reset the card\n";return 5;}
 packet["state"]="OFF";pgn::mining::NativeBridgeTestAccess::send(bridge,packet);
 if(samples!=beforeEmpty+1||last.raw.value("pgn_process_running").toBool()){std::cerr<<"FAIL physical stop acknowledgement suppressed by data guard\n";return 6;}
 const int beforeHistory=samples;const auto counterTime=last.observedUtc;
 pgn::mining::NativeBridgeTestAccess::send(bridge,QJsonObject{{"type","history"},{"rows",QJsonArray{
  QJsonObject{{"id",123},{"run",QString(32,'b')},{"p","100"},{"q","900"},{"gap","800"},{"merit","20.0"},{"unix_ts",1791676763}}}}});
 if(samples!=beforeHistory||last.observedUtc!=counterTime||gaps!=2){std::cerr<<"FAIL history replay changed current device freshness or was lost\n";return 8;}
 const QJsonObject row{{"type","history"},{"rows",QJsonArray{QJsonObject{{"id",124},{"run",QString(32,'b')},{"p","100"},{"q","900"},{"gap","800"},{"merit","20.0"}}}}};
 const QByteArray record=QJsonDocument(row).toJson(QJsonDocument::Compact)+'\n';
 QByteArray burst;for(int i=0;i<4000;++i)burst+=record;
 pgn::mining::NativeBridgeTestAccess::feed(bridge,burst.left(burst.size()-17));
 pgn::mining::NativeBridgeTestAccess::feed(bridge,burst.right(17));
 if(gaps!=4002||samples!=beforeHistory){std::cerr<<"FAIL valid large fragmented replay stream lost rows\n";return 9;}
 packet["driver_valid"]=true;
 packet["gpus"]=QJsonArray{"2026/10/11 07:34:39.692, GPU-raw, RTX 3090, 00000000:04:00.0, 100, 4096, 24576, 229.03, 350, 79, 1455, Active, Not Active, Not Active"};
 pgn::mining::NativeBridgeTestAccess::send(bridge,packet);
 if(resources!=1||hardware.devices.size()!=1||hardware.devices[0].smClockMhz!=1455.0||hardware.devices[0].throttleClass!="THERMAL"||!hardware.devices[0].throttleTelemetryAuthoritative||hardware.devices[0].throttleReasons.size()!=3){std::cerr<<"FAIL raw clock/active thermal reason lost\n";return 10;}
 packet["observed_utc"]=now.addSecs(31).toString(Qt::ISODateWithMs);
 packet["gpus"]=QJsonArray{"2026/10/11 07:34:40.692, GPU-raw, RTX 3090, 00000000:04:00.0, 100, 4096, 24576, 229.03, 350, 79, N/A, Active, N/A, Not Active"};
 pgn::mining::NativeBridgeTestAccess::send(bridge,packet);
 if(resources!=2||hardware.devices[0].smClockMhz||hardware.devices[0].throttleClass!="THERMAL"||!hardware.devices[0].throttleTelemetryAuthoritative||!hardware.devices[0].throttleDetail.contains("PARTIAL")){std::cerr<<"FAIL unavailable reason concealed known active thermal limit\n";return 11;}
 packet["driver_valid"]=false;packet["gpus"]=QJsonArray{};
 pgn::mining::NativeBridgeTestAccess::send(bridge,packet);
 if(resources!=2){std::cerr<<"FAIL missing driver sample invented hardware freshness\n";return 12;}
 pgn::mining::NativeBridge metadataBridge;FrontierTelemetrySnapshot metadata;
 metadataBridge.onDevice=[&](const auto& value){metadata=value;};
 int metadataRun=0;
 const auto checkBits=[&](const QString& start,const QString& end,std::optional<int> expected){
  pgn::mining::NativeBridgeTestAccess::send(metadataBridge,QJsonObject{{"type","snapshot"},{"state","RUNNING"},
   {"observed_utc",now.toString(Qt::ISODateWithMs)},
   {"manifest",QJsonObject{{"run",QString::number(++metadataRun)},{"start",start},{"end",end}}},{"lines",QJsonArray{}}});
  return metadata.numberBits==expected;
 };
 if(!checkBits("128","256",8)||!checkBits("000128","000256",8)||
    !checkBits("199999990000000000000","200000000000000000000",68)||
    !checkBits("128","257",{})||!checkBits("0","128",{})||
    !checkBits("256","128",{})||!checkBits("128","128",{})||
    !checkBits("invalid","256",{})||!checkBits(QString(161,'1'),"256",{})){
  std::cerr<<"FAIL exact exclusive range bit metadata or invalid-range guard\n";return 13;
 }
 std::cout<<"PASS raw counters/clock and thermal evidence, unknown dates, missing-data preservation, independent physical control\n";
}

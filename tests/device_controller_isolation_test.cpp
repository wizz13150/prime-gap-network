#include "mining/native_bridge.h"
#include <QCoreApplication>
#include <QDateTime>
#include <iostream>
namespace pgn::mining {
struct NativeBridgeTestAccess {
 static void send(NativeBridge& b,const QJsonObject& p){b.receive(p);}
 static void pending(NativeBridge& b,const QString& device,const QString& id,bool on){b.pending_.insert(id,{device,on});}
 static int pendingCount(const NativeBridge& b){return b.pending_.size();}
};
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);pgn::mining::NativeBridge bridge;
 const QString primary="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",test="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265";
 const auto utc=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
 int applied=0,samples=0;bridge.onControl=[&](const auto& device,const auto&,bool ok,const auto&){if(device!=test||!ok)std::abort();++applied;};
 bridge.onDevice=[&](const auto& lane){if(lane.gpuUuid==test){if(lane.e2ePositionsPerSecond||!lane.raw.value("pgn_test_only").toBool())std::abort();++samples;}};
 using Access=pgn::mining::NativeBridgeTestAccess;
 Access::pending(bridge,test,"test-on",true);
 Access::send(bridge,{{"type","snapshot"},{"state","RUNNING"},{"owned",true},{"observed_utc",utc},
  {"manifest",QJsonObject{{"run","11111111111111111111111111111111"}}}});
 if(applied||Access::pendingCount(bridge)!=1)return 1;
 QJsonObject snapshot{{"schema","pgn-device-control-observation-1"},{"device",test},{"scope","BOUNDED_SCIENCE_TEST"},
  {"observed_utc",utc},{"owned",true},{"stop_requested",false},{"desired_enabled",true},
  {"manifest",QJsonObject{{"uuid",test},{"pid",123},{"run","22222222222222222222222222222222"},
   {"image_sha256","A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43"},{"request_id","old-request"}}}};
 Access::send(bridge,{{"type","device_control_snapshot"},{"snapshot",snapshot}});
 if(applied||Access::pendingCount(bridge)!=1)return 6;
 auto currentEpoch=snapshot.value("manifest").toObject();currentEpoch.insert("request_id","test-on");snapshot.insert("manifest",currentEpoch);
 auto poison=snapshot;poison.insert("device",primary);Access::send(bridge,{{"type","device_control_snapshot"},{"snapshot",poison}});
 if(applied||samples)return 2;
 auto savedProduction=bridge.onDevice;
 int productionOverwrites=0;
 bridge.onDevice=[&](const FrontierTelemetrySnapshot&){++productionOverwrites;};
 bridge.onTestDevice=savedProduction;
 int modulePackets=0;bridge.onModuleRegistry=[&](const QJsonObject& packet){if(packet.value("request_id")!="module-inspect")std::abort();++modulePackets;};
 Access::send(bridge,{{"type","module_registry"},{"request_id","module-inspect"},{"state","RUNNING"},{"owned",true},{"observed_utc",utc},{"manifest",currentEpoch},{"raw",QJsonObject{{"pgn_e2e_positions_per_second",999.}}}});
 if(modulePackets!=1||productionOverwrites||applied||Access::pendingCount(bridge)!=1)return 10;
 bridge.moduleAction({{"action","module_list"},{"request_id","module-inspect"}});
 if(modulePackets!=2||productionOverwrites||applied||Access::pendingCount(bridge)!=1)return 11;
 Access::send(bridge,{{"type","device_control_snapshot"},{"snapshot",snapshot}});
 if(applied!=1||samples!=1||Access::pendingCount(bridge))return 3;
 Access::pending(bridge,test,"test-off",false);
 Access::send(bridge,{{"type","device_control_snapshot"},{"snapshot",snapshot}});
 if(applied!=1)return 4;
 snapshot.insert("owned",false);snapshot.insert("desired_enabled",false);
 Access::send(bridge,{{"type","device_control_snapshot"},{"snapshot",snapshot}});
 if(applied!=2||Access::pendingCount(bridge)||productionOverwrites)return 5;
 Access::pending(bridge,test,"already-finished",true);
 currentEpoch.insert("request_id","already-finished");snapshot.insert("manifest",currentEpoch);
 snapshot.insert("last_exit",QJsonObject{{"run","old-run"},{"exit_code",0}});
 Access::send(bridge,{{"type","device_control_snapshot"},{"snapshot",snapshot}});
 if(applied!=2||Access::pendingCount(bridge)!=1)return 7;
 snapshot.insert("last_exit",QJsonObject{{"run",currentEpoch.value("run")},{"exit_code",0}});
 Access::send(bridge,{{"type","device_control_snapshot"},{"snapshot",snapshot}});
 if(applied!=3||Access::pendingCount(bridge))return 8;
 int rejected=0;bridge.onControl=[&](const auto& device,const auto&,bool ok,const auto&){if(device!=test||ok)std::abort();++rejected;};
 Access::pending(bridge,test,"failed-finite",true);currentEpoch.insert("request_id","failed-finite");snapshot.insert("manifest",currentEpoch);
 snapshot.insert("last_exit",QJsonObject{{"run",currentEpoch.value("run")},{"exit_code",1}});
 Access::send(bridge,{{"type","device_control_snapshot"},{"snapshot",snapshot}});
 if(rejected!=1||Access::pendingCount(bridge)||productionOverwrites)return 9;
 std::cout<<"PASS isolated3060 acknowledgement;3090 cannot confirm it;no invented test rates;no hardware action\n";
}

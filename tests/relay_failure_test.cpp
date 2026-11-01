#include "mining/native_bridge.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

namespace pgn::mining {
struct NativeBridgeTestAccess {
 static void stage(NativeBridge& bridge){
  bridge.pending_.insert("pending-stop",{"test-device",false});
  bridge.pendingDispatch_.insert("pending-booking");
  bridge.receive(QJsonObject{{"type","history_begin"}});
  bridge.receive(QJsonObject{{"type","history_begin"}});
 }
 static void fail(NativeBridge& bridge){bridge.relayUnavailable("fixture transport loss");}
};
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);pgn::mining::NativeBridge bridge;
 int begin=0,end=0,controls=0,dispatches=0,measurements=0;
 bool unknown=true;QString status;
 bridge.onBatchBegin=[&]{++begin;};bridge.onBatchEnd=[&]{++end;};
 bridge.onDevice=[&](const auto&){++measurements;};
 bridge.onResources=[&](const auto&){++measurements;};
 bridge.onControl=[&](const auto&,const auto& id,bool applied,const auto& detail){
  ++controls;unknown=unknown&&!applied&&id=="pending-stop"&&detail.contains("outcome unconfirmed");};
 bridge.onDispatch=[&](const auto& id,bool accepted,const auto& detail){
  ++dispatches;unknown=unknown&&!accepted&&id=="pending-booking"&&detail.contains("outcome unconfirmed");};
 bridge.onStatus=[&](const auto& text){status=text;};
 pgn::mining::NativeBridgeTestAccess::stage(bridge);
 pgn::mining::NativeBridgeTestAccess::fail(bridge);
 pgn::mining::NativeBridgeTestAccess::fail(bridge);
 if(begin!=1||end!=1||controls!=1||dispatches!=1||measurements||!unknown||!status.contains("original measurements/age retained"))return 1;
 // Real failed startup; no mining command, zero sample or stuck UI batch.
 QTemporaryDir empty;if(!empty.isValid())return 2;bool observed=false;
 bridge.onStatus=[&](const auto& text){
  if(text.contains("RELAY UNAVAILABLE")){observed=true;QTimer::singleShot(0,&app,&QCoreApplication::quit);}
 };
 bridge.start(empty.path()+"/missing-node.exe","unused-script","unused-config");
 QTimer::singleShot(2000,&app,&QCoreApplication::quit);app.exec();
 if(!observed||measurements)return 3;
 std::cout<<"PASS relay startup failure and interrupted replay: exact-once uncertain requests, no synthetic measurements\n";
}

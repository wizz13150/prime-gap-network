#include "mining/native_bridge.h"
#include "mining/resource_age.h"
#include <QCoreApplication>
#include <QThread>
#include <QJsonArray>
#include <memory>
#include <iostream>
#include <stdexcept>
using pgn::mining::NativeBridge;
namespace pgn::mining {
struct NativeBridgeTestAccess {
 static bool send(NativeBridge& bridge,const QByteArray& bytes){return bridge.writeCommand(bytes);}
 static bool available(const NativeBridge& bridge){return bridge.runtimeAvailable();}
 static bool noChild(const NativeBridge& bridge){return bridge.relay_.state()==QProcess::NotRunning;}
};
}
using Access=pgn::mining::NativeBridgeTestAccess;
namespace {
int checks=0;
void check(bool condition,const char* label){++checks;if(!condition)throw std::runtime_error(label);}
void events(){QCoreApplication::processEvents();}
const QString id="00000001-0001-4001-8001-000000000001",nonce="00000002-0002-4002-8002-000000000002";
const QString device="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265";
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 if(app.arguments().size()==3&&app.arguments().at(1)=="--utc-probe"){
  const auto parsed=QDateTime::fromString(app.arguments().at(2),Qt::ISODateWithMs);
  std::cout<<"valid="<<parsed.isValid()<<" utc="<<parsed.toUTC().toString(Qt::ISODateWithMs).toStdString()<<" age_ms="<<parsed.msecsTo(QDateTime::currentDateTimeUtc())<<'\n';return parsed.isValid()?0:1;
 }
 try{
  {
   QObject runtime;NativeBridge bridge;int resources=0,devices=0,controls=0,sources=0;
   FrontierResourceTelemetrySnapshot actual;
   bridge.attachNativeRuntime(&runtime,[](const auto&){return true;});
   bridge.onResources=[&](const auto& sample){++resources;actual=sample;};
   bridge.onDevice=[&](const auto&){++devices;};
   bridge.onControl=[&](const auto&,const auto&,bool,const auto&){++controls;};
   bridge.onSourceSnapshot=[&](const auto&){++sources;};
   const auto original=QDateTime::currentDateTimeUtc().addSecs(-3600);
   QJsonObject packet{{"type","resource_snapshot"},{"driver_valid",true},
    {"observed_utc",original.toString(Qt::ISODateWithMs)},
    {"gpus",QJsonArray{"0, GPU-fixture, CLOSED FIXTURE, bus, 0, 123, 456, 24.5, 300, 55, 210, Not Active, Not Active, Not Active"}}};
   bridge.acceptNativeRuntimePacket(&runtime,packet);
   check(resources==1&&actual.devices.size()==1&&actual.stale&&actual.observedUtc==original,"OFF resource sample preserves original date and stale age");
   check(actual.devices[0].utilizationPercent==0.&&actual.devices[0].powerWatts==24.5&&actual.devices[0].temperatureC==55.,"real idle zero and hardware measurements conserved");
   check(devices==0&&controls==0&&sources==0,"resources never become producer source/control acknowledgements");
   const auto future=QDateTime::currentDateTimeUtc().addSecs(3600);
   packet["observed_utc"]=future.toString(Qt::ISODateWithMs);
   const auto received=pgn::mining::receiptMonotonicNs();
   bridge.acceptNativeRuntimePacket(&runtime,packet,received);
   check(!actual.stale&&actual.observedUtc==future&&actual.receivedMonotonicNs==received,"typed local receipt decouples foreign clock without rewriting source UTC");
   check(pgn::mining::resourceReceiptAgeMs(actual,received+3000000000LL,future.addDays(1))==3000,"local receipt age ignores a wall-clock jump");
   check(!pgn::mining::resourceReceiptAgeMs(actual,received-1,future),"future local receipt has no valid age");
   bridge.acceptNativeRuntimePacket(&runtime,packet,received-16000000000LL);
   check(actual.stale,"queue-delayed hardware remains stale; delivery never restamps receipt");
   bridge.acceptNativeRuntimePacket(&runtime,packet,received+3600000000000LL);
   check(actual.stale,"invalid future steady-clock witness cannot mint freshness");
   bridge.acceptNativeRuntimePacket(&runtime,packet,-1);
   check(actual.stale,"negative steady-clock witness cannot mint freshness");
   packet["received_monotonic_ns"]=QString::number(received);
   bridge.acceptNativeRuntimePacket(&runtime,packet);
   check(actual.stale&&!actual.receivedMonotonicNs,"JSON cannot forge the typed local receipt capability");
   check(devices==0&&controls==0&&sources==0,"clock-domain repair creates no producer or control acknowledgement");
   const auto beforeInvalid=resources;
   packet["driver_valid"]=false;bridge.acceptNativeRuntimePacket(&runtime,packet);
   packet["driver_valid"]=true;packet["observed_utc"]="";bridge.acceptNativeRuntimePacket(&runtime,packet);
   packet["observed_utc"]=original.toString(Qt::ISODateWithMs);packet["gpus"]=QJsonArray{};bridge.acceptNativeRuntimePacket(&runtime,packet);
   check(resources==beforeInvalid,"missing/invalid hardware never clears inventory or fabricates current zeros");
  }
  {
   QObject runtime,foreign;NativeBridge bridge;QList<QJsonObject> sent;int inventories=0,controls=0;
   check(!bridge.attachNativeRuntime(nullptr,[](const auto&){return true;}),"null owner refused");
   check(!bridge.attachNativeRuntime(&bridge,[](const auto&){return true;}),"self owner refused");
   check(!bridge.attachNativeRuntime(&runtime,{}),"empty sink refused");
   check(bridge.attachNativeRuntime(&runtime,[&](const auto& packet){sent.append(packet);return true;}),"runtime attached");
   check(!bridge.attachNativeRuntime(&foreign,[](const auto&){return true;}),"second writer refused");
   check(Access::available(bridge)&&Access::noChild(bridge),"attachment is local no child");
   bridge.onControl=[&](const auto&,const auto&,bool applied,const auto&){check(!applied,"no invented physical ack");++controls;};
   bridge.onApplicationStartInventory=[&](const auto& request,const auto& packet){check(request==id&&packet.value("inventory_id")==nonce,"inventory correlated");++inventories;};
   check(bridge.inspectApplicationStart(id,nonce),"inventory request queued");
   check(sent.isEmpty()&&inventories==0&&controls==0,"submission cannot reenter or acknowledge");events();
   check(sent.size()==1&&sent[0].value("action")=="application_start_inventory","same real bridge action");
   const QJsonObject inventory{{"type","application_start_inventory"},{"request_id",id},{"inventory_id",nonce},{"accepted",true},{"snapshot",QJsonObject{}}};
   check(!bridge.acceptNativeRuntimePacket(&foreign,inventory)&&inventories==0,"foreign runtime cannot inject result");
   auto wrong=inventory;wrong["inventory_id"]=id;
   check(bridge.acceptNativeRuntimePacket(&runtime,wrong)&&inventories==0,"stale nonce cannot answer request");
   check(bridge.acceptNativeRuntimePacket(&runtime,inventory)&&inventories==1&&controls==0,"inventory is not physical launch");
   check(bridge.acceptNativeRuntimePacket(&runtime,inventory)&&inventories==1,"duplicate inventory ignored");
   check(!Access::send(bridge,"[]\n")&&!Access::send(bridge,"{broken"),"invalid local envelopes refused");
   check(!Access::send(bridge,QByteArray(524289,' ')),"command record bound");
   int frozen=0,closeInventories=0;
   // Channel loss at scope teardown can legitimately report an unconfirmed
   // close. Never throw from a QObject destruction callback.
   bridge.onApplicationCloseFrozen=[&](const auto& request,bool accepted,const auto&){if(request==id&&accepted)++frozen;};
   bridge.onApplicationCloseInventory=[&](const auto&,const auto&){++closeInventories;};
   check(bridge.prepareApplicationClose(id),"native close freezes local admission");
   check(!bridge.inspectApplicationStart(nonce,id),"close prevents fresh initial admission");events();
   check(sent.back().value("action")=="application_close_prepare"&&frozen==0,"close posted not applied");
   bridge.acceptNativeRuntimePacket(&runtime,{{"type","application_close_frozen"},{"request_id",nonce},{"accepted",true}});
   check(frozen==0,"foreign close request ignored");
   bridge.acceptNativeRuntimePacket(&runtime,{{"type","application_close_frozen"},{"request_id",id},{"accepted",true}});
   check(frozen==1&&controls==0,"freeze not stop proof");
   check(bridge.inspectApplicationClose(id,nonce),"close readback queued");events();
   check(sent.back().value("action")=="application_close_inventory","same native close inventory action");
   bridge.acceptNativeRuntimePacket(&runtime,{{"type","application_close_inventory"},{"request_id",id},{"inventory_id",id}});
   check(closeInventories==0,"wrong close nonce ignored");
   bridge.acceptNativeRuntimePacket(&runtime,{{"type","application_close_inventory"},{"request_id",id},{"inventory_id",nonce}});
   check(closeInventories==1&&Access::noChild(bridge),"readback packet still no process launch");
  }
  {
   NativeBridge bridge;auto runtime=std::make_unique<QObject>();int sent=0,failed=0;
   bridge.onApplicationStartInventory=[&](const auto&,const auto& packet){check(!packet.value("accepted").toBool(),"destroyed channel unconfirmed");++failed;};
   bridge.attachNativeRuntime(runtime.get(),[&](const auto&){++sent;return true;});
   check(bridge.inspectApplicationStart(id,nonce),"request before owner loss");runtime.reset();events();
   check(sent==0&&failed==1&&!Access::available(bridge),"owner loss cancels queued send without retry");
   check(!bridge.inspectApplicationStart(id,nonce)&&sent==0,"lost runtime cannot resubmit silently");
  }
  {
   QObject runtime;auto bridge=std::make_unique<NativeBridge>();int sent=0;
   bridge->attachNativeRuntime(&runtime,[&](const auto&){++sent;return true;});
   bridge->inspectApplicationStart(id,nonce);bridge.reset();events();check(sent==0,"bridge loss drops queued request");
  }
  for(bool throws:{false,true}){
   QObject runtime;NativeBridge bridge;int sent=0;QString status;
   bridge.onStatus=[&](const auto& text){status=text;};
   bridge.attachNativeRuntime(&runtime,[&](const auto&)->bool{++sent;if(throws)throw std::runtime_error("own fixture");return false;});
   check(Access::send(bridge,"{\"action\":\"fixture\"}\n"),"sink submission queued only");events();
   check(sent==1&&!Access::available(bridge)&&!status.isEmpty(),"failed sink poisons channel, not prepared history");
   check(!Access::send(bridge,"{\"action\":\"fixture\"}\n")&&sent==1,"no automatic retry");
   check(!bridge.attachNativeRuntime(&runtime,[](const auto&){return true;}),"cannot replace uncertain writer in place");
  }
  {
   QObject runtime;auto bridge=std::make_unique<NativeBridge>();
   bridge->attachNativeRuntime(&runtime,[&](const auto&){bridge.reset();return false;});
   check(Access::send(*bridge,"{\"action\":\"fixture\"}\n"),"deletion callback queued");events();
   check(!bridge,"sink may destroy bridge without use after free");
  }
  {
   QThread worker;QObject runtime;NativeBridge bridge;worker.start();runtime.moveToThread(&worker);
   check(!bridge.attachNativeRuntime(&runtime,[](const auto&){return true;}),"foreign-thread owner refused");
   QMetaObject::invokeMethod(&runtime,[&]{runtime.moveToThread(app.thread());},Qt::BlockingQueuedConnection);
   check(bridge.attachNativeRuntime(&runtime,[](const auto&){return true;}),"creating-thread owner accepted");
   runtime.moveToThread(&worker);
   check(!Access::available(bridge)&&!Access::send(bridge,"{}"),"owner thread invariant retained after attach");
   QMetaObject::invokeMethod(&runtime,[&]{runtime.moveToThread(app.thread());},Qt::BlockingQueuedConnection);worker.quit();worker.wait();
  }
  std::cout<<"native bridge runtime port checks="<<checks<<" failures=0; in-process only, NO Node/SSH/GPU/child or frontend launch\n";return 0;
 }catch(const std::exception& e){std::cerr<<"native runtime port failed after "<<checks<<": "<<e.what()<<'\n';return 1;}
}

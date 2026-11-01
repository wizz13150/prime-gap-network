#include "mining/native_bridge.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace pgn::mining {
struct NativeBridgeTestAccess {
 static QProcess& process(NativeBridge& bridge){return bridge.relay_;}
 static void receive(NativeBridge& bridge,const QJsonObject& packet){bridge.receive(packet);}
};
}
using namespace pgn::mining;
namespace {
int checks{};
const QString id="11111111-2222-4333-8444-555555555555",other="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
const QString inventoryId="22222222-3333-4444-8555-666666666666";
const QString device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void emitPacket(const QJsonObject& packet){const auto bytes=QJsonDocument(packet).toJson(QJsonDocument::Compact)+'\n';std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);std::fflush(stdout);}
int self(const QString& path){
 QFile input;if(!input.open(stdin,QIODevice::ReadOnly))return 10;
 QJsonArray actions;
 const auto data=input.readAll();
 for(const auto& line:data.split('\n')){
  if(line.isEmpty())continue;
  const auto command=QJsonDocument::fromJson(line).object();actions.append(command);
  if(command["action"]=="application_close_prepare")emitPacket({{"type","application_close_frozen"},{"request_id",command["request_id"]},{"accepted",true}});
  if(command["action"]=="application_close_inventory")emitPacket({{"type","application_close_inventory"},{"request_id",command["request_id"]},{"inventory_id",command["inventory_id"]},{"accepted",false},{"inventory_known",false},{"error","SELF deliberately unknown, not stopped"}});
 }
 QFile audit(path);if(!audit.open(QIODevice::WriteOnly))return 11;
 return audit.write(QJsonDocument(actions).toJson(QJsonDocument::Compact))>0?0:12;
}
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);const auto args=app.arguments();
 if(args.size()==3&&args[1]=="--self-application-close-bridge")return self(args[2]);
 try{
  NativeBridge bridge;QTemporaryDir temp;require(temp.isValid(),"own SELF audit directory");
  QJsonArray frozen,inventory;QStringList refusedStarts,dispatches,moduleErrors;int deviceSamples=0;bool noPhysicalAck=true,noDispatchAccepted=true;QString callbackError;
  bridge.onApplicationCloseFrozen=[&](const QString& request,bool accepted,const QString& error){frozen.append(QJsonObject{{"id",request},{"accepted",accepted},{"error",error}});};
  bridge.onApplicationCloseInventory=[&](const QString&,const QJsonObject& packet){inventory.append(packet);};
  bridge.onControl=[&](const QString&,const QString& request,bool applied,const QString&){noPhysicalAck=noPhysicalAck&&!applied;refusedStarts.append(request);};
  bridge.onDispatch=[&](const QString& request,bool accepted,const QString&){noDispatchAccepted=noDispatchAccepted&&!accepted;dispatches.append(request);};
  bridge.onModuleRegistry=[&](const QJsonObject& packet){if(packet.contains("error"))moduleErrors.append(packet["request_id"].toString());};
  bridge.onDevice=[&](const auto&){++deviceSamples;};
  require(!bridge.prepareApplicationClose("bad"),"invalid close ID rejected without starting process");
  require(!bridge.prepareApplicationClose(id),"unavailable runtime is not a successful freeze");
  QEventLoop loop;bool finished=false;
  QObject::connect(&NativeBridgeTestAccess::process(bridge),&QProcess::started,&app,[&]{
   try{
   require(bridge.prepareApplicationClose(id),"close UUID sends one admission freeze request");
   require(!bridge.prepareApplicationClose(other),"different close generation cannot replace active request");
   NativeBridgeTestAccess::receive(bridge,{{"type","application_close_frozen"},{"request_id",other},{"accepted",true}});
   NativeBridgeTestAccess::receive(bridge,{{"type","application_close_inventory"},{"request_id",other},{"accepted",true}});
   require(inventory.isEmpty(),"stale inventory callback ignored");
   bridge.control(device,"blocked-start",true);bridge.dispatch("blocked-booking");
   bridge.moduleAction({{"action","module_select"},{"request_id","blocked-module"}});
   bridge.moduleAction({{"action","module_list"},{"request_id","permitted-read"}});
   bridge.control(device,"permitted-stop",false);
   require(!bridge.inspectApplicationClose(other,inventoryId)&&!bridge.inspectApplicationClose(id,"invalid")&&bridge.inspectApplicationClose(id,inventoryId),"inventory request requires same close epoch and canonical read nonce");
   NativeBridgeTestAccess::receive(bridge,{{"type","application_close_inventory"},{"request_id",id},{"inventory_id",other},{"accepted",true}});
   require(inventory.isEmpty(),"old read nonce is not a fresh inventory callback");
   NativeBridgeTestAccess::process(bridge).closeWriteChannel();
   }catch(const std::exception& e){callbackError=QString::fromUtf8(e.what());NativeBridgeTestAccess::process(bridge).closeWriteChannel();}
  });
  QObject::connect(&NativeBridgeTestAccess::process(bridge),qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&app,[&](int code,QProcess::ExitStatus status){finished=code==0&&status==QProcess::NormalExit;loop.quit();});
  QTimer timeout;timeout.setSingleShot(true);QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);timeout.start(8000);
  bridge.start(QCoreApplication::applicationFilePath(),"--self-application-close-bridge",temp.filePath("stdin.json"),false,false);loop.exec();
  require(finished,"own SELF subprocess finishes normally");
  require(callbackError.isEmpty(),"all callback admission checks passed without throwing through Qt");
  require(noPhysicalAck&&noDispatchAccepted,"informational close callbacks never invent physical ACK or accept dispatch");
  require(refusedStarts.contains("blocked-start")&&dispatches==QStringList{"blocked-booking"}&&moduleErrors.contains("blocked-module"),"all new mutation refusals are correlated locally");
  require(inventory.size()==1&&!inventory[0].toObject()["inventory_known"].toBool()&&!deviceSamples,"unknown inventory remains unknown and does not fabricate telemetry");
  bool acceptedFreeze=false;for(const auto item:frozen)if(item.toObject()["id"]==id&&item.toObject()["accepted"].toBool())acceptedFreeze=true;
  require(acceptedFreeze,"matching backend freeze is delivered without asserting a stop");
  QFile audit(temp.filePath("stdin.json"));require(audit.open(QIODevice::ReadOnly),"read own SELF stdin audit");
  const auto actions=QJsonDocument::fromJson(audit.readAll()).array();QStringList names;
  for(const auto action:actions){const auto object=action.toObject();names.append(object["action"].toString());require(object["action"]!="start"&&object["action"]!="dispatch"&&object["action"]!="module_select","frozen mutations send ZERO stdin bytes");}
  require(names.count("application_close_prepare")==1&&names.count("application_close_inventory")==1&&names.count("stop")==1&&names.count("module_list")==2,"only freeze, explicit inventory, STOP and reads cross the local pipe");
  std::cout<<"Native application close bridge: "<<checks<<" checks / 0 failures; REAL own QProcess SELF, NO SSH/Node/GPU/UI/controller/quit\n";return 0;
 }catch(const std::exception& e){std::cerr<<"Native application close bridge failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}

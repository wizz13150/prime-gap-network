#include "mining/native_bridge.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <QTimeZone>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace pgn::mining {
struct NativeBridgeTestAccess {
 static QProcess& process(NativeBridge& bridge){return bridge.relay_;}
 static bool observer(const NativeBridge& bridge){return bridge.observationOnly_;}
 static bool pendingEmpty(const NativeBridge& bridge){return bridge.pending_.isEmpty()&&bridge.pendingDispatch_.isEmpty();}
 static QDateTime counterTime(const NativeBridge& bridge){return bridge.lastCounters_;}
 static void receive(NativeBridge& bridge,const QJsonObject& packet){bridge.receive(packet);}
};
}
using namespace pgn::mining;
namespace {
int checks=0;
const QString device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
const QString testDevice="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265";
const QString run(32,'a');
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
void output(const QJsonObject& object){
 const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n';
 if(std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout)!=size_t(bytes.size())||std::fflush(stdout)!=0)throw std::runtime_error("SELF output failed");
}
QJsonObject snapshot(bool duplicate=false){
 return {{"type","snapshot"},{"schema","pgn-runtime-snapshot-1"},{"state","RUNNING"},
  {"observed_utc",duplicate?"2026-10-14T00:00:23.000Z":"2026-10-14T00:00:03.000Z"},
  {"source_clock_ns","3500000000"},{"pending_bytes",0},
  {"manifest",QJsonObject{{"run",run},{"uuid",device},{"start","1000"},{"end","2000"},{"birth","2026-10-13T23:00:00.000Z"}}},
  {"lines",QJsonArray{
   "[pgn-counters] time_ns=1000000000 positions=10 gaps=1 tests=100 batches=1 processed_gaps=1",
   "[pgn-counters] time_ns=2000000000 positions=30 gaps=2 tests=160 batches=2 processed_gaps=3"}}};
}
int fixture(bool observer){
 // SELF ONLY, not SSH/Node/a hardware controller. The test closes this owned
 // stdin pipe; capture every byte before replying and exiting normally.
 const auto args=QCoreApplication::arguments();if(args.size()<3)return 10;
 output(snapshot());QByteArray input;char bytes[1024];
 while(const auto n=std::fread(bytes,1,sizeof(bytes),stdin)){input.append(bytes,qsizetype(n));if(input.size()>8192)return 11;}
 if(std::ferror(stdin))return 12;
 QJsonArray arguments;for(const auto& arg:args.mid(1))arguments.append(arg);
 QFile audit(args[2]);if(!audit.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 13;
 const auto record=QJsonDocument(QJsonObject{{"argv",arguments},{"stdin",QString::fromUtf8(input)}}).toJson(QJsonDocument::Compact);
 if(audit.write(record)!=record.size()||!audit.flush())return 14;audit.close();
 output(snapshot(true));
 if(observer){
  output({{"type","dispatch"},{"booking_id","observer-booking"},{"accepted",true},{"detail","UNSOLICITED SELF REPLY"}});
  output({{"type","control"},{"request_id","observer-start"},{"result",QJsonObject{}},{"error","UNSOLICITED SELF REPLY"}});
 }else{
  for(const auto& line:input.split('\n')){
   if(line.isEmpty())continue;const auto command=QJsonDocument::fromJson(line).object();
   if(command.value("action")=="dispatch")output({{"type","dispatch"},{"booking_id",command.value("booking_id")},{"accepted",true},{"detail","SELF correlated dispatch receipt"}});
   if(command.value("action")=="module_list")output({{"type","module_registry"},{"request_id",command.value("request_id")},{"applied",true},{"fixture_only",true}});
  }
 }
 return 0;
}
struct Control {QString device,id;bool applied;QString detail;};
struct Dispatch {QString id;bool accepted;QString detail;};
void scenario(bool observer,const QString& audit){
 NativeBridge bridge;auto& process=NativeBridgeTestAccess::process(bridge);QEventLoop loop;QTimer watchdog;watchdog.setSingleShot(true);
 QList<Control> controls;QList<Dispatch> dispatches;QList<QJsonObject> modules;QList<FrontierTelemetrySnapshot> samples;
 bool issued=false,finished=false,timedOut=false,activeUnchanged=false;int exit=-1;QProcess::ExitStatus exitStatus=QProcess::CrashExit;
 bridge.onControl=[&](const auto& dev,const auto& id,bool applied,const auto& detail){controls.append({dev,id,applied,detail});};
 bridge.onDispatch=[&](const auto& id,bool accepted,const auto& detail){dispatches.append({id,accepted,detail});};
 bridge.onModuleRegistry=[&](const auto& packet){modules.append(packet);};
 bridge.onDevice=[&](const auto& sample){
  samples.append(sample);if(issued)return;issued=true;
  const auto pid=process.processId();const auto program=process.program();const auto arguments=process.arguments();
  // Refusal while active must not kill, restart, switch capability, or even
  // replace the program/arguments of this same SELF process.
  const bool rejectedObserver=!bridge.startNativeObserver(QCoreApplication::applicationFilePath(),{"--self-native-observer","UNUSED-ACTIVE-PATH"});
  if(observer){
   bridge.start(QCoreApplication::applicationFilePath(),"--self-legacy-relay","UNUSED-ACTIVE-PATH",true,true);
   bridge.control(device,"observer-start",true);bridge.control(testDevice,"observer-test-stop",false);
   bridge.dispatch("observer-booking");
   for(const auto& action:QStringList{"module_list","module_register","module_select","module_receipt"})
    bridge.moduleAction({{"action",action},{"request_id","observer-"+action}});
   bridge.control(device,"observer-after-node-attempt",true);
  }else{
   // residentViewer must retain the OLD Node command contract; it is not the
   // new local observation-only mode.
   bridge.control(device,"legacy-start",true);bridge.dispatch("legacy-booking");
   bridge.moduleAction({{"action","module_list"},{"request_id","legacy-module"}});
  }
  activeUnchanged=rejectedObserver&&NativeBridgeTestAccess::observer(bridge)==observer&&process.processId()==pid&&pid>0&&
   process.state()==QProcess::Running&&process.program()==program&&process.arguments()==arguments;
  process.closeWriteChannel();
 };
 QObject::connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&loop,[&](int code,QProcess::ExitStatus status){finished=true;exit=code;exitStatus=status;loop.quit();});
 QObject::connect(&watchdog,&QTimer::timeout,&loop,[&]{timedOut=true;process.closeWriteChannel();loop.quit();});
 const auto self=QCoreApplication::applicationFilePath();
 if(observer)require(bridge.startNativeObserver(self,{"--self-native-observer",audit}),"explicit observer launch attempt accepted");
 else bridge.start(self,"--self-legacy-relay",audit,true,true);
 watchdog.start(8000);if(!finished)loop.exec();watchdog.stop();
 if(!finished){process.closeWriteChannel();process.waitForFinished(1000);} // only own SELF fixture, never a runtime PID
 require(!timedOut&&finished&&exit==0&&exitStatus==QProcess::NormalExit,"owned SELF fixture exits normally, no service stopped");
 require(issued&&activeUnchanged,"active launch refusals preserve exact process identity/argv/mode");
 require(NativeBridgeTestAccess::pendingEmpty(bridge),"no stranded control or dispatch pending after fixture");
 require(samples.size()==1&&samples[0].runId==run&&samples[0].gpuUuid==device,"real bridge receives RUNNING and one admitted interval, not duplicate freshness");
 const auto& sample=samples[0];
 require(sample.rowsCompleted=="30"&&sample.e2ePositionsPerSecond&&*sample.e2ePositionsPerSecond==20.0,"source counters and native adjacent interval remain exact");
 require(sample.powmPerSecond&&*sample.powmPerSecond==60.0,"source test-rate interval is preserved");
 const auto expectedTime=QDateTime::fromString("2026-10-14T00:00:01.500Z",Qt::ISODateWithMs);
 require(sample.observedUtc==expectedTime&&NativeBridgeTestAccess::counterTime(bridge)==expectedTime,"duplicate later packet does not retime the producer counter observation");
 require(sample.raw.value("pgn_age_at_receipt_ns")=="1500000000"&&sample.raw.value("pgn_clock_namespace")=="native-source-clock/"+run,"source age lower-bound and clock namespace preserved");
 require(sample.raw.value("pgn_receipt_monotonic_ns").toString().toLongLong()>0,"receipt uses real monotonic clock, not a fabricated producer timestamp");
 QFile file(audit);require(file.open(QIODevice::ReadOnly),"SELF audit exists only in own temporary directory");
 const auto record=QJsonDocument::fromJson(file.readAll()).object();const auto input=record.value("stdin").toString().toUtf8();
 if(observer){
  require(input.isEmpty(),"observer sends ZERO stdin bytes, including no automatic module_list");
  require(controls.size()==3,"three observer controls receive exactly one refusal each, never later RUNNING acknowledgement");
  QSet<QString> ids;for(const auto& result:controls){ids.insert(result.id);require(!result.applied&&(result.detail.contains("not submitted")||result.detail.contains("requires native admission")),"observer control refusal is explicit and never applied");}
  require(ids==QSet<QString>{"observer-start","observer-test-stop","observer-after-node-attempt"},"control request correlations are preserved");
  require(dispatches.size()==1&&dispatches[0].id=="observer-booking"&&!dispatches[0].accepted,"unsolicited accepted dispatch cannot override local correlated refusal");
  require(modules.size()==4,"no automatic module preload callback in observer mode");
  QSet<QString> moduleIds;for(const auto& result:modules){moduleIds.insert(result.value("request_id").toString());require(result.value("applied").isBool()&&!result.value("applied").toBool()&&result.contains("error"),"observer module service refusal is explicit");}
  require(moduleIds==QSet<QString>{"observer-module_list","observer-module_register","observer-module_select","observer-module_receipt"},"all observer module request IDs retained");
  require(record.value("argv").toArray()==QJsonArray{"--self-native-observer",audit},"native observer uses exact separate argv");
 }else{
  QList<QJsonObject> commands;for(const auto& line:input.split('\n'))if(!line.isEmpty())commands.append(QJsonDocument::fromJson(line).object());
  // The later native-admission guard deliberately retired naked productive
  // START, also for the legacy relay. Never weaken that production guard to
  // preserve this earlier fixture's obsolete expectation.
  require(commands.size()==3,"legacy relay cannot bypass mandatory native start admission");
  require(commands[0].value("action")=="module_list"&&commands[0].value("request_id")=="initial-module-catalogue","legacy automatic module_list is unchanged");
  require(commands[1].value("action")=="dispatch"&&commands[1].value("booking_id")=="legacy-booking","legacy booking command unchanged");
  require(commands[2].value("request_id")=="legacy-module","legacy module request unchanged");
  require(controls.size()==1&&!controls[0].applied&&controls[0].id=="legacy-start"&&controls[0].detail.contains("requires native admission"),"legacy naked START explicitly refused without a physical-control command");
  require(dispatches.size()==1&&dispatches[0].accepted&&dispatches[0].id=="legacy-booking","legacy correlated dispatch reply remains received");
  require(modules.size()==2,"legacy preload and explicit module replies remain received");
  require(record.value("argv").toArray()==QJsonArray{"--self-legacy-relay",audit,"--indexed-native","--stream-native","--resident-viewer"},"legacy residentViewer flags remain exact, no automatic-advance flag added");
 }
}
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 try{
  const auto args=app.arguments();if(args.size()>1&&args[1]=="--self-native-observer")return fixture(true);
  if(args.size()>1&&args[1]=="--self-legacy-relay")return fixture(false);
  {
   NativeBridge bridge;QString notice;int controls=0,devices=0;
   bridge.onStatus=[&](const QString& detail){notice=detail;};
   bridge.onControl=[&](const auto&,const auto&,bool,const auto&){++controls;};
   bridge.onDevice=[&](const auto&){++devices;};
   NativeBridgeTestAccess::receive(bridge,{{"type","status"},{"detail","Manual reservations complete · automatic continuation OFF"}});
   require(notice=="Manual reservations complete · automatic continuation OFF","manual drain has visible informational feedback");
   require(!controls&&!devices&&!NativeBridgeTestAccess::counterTime(bridge).isValid(),"status never acknowledges control, invents telemetry or renews original age");
   NativeBridgeTestAccess::receive(bridge,{{"type","status"},{"detail",QString(1000,'x')}});
   require(notice.size()==400,"informational detail has bounded display size");
  }
  QTemporaryDir temporary;require(temporary.isValid(),"own temporary fixture directory");
  scenario(true,temporary.filePath("observer-audit.json"));scenario(false,temporary.filePath("legacy-audit.json"));
  std::cout<<"Native observer bridge: "<<checks<<" checks / 0 failures; REAL QProcess SELF, QtCore only, ZERO real SSH/Node/GPU/UI/control\n";return 0;
 }catch(const std::exception& e){std::cerr<<"Native observer bridge failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}

#include "mining/cpu_normal_resources.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#include <iostream>
#include <stdexcept>
using namespace pgn::mining;
static void check(bool okay,const char* why){if(!okay)throw std::runtime_error(why);}
static bool until(std::function<bool()> predicate,int ms=5000){QElapsedTimer time;time.start();while(time.elapsed()<ms){QCoreApplication::processEvents();if(predicate())return true;QThread::msleep(2);}return false;}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
 check(argc==4,"python/fixture/workspace arguments required");const QString python=QString::fromLocal8Bit(argv[1]),fixture=QString::fromLocal8Bit(argv[2]),root=QString::fromLocal8Bit(argv[3]);
 QCoreApplication::setOrganizationName("PGN-Synthetic-Tests");QCoreApplication::setApplicationName("cpu-normal-resources");
 QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
 CpuNormalResources resources;QHash<QString,QJsonObject> statuses;int controls=0;
 resources.onTelemetry=[&](auto device,auto inventory,auto state,bool connected){if(connected){check(inventory.value("host").toString()==state.value("fixture_host").toString(),"CPU host identity crossed");statuses[device]=state;}};
 resources.onControl=[&](auto,auto,bool okay,auto){check(okay,"resource control refused");++controls;};
 for(const auto& host:QStringList{"ALEX","WIZZ"}){CpuControlEndpoint endpoint;endpoint.deviceId="CPU/"+host+"/0";endpoint.host=host;endpoint.program=python;endpoint.arguments={"-I","-X","utf8",fixture,"--host",host,"--workspace",root+"/"+host};resources.add(endpoint);}
 check(until([&]{return statuses.size()==2;}),"two CPU services did not become ready");
 QProcess miner;miner.start(python,{"-I","-X","utf8",fixture,"--echo"});check(miner.waitForStarted(3000),"closed miner fixture failed");resources.attachMiner(&miner);
 QByteArray buffer;QList<QJsonObject> packets;
 const auto drain=[&]{buffer+=miner.readAllStandardOutput();while(buffer.contains('\n')){const auto end=buffer.indexOf('\n');packets.append(QJsonDocument::fromJson(buffer.left(end)).object());buffer.remove(0,end+1);}};
 check(resources.control("CPU/ALEX/0","alex-on",true,50),"ALEX control missing");check(resources.control("CPU/WIZZ/0","wizz-on",true,25),"WIZZ control missing");
 check(until([&]{drain();return packets.size()==2;}),"normal-miner controls were not forwarded");const auto configured=packets;packets.clear();
 for(const auto& command:configured){const auto device=command.value("device_id").toString();check(command.value("action")=="cpu_configure","CPU control bypassed normal miner");auto body=command;body.remove("action");body.remove("id");body.remove("device_id");body.remove("inventory");body.insert("action","configure");body.insert("generation",1);resources.packet(&miner,{{"schema","PGN_HORIZON_CPU_COMMAND_V1"},{"device_id",device},{"bridge_id",device},{"command",body}});}
 check(until([&]{drain();return packets.size()==2;}),"two owned-service replies missing");
 for(const auto& reply:packets){const auto device=reply.value("device_id").toString(),host=device.split('/').at(1);const auto state=reply.value("response").toObject().value("status").toObject();check(state.value("fixture_host")==host,"wrong CPU host answered");check(state.value("fixture_workspace").toString().replace('\\','/').contains('/'+host+'/'),"wrong CPU workspace answered");const auto id=host=="ALEX"?"alex-on":"wizz-on";resources.packet(&miner,{{"schema","PGN_HORIZON_CPU_NORMAL_EVENT_V1"},{"device_id",device},{"type","control"},{"id",id},{"applied",true},{"status",state},{"resource_enabled",true}});}
 check(controls==2,"resource controls were not acknowledged independently");packets.clear();
 const QJsonObject request{{"schema","PGN_CPU_CYCLE_WORK_V1"},{"work_cycle",QString(64,'a')},{"normal_ticket",QString(64,'b')},{"stage","search"},{"ticket_id",QString(64,'b')},{"start","147573952589676412928"},{"end","147573952589676422928"}};
 for(const auto& host:QStringList{"ALEX","WIZZ"})resources.packet(&miner,{{"schema","PGN_HORIZON_CPU_COMMAND_V1"},{"device_id","CPU/"+host+"/0"},{"bridge_id","work-"+host},{"command",QJsonObject{{"action","execute"},{"request",request}}}});
 check(until([&]{return QFile::exists(root+"/ALEX/normal-cycle-v2/owned-request.json")&&QFile::exists(root+"/WIZZ/normal-cycle-v2/owned-request.json");}),"cycle work did not reach distinct host workspaces");
 check(resources.requestClose()||until([&]{return resources.allStopped();}),"CPU services failed to drain/close");miner.closeWriteChannel();check(miner.waitForFinished(3000),"closed miner fixture failed to exit");
 {
  CpuNormalResources automatic;QJsonObject latest;int acknowledged=0;QString lastRequest;
  automatic.onTelemetry=[&](auto,auto,QJsonObject value,bool connected){if(connected)latest=value;};
  automatic.onControl=[&](auto,QString id,bool okay,auto){check(okay,"automatic CPU normal control refused");++acknowledged;lastRequest=id;};
  CpuControlEndpoint endpoint;endpoint.deviceId="CPU/LOCAL/0";endpoint.host="LOCAL";endpoint.program=python;endpoint.arguments={"-I","-X","utf8",fixture,"--host","LOCAL","--workspace",root+"/automatic"};automatic.add(endpoint);
  check(until([&]{return !latest.isEmpty();}),"automatic endpoint not ready");
  QJsonObject profile{{"schema","PGN_LOCAL_CPU_NORMAL_PROFILE_V1"},{"program",python},{"local",true},{"network_arguments",QJsonArray{"-I","-X","utf8",fixture,"--network","--cpu-only"}}};
  check(automatic.configureNormalMiner(profile),"local CPU normal profile refused");
  auto bad=profile;bad.insert("network_arguments",QJsonArray{"--network","--cpu-only","--gpu","forbidden"});check(!automatic.configureNormalMiner(bad),"CPU profile accepted a GPU identity");
  check(automatic.control("CPU/LOCAL/0","first-on",true,50),"automatic CPU ON not routed");
  check(automatic.control("CPU/LOCAL/0","starting-off",false,50),"startup OFF not queued");
  check(until([&]{return lastRequest=="starting-off";}),"latest OFF during startup was lost");check(latest.value("enabled")==false,"startup OFF executed stale ON");
  check(automatic.control("CPU/LOCAL/0","second-on",true,25),"normal CPU restart not routed");
  check(until([&]{return lastRequest=="second-on"&&latest.value("enabled")==true;}),"CPU ON did not reach its endpoint");
  check(automatic.control("CPU/LOCAL/0","hot-threads",true,100),"hot CPU budget not routed");
  check(until([&]{return lastRequest=="hot-threads"&&latest.value("requested_threads")==4;}),"hot CPU budget not applied");
  check(acknowledged>=3,"CPU controls lacked real acknowledgements");
  check(automatic.updateResumeChoices().size()==1&&automatic.updateResumeChoices().first().toObject().value("threads")==4,"CPU update snapshot lost its exact selected budget");
  check(automatic.restoreAfterUpdate(endpoint.deviceId,3),"CPU update restore was not dispatched");
  check(until([&]{return latest.value("requested_threads")==3;}),"CPU update restore rounded the saved budget back to the 100-percent value");
  check(automatic.requestClose()||until([&]{return automatic.allStopped();}),"CPU host and endpoints did not close together");
  check(!automatic.restoreAfterUpdate(endpoint.deviceId,3),"closed CPU fleet consumed a pending resume choice");
  check(automatic.reopenAfterAbortedUpdate(),"drained CPU channels did not reopen after aborted update");
  check(until([&]{return automatic.restoreAfterUpdate(endpoint.deviceId,3);}),"reopened CPU inventory never accepted saved choice");
  check(until([&]{return latest.value("enabled")==true&&latest.value("requested_threads")==3;}),"saved CPU budget did not resume after update abort");
  check(automatic.control(endpoint.deviceId,"after-abort-off",false,75),"manual OFF after abort was not routed");
  check(automatic.updateResumeChoices().isEmpty(),"unacknowledged manual OFF remained eligible for update resume");
  check(until([&]{return lastRequest=="after-abort-off"&&latest.value("enabled")==false;}),"manual OFF lost to stale CPU ON");
  check(automatic.requestClose()||until([&]{return automatic.allStopped();}),"reopened CPU fleet did not drain normally");
 }
 std::cout<<"{\"PASS\":true,\"hosts\":2,\"distinct_workspaces\":true,\"normal_cycle_bound\":true,\"all_own_processes_exited\":true}"<<std::endl;return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}}

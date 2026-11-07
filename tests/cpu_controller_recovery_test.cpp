#include "mining/cpu_fleet.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFile>
#include <QThread>
#include <QTimer>
#include <iostream>
#include <string>
using namespace pgn::mining;
static int child(QString mode,QString logPath={}){
 auto out=[](QJsonObject v){std::cout<<QJsonDocument(v).toJson(QJsonDocument::Compact).constData()<<std::endl;};
 out({{"event","ready"},{"schema","PGN_CPU_JSONLINES_V1"},{"compute_started",false}});
 bool delayed=false;std::string line;
 while(std::getline(std::cin,line)){
  auto c=QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();auto action=c.value("action").toString();
  if(mode=="--retry"){QFile log(logPath);if(!log.open(QIODevice::Append))return 9;log.write(action.toUtf8()+'\n');log.close();if(action=="configure")return 7;}
  if(mode=="--fault"&&action=="status"){out({{"event","worker_error"},{"message","fixture checkpoint replacement denied"}});return 1;}
  if(mode=="--late"&&action=="status"&&!delayed){delayed=true;QThread::msleep(11300);}
  QJsonObject state{{"network_accepted",false},{"state",action=="close"?"CLOSED":"IDLE"},{"enabled",false},{"generation",0},{"requested_threads",1},{"worker_pids",QJsonArray{}},{"completed_positions","42"}};
  QJsonObject reply{{"id",c.value("id")},{"ok",true},{"status",state}};
  if(action=="inventory")reply.insert("inventory",QJsonObject{{"host","FIXTURE"},{"compute_started",false},{"maximum_workers",1},{"available_cpus",QJsonArray{0}},{"topology",QJsonArray{QJsonObject{{"cpus",QJsonArray{0}}}}}});
  out(reply);if(action=="close")return 0;
 }
 return 0;
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 if(argc==2)return child(QString::fromLocal8Bit(argv[1]));
 if(argc==3)return child(QString::fromLocal8Bit(argv[1]),QString::fromLocal8Bit(argv[2]));
 QTimer deadline;deadline.setSingleShot(true);QObject::connect(&deadline,&QTimer::timeout,&app,[&]{app.exit(2);});
 QStringList errors;int recovered=0;bool timedOut=false,closed=false;
 CpuControllerClient late({app.applicationFilePath(),{"--late"},"CPU/FIXTURE/0","FIXTURE"});
 late.onError=[&](QString e){errors.append(e);if(e.contains("timed out")){timedOut=true;if(late.available()){std::cerr<<"stale controller advertised available";app.exit(1);}}};
 late.onStatus=[&](QJsonObject status){if(timedOut&&late.available()){++recovered;late.close();}};
 late.onClosed=[&]{closed=true;app.quit();};
 late.start();deadline.start(16000);int code=app.exec();deadline.stop();
 if(code||!timedOut||!closed||recovered!=1||errors.size()!=1){std::cerr<<"read-only timeout recovery failed "<<code<<" "<<errors.join("|").toStdString();return 1;}
 CpuFleet fleet;bool faultSeen=false,disconnected=false,finished=false;QString finalError;
 QTimer fleetStop;fleetStop.setSingleShot(true);QObject::connect(&fleetStop,&QTimer::timeout,&app,[&]{if(fleet.requestClose()){finished=true;app.quit();}});
 fleet.onTelemetry=[&](QString,QJsonObject,QJsonObject state,bool connected){
  if(state.value("error").toString().contains("fixture checkpoint")){faultSeen=true;disconnected=!connected&&state.value("state")=="UNAVAILABLE";finalError=state.value("error").toString();if(!fleetStop.isActive())fleetStop.start(50);}
 };
 fleet.onCloseReady=[&]{finished=true;app.quit();};
 fleet.add({app.applicationFilePath(),{"--fault"},"CPU/FIXTURE/0","FIXTURE"});
 deadline.start(4000);code=app.exec();deadline.stop();fleetStop.stop();
 auto final=fleet.status("CPU/FIXTURE/0");
 if(code||!finished||!faultSeen||!disconnected||final.value("state")!="UNAVAILABLE"||!final.value("error").toString().contains("fixture checkpoint")){std::cerr<<"terminal diagnostic/fleet freshness lost "<<code<<" finished="<<finished<<" fault="<<faultSeen<<" disconnected="<<disconnected<<" state="<<final.value("state").toString().toStdString()<<" "<<finalError.toStdString();return 1;}
 QTemporaryDir directory;const auto logPath=directory.filePath("commands.txt");
 CpuControllerClient retry({app.applicationFilePath(),{"--retry",logPath},"CPU/RETRY/0","FIXTURE"});
 int inventories=0,retryErrors=0;bool mutationSent=false,reconnected=false,retryClosed=false;
 retry.onInventory=[&](QJsonObject){++inventories;};
 retry.onError=[&](QString){++retryErrors;};
 retry.onStatus=[&](QJsonObject){if(!retry.available())return;if(!mutationSent){mutationSent=true;retry.configure(false,1);}else if(inventories==2&&!reconnected){reconnected=true;retry.close();}};
 retry.onClosed=[&]{retryClosed=true;app.quit();};retry.start();deadline.start(5000);code=app.exec();deadline.stop();
 QFile log(logPath);if(!log.open(QIODevice::ReadOnly)){std::cerr<<"missing subprocess command log";return 1;}const auto commands=log.readAll().split('\n');
 if(code||!retryClosed||!reconnected||inventories!=2||retryErrors<1||commands.count("configure")!=1||commands.count("inventory")!=2||commands.count("close")!=1){std::cerr<<"idle reconnect or no-replay contract failed "<<code<<" inventories="<<inventories<<" errors="<<retryErrors<<" reconnected="<<reconnected<<" closed="<<retryClosed<<" commands="<<commands.join('|').constData();return 1;}
 CpuControllerClient cancelled({directory.filePath("absent-controller.exe"),{},"CPU/CANCEL/0","FIXTURE"});int cancelledErrors=0;bool cancelClosed=false;
 cancelled.onError=[&](QString){++cancelledErrors;QTimer::singleShot(50,&app,[&]{cancelled.close();});};cancelled.onClosed=[&]{cancelClosed=true;};
 cancelled.start();QTimer::singleShot(1300,&app,&QCoreApplication::quit);deadline.start(2500);code=app.exec();deadline.stop();
 if(code||!cancelClosed||cancelledErrors!=1||!cancelled.stopped()){std::cerr<<"closing during reconnect backoff did not cancel retry";return 1;}
 std::cout<<"PASS actual subprocess: delayed observation recovers, no stale availability, terminal cause retained, idle reconnect after lost mutation acknowledgement without replay, close cancels backoff, no computation\n";
}

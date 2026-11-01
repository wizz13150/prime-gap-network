#include "mining/cpu_fleet.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <iostream>
#include <string>
using namespace pgn::mining;
static int child(QString mode){
 auto out=[](QJsonObject v){std::cout<<QJsonDocument(v).toJson(QJsonDocument::Compact).constData()<<std::endl;};
 out({{"event","ready"},{"schema","PGN_CPU_JSONLINES_V1"},{"compute_started",false}});
 bool delayed=false;std::string line;
 while(std::getline(std::cin,line)){
  auto c=QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();auto action=c.value("action").toString();
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
 QStringList errors;int recovered=0;bool timedOut=false,closed=false;
 CpuControllerClient late({app.applicationFilePath(),{"--late"},"CPU/FIXTURE/0","FIXTURE"});
 late.onError=[&](QString e){errors.append(e);if(e.contains("timed out")){timedOut=true;if(late.available()){std::cerr<<"stale controller advertised available";app.exit(1);}}};
 late.onStatus=[&](QJsonObject status){if(timedOut&&late.available()){++recovered;late.close();}};
 late.onClosed=[&]{closed=true;app.quit();};
 late.start();QTimer::singleShot(16000,&app,[&]{app.exit(2);});int code=app.exec();
 if(code||!timedOut||!closed||recovered!=1||errors.size()!=1){std::cerr<<"read-only timeout recovery failed "<<code<<" "<<errors.join("|").toStdString();return 1;}
 CpuFleet fleet;bool faultSeen=false,disconnected=false,finished=false;QString finalError;
 fleet.onTelemetry=[&](QString,QJsonObject,QJsonObject state,bool connected){
  if(state.value("error").toString().contains("fixture checkpoint")){faultSeen=true;disconnected=!connected&&state.value("state")=="UNAVAILABLE";finalError=state.value("error").toString();}
 };
 fleet.onCloseReady=[&]{finished=true;app.quit();};
 fleet.add({app.applicationFilePath(),{"--fault"},"CPU/FIXTURE/0","FIXTURE"});
 QTimer::singleShot(1300,&app,[&]{if(fleet.requestClose()){finished=true;app.quit();}});
 QTimer::singleShot(4000,&app,[&]{app.exit(2);});code=app.exec();
 auto final=fleet.status("CPU/FIXTURE/0");
 if(code||!finished||!faultSeen||!disconnected||final.value("state")!="UNAVAILABLE"||!final.value("error").toString().contains("fixture checkpoint")){std::cerr<<"terminal diagnostic/fleet freshness lost "<<code<<" finished="<<finished<<" fault="<<faultSeen<<" disconnected="<<disconnected<<" state="<<final.value("state").toString().toStdString()<<" "<<finalError.toStdString();return 1;}
 std::cout<<"PASS actual subprocess: delayed observation recovers, no stale availability, terminal cause retained, fleet state unavailable, no computation or mutation replay\n";
}

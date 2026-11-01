#include "mining/native_bridge.h"
#include "mining/relay_runtime.h"
#include <QCoreApplication>
#include <QTimer>
#include <QFileInfo>
#include <iostream>
// Explicit integration runner, not part of ordinary ctest: it exercises the
// real owned3090 stop/resume path and must never run on an unrelated device.
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=3)return 2;
 pgn::mining::NativeBridge bridge;int phase=0,gaps=0;QString originalRun;
 const QString device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
 auto fail=[&](const QString& why){std::cerr<<"FAIL "<<why.toStdString()<<std::endl;app.exit(1);};
 bridge.onGap=[&](const auto&){++gaps;};
 bridge.onControl=[&](const auto&,const QString& id,bool applied,const QString& detail){
  std::cout<<id.toStdString()<<" applied="<<applied<<" "<<detail.toStdString()<<std::endl;
  if(!applied){fail(detail);return;}
  if(id=="qa-owned-stop"){phase=2;bridge.control(device,"qa-owned-resume",true);}
  else if(id=="qa-owned-resume")phase=3;
 };
 bridge.onDevice=[&](const auto& sample){
  if(phase==0&&!sample.stale&&sample.e2ePositionsPerSecond&&gaps>0){
   originalRun=sample.runId;phase=1;std::cout<<"LIVE source verified; cooperative stop requested"<<std::endl;
   bridge.control(device,"qa-owned-stop",false);
  }else if(phase==3&&!sample.stale&&sample.e2ePositionsPerSecond&&sample.runId!=originalRun){
   std::cout<<"PASS distinct resumed native run; raw counters and gaps retained"<<std::endl;app.exit(0);
  }
 };
 const auto runtime=pgn::mining::resolveRelayRuntime(QFileInfo(QString::fromLocal8Bit(argv[1])).absolutePath()+"/..");
 if(!runtime.valid()){fail(runtime.error);return 1;}
 bridge.start(runtime.program,QString::fromLocal8Bit(argv[1]),QString::fromLocal8Bit(argv[2]));
 QTimer::singleShot(120000,&app,[&]{fail("Integration timeout; no forced process kill");});
 return app.exec();
}

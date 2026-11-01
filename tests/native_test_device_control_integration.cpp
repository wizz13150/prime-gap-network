#include "mining/native_bridge.h"
#include "mining/relay_runtime.h"
#include <QCoreApplication>
#include <QTimer>
#include <QDateTime>
#include <iostream>

// Explicit hardware runner, never registered with ctest. It commands only
// the existing bounded-test controller and leaves the production GPU alone.
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=3)return 2;
 const QString root=QString::fromLocal8Bit(argv[1]);
 const QString test="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265";
 const QString primary="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
 const QString nonce=QString::number(QDateTime::currentMSecsSinceEpoch());
 const QString onId="root475-finite-on-"+nonce,offId="root475-finite-off-"+nonce;
 pgn::mining::NativeBridge bridge;QString primaryRun;bool testIdle=false;int phase=0;
 auto fail=[&](const QString& detail){std::cerr<<"FAIL "<<detail.toStdString()<<std::endl;app.exit(1);};
 auto start=[&]{if(phase==0&&testIdle&&!primaryRun.isEmpty()){phase=1;bridge.control(test,onId,true);}};
 bridge.onDevice=[&](const auto& lane){
  if(lane.gpuUuid!=primary||!lane.raw.value("pgn_process_running").toBool())return;
  if(primaryRun.isEmpty())primaryRun=lane.runId;
  else if(primaryRun!=lane.runId){fail("Primary epoch changed; no isolation qualification");return;}
  start();
 };
 bridge.onTestDevice=[&](const auto& lane){
  if(lane.gpuUuid!=test||!lane.raw.value("pgn_test_only").toBool()||lane.e2ePositionsPerSecond||lane.powmPerSecond){fail("Test telemetry scope/rate changed");return;}
  testIdle=!lane.raw.value("pgn_process_running").toBool();start();
 };
 bridge.onControl=[&](const QString& device,const QString& id,bool applied,const QString& detail){
  if(device!=test){fail("Unexpected production control acknowledgement");return;}
  std::cout<<id.toStdString()<<" applied="<<applied<<" "<<detail.toStdString()<<std::endl;
  if(!applied){fail(detail);return;}
  if(id==onId&&phase==1){phase=2;bridge.control(test,offId,false);}
  else if(id==offId&&phase==2){phase=3;
   std::cout<<"PASS real bounded3060 ON/OFF; primary_run="<<primaryRun.toStdString()<<"; no invented test rates; no primary control sent\n";
   app.exit(0);
  }
 };
 const auto runtime=pgn::mining::resolveRelayRuntime(root);
 if(!runtime.valid()){fail(runtime.error);return 1;}
 bridge.start(runtime.program,root+"/tools/live-client.mjs",QString::fromLocal8Bit(argv[2]),true,true);
 QTimer::singleShot(35000,&app,[&]{fail("Bounded control observation timeout; no forced GPU stop");});
 return app.exec();
}

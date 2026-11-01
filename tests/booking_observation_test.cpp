#include "booking/booking_model.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <iostream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 1;QTemporaryDir dir;
 const auto target=dir.path()+"/data/runtime";QDir().mkpath(target);
 for(const auto* name:{"binding.json","community-source.csv"})
  if(!QFile::copy(QString::fromLocal8Bit(argv[1])+"/"+name,target+"/"+name))return 2;
 pgn::booking::BookingModel model(dir.path());if(!model.source().valid())return 3;
 const QString a(32,'a'),b(32,'b'),c(32,'c'),device="GPU-QA";
 const QString first="199998000000000000000",last="199999000000000000000";
 const auto utc=QDateTime::fromString("2026-10-11T11:00:00.123Z",Qt::ISODateWithMs);
  const auto emptyRevision=model.jobsRevision();const auto emptyCount=model.jobs().size();
  const QString observedGpu="GPU-11111111-2222-3333-4444-555555555555";
  model.observeDeviceIdentity(observedGpu,"Observed idle GPU",{});
  if(!model.runtime().device.isEmpty())return 20;
  model.observeDeviceIdentity("GPU-invalid","Observed idle GPU",utc);
  if(!model.runtime().device.isEmpty())return 21;
  model.observeDeviceIdentity(observedGpu,"Observed idle GPU",utc);
  if(model.runtime().device!=observedGpu||model.runtime().label!="Observed idle GPU"||!model.runtime().run.isEmpty()||
     !model.runtime().start.isEmpty()||model.runtime().running||model.runtime().observedUtc.isValid()||
     model.jobsRevision()!=emptyRevision||model.jobs().size()!=emptyCount)return 22;
 bool incompleteNotification=false;
 model.subscribe(&app,[&]{const auto& value=model.runtime();
  if((value.run==a||value.run==b)&&value.positions.isEmpty())incompleteNotification=true;
 });
 model.observeRuntime(a,first,last,device,"QA", "0",utc,true);
 if(model.runtime().run!=a||model.runtime().positions!="0"||!model.runtime().fraction||*model.runtime().fraction!=0)return 4;
 model.observeRuntime(b,first,last,device,"QA","50",utc.addSecs(10),true);
 if(model.runtime().run!=b)return 5;
  model.observeDeviceIdentity(observedGpu,"Idle hardware must not rebind an active job",utc.addSecs(11));
  if(model.runtime().device!=device||model.runtime().label!="QA")return 23;
 if(incompleteNotification)return 14;
 const auto revision=model.jobsRevision();
 model.observeRuntime(a,first,last,device,"late old run","100",utc.addSecs(9),true);
 if(model.runtime().run!=b||model.runtime().positions!="50"||model.jobsRevision()!=revision){
  std::cerr<<"FAIL late old run changed the actual current reservation before freshness validation\n";return 6;
 }
 for(const auto& invalid:{QString("1000000000000001"),QString("-1"),QString("not-a-counter")}){
  model.observeRuntime(c,first,last,device,"invalid new run",invalid,utc.addSecs(11),true);
  if(model.runtime().run!=b||model.jobsRevision()!=revision)return 7;
  for(const auto& job:model.jobs())if(job.id==c)return 8;
 }
 model.observeRuntime(c,first,last,device,"missing time","0",{},true);
 if(model.runtime().run!=b||model.jobsRevision()!=revision)return 9;
 model.observeRuntime(b,first,last,device,"QA","49",utc.addSecs(12),true);
 if(model.runtime().positions!="50"||model.runtime().observedUtc!=utc.addSecs(10))return 10;
 model.observeRuntime(b,first,last,device,"QA","",utc.addSecs(13),false);
 if(model.runtime().running||model.runtime().positions!="50"||model.runtime().observedUtc!=utc.addSecs(10))return 11;
 const auto pausedRevision=model.jobsRevision();
 model.observeRuntime(b,first,last,device,"late ON","50",utc.addSecs(9),true);
 if(model.runtime().running||model.jobsRevision()!=pausedRevision)return 12;
 model.observeRuntime(b,"0"+first,"0"+last,device,"QA","51",utc.addSecs(14),true);
 if(model.runtime().positions!="51"||model.runtime().start!=first||model.runtime().end!=last)return 13;
 if(!model.error().isEmpty())return 15;
 {
  pgn::booking::BookingStore store(dir.path()+"/resume.sqlite");
  if(!store.observeRuntime(a,first,last,device,true).isEmpty()||
     !store.observeRuntime(b,first,last,device,true).isEmpty()||
     !store.observeRuntime(a,first,last,device,true).isEmpty())return 16;
  int active=0;for(const auto& job:store.active())if(job.state=="ACTIVE")++active;
  if(active!=1){std::cerr<<"FAIL resumed physical job left two ACTIVE reservations for one device\n";return 17;}
  if(!store.recordSourceCompletion(a,first,last,device).isEmpty()||
     !store.observeRuntime(b,first,last,device,true).isEmpty()||
     !store.observeRuntime(a,first,last,device,true).isEmpty())return 18;
  const auto rows=store.active();if(rows.size()!=1||rows.front().id!=b||rows.front().state!="ACTIVE")return 19;
 }
 std::cout<<"PASS stale run/state cannot replace current booking; invalid samples leave no durable phantom; raw zero and missing-sample age preserved\n";
}

#include "mining/native_application_runtime.h"
#include "mining/application_mining_start.h"
#include "mining/application_mining_close.h"
#include "mining/native_bridge.h"
#include "booking/booking_model.h"
#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <QWidget>
#include <iostream>

// Explicit physical test of the existing PGN application composition, using an
// isolated local registry/runtime. No alternate UI/coordinator or network claim.
int main(int argc,char** argv){
 QApplication app(argc,argv);if(argc!=5)return 2;app.setQuitOnLastWindowClosed(false);
 const QString project=QString::fromLocal8Bit(argv[1]),transportFile=QString::fromLocal8Bit(argv[2]),ssh=QString::fromLocal8Bit(argv[3]),output=QString::fromLocal8Bit(argv[4]);
 const QString directory=project+"/data/runtime",device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",first67="100000000000000000000",end67="100000064000000000000",first95="20000000000000000000000000000",end95="20000000000000002000000000000";
 pgn::booking::BookingModel bookings(project);if(!bookings.source().valid()||!bookings.source().localValidation)return 3;
 {
  pgn::booking::BookingStore seed(bookings.historyDatabase());if(!seed.valid())return 4;
  if(!seed.active().isEmpty())return 5;
  if(!seed.setMiningSessionMode(device,pgn::mining::MiningSessionMode::ManualReservations).isEmpty()
    ||!seed.setAutomaticAfterManual(device,false).isEmpty()
    ||!seed.queue(bookings.source(),first67,end67,device).isEmpty()
    ||!seed.queue(bookings.source(),first95,end95,device).isEmpty())return 6;
 }
 bookings.reloadDispatchState();QString queued95;for(const auto& job:bookings.jobs())if(job.start==first95)queued95=job.id;if(queued95.isEmpty())return 7;
 pgn::mining::NativeRuntimePaths paths{bookings.historyDatabase(),directory+"/scientific-history.sqlite",directory+"/source-journals",directory+"/engine-modules.sqlite","C:/legacy/checkpoint"};QDir().mkpath(paths.sourceMirrorDirectory);
 const auto config=pgn::mining::loadNativeRuntimeConfiguration(transportFile,ssh,paths);if(!config.available()||!config.configuration->sourceOnly)return 8;
 pgn::mining::NativeBridge bridge;QWidget window;
 pgn::mining::NativeApplicationRuntimeConfig runtimeConfig;runtimeConfig.transport=*config.configuration;runtimeConfig.sourceRegistryDirectory=directory;runtimeConfig.controlTimeouts={5000,30000};runtimeConfig.timingAudit=true;
 pgn::mining::NativeApplicationRuntime runtime(runtimeConfig,&bridge);
 pgn::mining::ApplicationMiningStart start(&bridge,paths.bookingDatabase,directory,nullptr,config.configuration->paths.checkpointLabel);
 pgn::mining::ApplicationMiningClose close(&window,&app,&bridge,paths.bookingDatabase,config.configuration->paths.checkpointLabel,nullptr,false);
 unsigned stage=0;bool failed=false,closing=false;QString error,run67;quint64 paused=0;QJsonArray events,statuses;QJsonObject last;
 auto event=[&](const QString& name,const QJsonObject& fields=QJsonObject{}){QJsonObject row=fields;row.insert("event",name);row.insert("utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));events.append(row);std::cout<<QJsonDocument(row).toJson(QJsonDocument::Compact).constData()<<std::endl;};
 auto fail=[&](const QString& problem){if(failed)return;failed=true;error=problem;event("FAILED",{{"error",problem}});start.control(device,QUuid::createUuid().toString(QUuid::WithoutBraces),false);QTimer::singleShot(1000,&close,[&]{close.requestClose();});};
 bridge.onDevice=[&](const FrontierTelemetrySnapshot& sample){
  bookings.observeRuntime(sample.runId,sample.domainStart,sample.domainEnd,sample.gpuUuid,sample.gpuName,sample.rowsCompleted,sample.observedUtc,sample.raw.value("pgn_process_running").toBool());
  if(failed)return;const auto position=sample.rowsCompleted.toULongLong();
  if(stage==0&&sample.domainStart==first67&&sample.raw.value("pgn_process_running")==true&&position>100000000000ULL){run67=sample.runId;stage=1;event("67 actual progress, stop requested",{{"position",sample.rowsCompleted},{"run",run67}});QTimer::singleShot(0,&start,[&]{start.control(device,QUuid::createUuid().toString(QUuid::WithoutBraces),false);});}
  else if(stage==2&&sample.domainStart==first67&&sample.runId!=run67&&sample.raw.value("pgn_process_running")==true&&position>paused){stage=3;event("67 actual resumed progression",{{"position",sample.rowsCompleted},{"run",sample.runId}});bookings.dispatch(queued95);}
 };
 bridge.onDispatch=[&](const QString&,bool accepted,const QString& detail){event("95 queue arm",{{"accepted",accepted},{"detail",detail}});if(!accepted)fail("Existing PGN could not arm95: "+detail);};
 bookings.onDispatch=[&](const QString& id){bridge.dispatch(id);};
 bridge.onSourceCompletion=[&](const QJsonObject& receipt,bool reload){bookings.observeSourceCompletion(receipt,reload);};
 bridge.onSourceSnapshot=[&](const QJsonObject& snapshot){
  last=snapshot;if(failed)return;
  const auto manifest=snapshot.value("manifest").toObject(),exit=snapshot.value("last_exit").toObject();const auto range=manifest.value("start").toString();
  if(stage==1&&range==first67&&snapshot.value("state")=="OFF"&&snapshot.value("owned")==false&&snapshot.value("checkpoint_evidence").toObject().value("status")=="captured"){
   const auto b64=snapshot.value("checkpoint_evidence").toObject().value("checkpoint_base64").toString();const auto bytes=QByteArray::fromBase64(b64.toLatin1());if(bytes.size()!=64){fail("Actual pause checkpoint length");return;}
   for(int i=0;i<8;++i)paused|=quint64(uchar(bytes[32+i]))<<(8*i);stage=2;event("67 checkpoint observed, resume requested",{{"positions",QString::number(paused)}});QTimer::singleShot(0,&start,[&]{start.control(device,QUuid::createUuid().toString(QUuid::WithoutBraces),true);});
  }
  bool complete=false;for(const auto* key:{"completion","completion_candidate","source_completion_observation"}){const auto c=snapshot.value(key).toObject();if(c.value("run")==manifest.value("run")&&c.value("source_range_complete")==true)complete=true;}
  if(stage==3&&range==first67&&snapshot.value("state")=="OFF"&&complete){stage=4;event("67 complete,95 start requested");QTimer::singleShot(0,&start,[&]{start.control(device,QUuid::createUuid().toString(QUuid::WithoutBraces),true);});}
  else if(stage==3&&range==first95)stage=4;
  if(stage==4&&range==first95&&snapshot.value("state")=="OFF"&&snapshot.value("owned")==false&&complete&&exit.value("exit_code")==0){stage=5;event("95 complete, application close requested");closing=true;QTimer::singleShot(0,&close,[&]{close.requestClose();});}
 };
 const auto priorControl=bridge.onControl;
 bridge.onControl=[&,priorControl](const QString& gpu,const QString& id,bool applied,const QString& detail){if(priorControl)priorControl(gpu,id,applied,detail);event("control result",{{"device",gpu},{"request",id},{"applied",applied},{"detail",detail}});if(!applied&&!closing&&detail.contains("held",Qt::CaseInsensitive))fail("Actual PGN control held: "+detail);};
 runtime.onStatus=[&](const QString& text){if(statuses.size()==48)statuses.removeFirst();statuses.append(text);std::cout<<"STATUS "<<text.toStdString()<<std::endl;};
 start.onStatus=[&](const QString& text){event("start status",{{"detail",text}});if(text.contains(" held:",Qt::CaseInsensitive)||text.startsWith("Initial start held:")||text.startsWith("Checkpoint resume held:"))fail(text);};
 runtime.onTiming=[&](const QJsonObject& packet){event("native timing",packet);};
 close.onStatus=[&](const QString& text){if(statuses.size()==48)statuses.removeFirst();statuses.append(text);std::cout<<"CLOSE "<<text.toStdString()<<std::endl;if(failed&&text.startsWith("Close held:")){runtime.onStopped=[&]{app.exit(11);};runtime.stop();}};
 close.onDrainBeforeQuit=[&](auto done){runtime.onStopped=[done]{done();};runtime.stop();};
 QTimer::singleShot(300000,&app,[&]{fail("Actual existing-application QA deadline");});
 QTimer::singleShot(420000,&app,[&]{failed=true;error="Close not confirmed after bounded cleanup; inspect own validation runtime";runtime.stop();app.exit(9);});
 if(!runtime.start())return 10;
 QTimer::singleShot(100,&start,[&]{event("Existing PGN67 start requested");start.control(device,QUuid::createUuid().toString(QUuid::WithoutBraces),true);});
 const auto code=app.exec();const bool good=!failed&&stage==5&&close.readyToQuit()&&runtime.stopped();
 QSaveFile out(output);if(out.open(QIODevice::WriteOnly)){out.write(QJsonDocument(QJsonObject{{"PASS",good},{"exit",code},{"stage",int(stage)},{"error",error},{"events",events},{"statuses",statuses},{"last_snapshot",last},{"runtime_stopped",runtime.stopped()},{"application_close_ready",close.readyToQuit()},{"scope","Actual existing PGN composition withisolatedoperatorregistry. Offscreen prequalification, notvisibleUI/networkacceptance."}}).toJson());out.commit();}
 // Captured fixture state is declared after the actors. Remove its callbacks
 // before stack unwinding so actor/client destruction cannot access that state.
 bridge.onDevice={};bridge.onControl={};bridge.onSourceSnapshot={};bridge.onSourceCompletion={};bridge.onDispatch={};
 bookings.onDispatch={};runtime.onStatus={};runtime.onTiming={};runtime.onPacket={};runtime.onStopped={};start.onStatus={};close.onStatus={};close.onDrainBeforeQuit={};
 return good?0:11;
}

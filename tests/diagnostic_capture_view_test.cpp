#include "ui/diagnostic_capture_view.h"
#include "ui/mining_startup_status.h"
#include "ui/mining_process_diagnostics.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QPushButton>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <QJsonDocument>
#include <iostream>
int main(int argc,char** argv){
 if(argc>1&&QString::fromLocal8Bit(argv[1])=="--process-wait"){QThread::msleep(10000);return 0;}
 if(argc>1&&QString::fromLocal8Bit(argv[1])=="--process-failure"){
  std::cerr<<"Traceback: C:/Users/example/private-folder/engine.py\nModuleNotFoundError: missing_runtime_module\npassword=INERT-SENSITIVE\n";return 7;
 }
 QApplication app(argc,argv);
 if(argc==5&&QString::fromLocal8Bit(argv[1])=="--packaged-startup-probe"){
  const QString host=QString::fromLocal8Bit(argv[2]),bundle=QString::fromLocal8Bit(argv[3]),output=QString::fromLocal8Bit(argv[4]);
  QTemporaryDir state;if(!state.isValid())return 20;QWidget canvas;QProcess process;
  pgn::ui::MiningProcessDiagnostics diagnostic(&canvas,&process,"GPU-ISOLATED-FAILURE");QByteArray pending;
  QObject::connect(&process,&QProcess::readyReadStandardOutput,&process,[&]{pending+=process.readAllStandardOutput();while(pending.contains('\n')){const int end=pending.indexOf('\n');const auto packet=QJsonDocument::fromJson(pending.left(end)).object();pending.remove(0,end+1);diagnostic.packet(packet);}});
  QObject::connect(&process,&QProcess::readyReadStandardError,&process,[&]{diagnostic.standardError(process.readAllStandardError());});
  process.setProgram(host);process.setArguments({"--network","--bundle",bundle,"--state-root",state.path(),"--gpu","GPU-11111111-1111-1111-1111-111111111111","--architecture","86","--rpc-port","1","--cookie",state.filePath("absent-core-cookie"),"--genesis",QString(64,'a'),"--wallet","diagnostic-fixture","--reward","0014"+QString(40,'1')});
  diagnostic.intent(true);diagnostic.launching("network");QElapsedTimer elapsed;elapsed.start();process.start();
  while((process.state()!=QProcess::NotRunning||!canvas.property("pgn_worker_process_state").toJsonObject().contains("exit_code"))&&elapsed.elapsed()<15000){app.processEvents();QThread::msleep(2);}
  const auto captured=pgn::ui::diagnosticMiningStartup(&canvas),worker=captured["process"].toObject(),fault=worker["failure_context"].toObject();
  if(worker["exit_code"]!=1||fault["schema"]!="PGN_RUNTIME_FAILURE_V1"||fault["step"]!="verifying_core"||!fault["causes"].toArray().first().toObject()["resource"].toString().endsWith("/absent-core-cookie"))return 21;
  // Begin recording after the failed process is gone, as the tester may do.
  auto* view=pgn::ui::createDiagnosticCaptureView(host,[captured]{return QJsonObject{{"mining_control",captured}};});
  view->findChild<QPushButton*>("pgn-diagnostics-start")->click();view->findChild<QPushButton*>("pgn-diagnostics-stop")->click();pgn::ui::saveDiagnosticCapture(view,output);
  QElapsedTimer wait;wait.start();while(!view->property("pgn_capture_saved").isValid()&&wait.elapsed()<15000){app.processEvents();QThread::msleep(3);}
  const bool saved=view->property("pgn_capture_saved").toBool();delete view;if(!saved)return 22;
  QFile proof(output+".json");if(!proof.open(QIODevice::WriteOnly))return 23;proof.write(QJsonDocument(QJsonObject{{"PASS",true},{"actual_packaged_network_startup",true},{"capture_started_after_process_exit",true},{"GPU_started",false},{"elapsed_to_exit_ms",elapsed.elapsed()},{"captured",captured}}).toJson());return 0;
 }
 if(argc!=2)return 2;QTemporaryDir folder;if(!folder.isValid())return 3;
 QWidget canvas;QLabel startup;
 pgn::ui::presentMiningStartup(&canvas,&startup,"prepare_failed","Missing module: C:/Users/example/private-folder/worker.exe");
 canvas.setProperty("pgn_local_gpu_profiles",0);canvas.setProperty("pgn_worker_stream_error","password=INERT-SENSITIVE");canvas.setProperty("private_key","INERT-NOT-EXPORTED");
 const auto control=pgn::ui::diagnosticMiningStartup(&canvas);const auto controlBytes=QJsonDocument(control).toJson();
 if(startup.isHidden()||control.value("stage")!="prepare_failed"||control.value("gpu_profiles").toInt()!=0||controlBytes.contains("private-folder")||controlBytes.contains("INERT-"))return 13;
 pgn::ui::presentMiningStartup(&canvas,&startup,"ready","Prepared; computation waits for user ON");
 if(!startup.isHidden()||canvas.property("pgn_local_runtime_stage").toString()!="ready")return 14;
 canvas.setProperty("pgn_network_desired_on",true);
 canvas.setProperty("pgn_worker_process_state",QJsonObject{{"role","network"},{"running",true},{"phase","Searching"},{"notice",""}});
 if(pgn::ui::diagnosticMiningStartup(&canvas)["detail"]!="Searching"||pgn::ui::diagnosticMiningStartup(&canvas)["stage"]!="network")return 24;
 canvas.setProperty("pgn_worker_process_state",QJsonObject{{"role","observer"},{"running",true}});
 if(pgn::ui::diagnosticMiningStartup(&canvas)["detail"].toString().contains("OFF"))return 25;
 canvas.setProperty("pgn_worker_process_state",QJsonObject{});canvas.setProperty("pgn_network_desired_on",false);
 pgn::ui::presentMiningStartup(&canvas,&startup,"gpu_unavailable","NVIDIA hardware query failed");
 if(startup.isHidden()||!startup.text().contains("NVIDIA hardware query failed"))return 15;
  {
   QProcess process;pgn::ui::MiningProcessDiagnostics diagnostic(&canvas,&process,"GPU-FIXTURE");
   QObject::connect(&process,&QProcess::readyReadStandardError,&process,[&]{diagnostic.standardError(process.readAllStandardError());});
   diagnostic.intent(true);diagnostic.launching("network");process.start(QCoreApplication::applicationFilePath(),{"--process-failure"});
   QElapsedTimer until;until.start();while((process.state()!=QProcess::NotRunning||!canvas.property("pgn_worker_process_state").toJsonObject().contains("exit_code"))&&until.elapsed()<5000){app.processEvents();QThread::msleep(2);}
   const auto captured=pgn::ui::diagnosticMiningStartup(&canvas);const auto state=captured["process"].toObject();const auto raw=QJsonDocument(captured).toJson();
   if(state["exit_code"]!=7||state["exit_kind"]!="normal"||!state["stderr_tail"].toString().contains("missing_runtime_module")||raw.contains("private-folder")||raw.contains("INERT-SENSITIVE")||captured["events"].toArray().size()<4)return 16;
   // A subsequent launch must not erase the preceding failure before a user
   // can start recording. Event storage remains bounded under repeated retries.
   for(int i=0;i<40;++i)diagnostic.intent(true);diagnostic.launching("network");
   if(canvas.property("pgn_worker_process_events").toJsonArray().size()!=24||!canvas.property("pgn_worker_process_state").toJsonObject()["last_failure"].toString().contains("missing_runtime_module"))return 17;
   process.setProgram(QCoreApplication::applicationFilePath()+".absent");process.setArguments({});process.start();until.restart();
   while(!canvas.property("pgn_worker_process_state").toJsonObject().contains("process_error")&&until.elapsed()<5000){app.processEvents();QThread::msleep(2);}
   if(canvas.property("pgn_worker_process_state").toJsonObject()["process_error"].toInt(-1)!=int(QProcess::FailedToStart))return 18;
  }
 // Deliberate handover must not hide an unrelated observer failure.
 for(bool expected:{true,false}){
  QWidget isolated;QProcess process;pgn::ui::MiningProcessDiagnostics diagnostic(&isolated,&process,"GPU-OBSERVER-FIXTURE");
  diagnostic.launching("observer");process.start(QCoreApplication::applicationFilePath(),{"--process-wait"});
  if(!process.waitForStarted(5000))return 26;
  app.processEvents();if(expected)diagnostic.replacingObserver();process.kill();
  QElapsedTimer until;until.start();while((process.state()!=QProcess::NotRunning||!isolated.property("pgn_worker_process_state").toJsonObject().contains("exit_code"))&&until.elapsed()<5000){app.processEvents();QThread::msleep(2);}
  const auto state=isolated.property("pgn_worker_process_state").toJsonObject();
  if(expected){if(state["exit_kind"]!="replaced"||state.contains("process_error")||state["native_exit_kind"]!="crash")return 27;}
  else if(state["exit_kind"]!="crash"||state["process_error"].toInt(-1)!=int(QProcess::Crashed))return 28;
 }
 const auto maintenance=pgn::ui::diagnosticPublicationMaintenance(QJsonObject{{"owner",QJsonObject{{"active",true},{"elapsed_seconds",2.5},{"next_in_seconds",1.0},{"error","password=INERT-MAINTENANCE"},{"cookie","INERT-MAINTENANCE"}}},{"auditors",QJsonObject{{"active",false},{"elapsed_seconds",-1},{"error",QJsonValue{}}}}});
 if(QJsonDocument(maintenance).toJson().contains("INERT-")||maintenance["auditors"].toObject().contains("elapsed_seconds")||maintenance["owner"].toObject()["elapsed_seconds"]!=2.5)return 29;
 const QJsonObject inventory{{"schema","PGN_LOCAL_COMPONENT_VIEW_V1"},{"components",QJsonArray{
  QJsonObject{{"id","ui"},{"state","matched_local_manifest"},{"version","2026.11.7.45"},{"primary_bound",true},{"code_root","C:/Users/example/private-location"},{"private_key","INERT-DO-NOT-COPY"}},
  QJsonObject{{"id","cpu"},{"state","unidentified"},{"version","99.0"},{"reason","password=INERT-DO-NOT-COPY"}}}}};
 const auto software=pgn::ui::diagnosticComponentSnapshot(inventory);const auto entries=software.value("components").toArray();
 if(!software.value("available").toBool()||entries.size()!=2||entries[0].toObject().value("version")!="2026.11.7.45"||!entries[1].toObject().value("version").isNull()||QJsonDocument(software).toJson().contains("DO-NOT-COPY"))return 10;
 auto duplicates=inventory;duplicates.insert("components",QJsonArray{entries[0],entries[0]});if(pgn::ui::diagnosticComponentSnapshot(duplicates).value("available").toBool())return 11;
 if(pgn::ui::diagnosticComponentSnapshot({}).value("available").toBool())return 12;
 int reads=0;auto* view=pgn::ui::createDiagnosticCaptureView(QString::fromLocal8Bit(argv[1]),[&]{++reads;return QJsonObject{{"phase","Searching"},{"software",software},{"mining_control",control},{"positions","1000"},{"message","C:/Users/example/device-status"}};});
 auto* start=view->findChild<QPushButton*>("pgn-diagnostics-start");auto* stop=view->findChild<QPushButton*>("pgn-diagnostics-stop");
 const auto file=folder.filePath("capture.zip");if(!start||!stop||QFile::exists(file)||reads)return 4;
 start->click();start->click();if(reads!=1||view->property("pgn_capture_samples").toInt()!=1)return 5;
 stop->click();stop->click();QElapsedTimer quiet;quiet.start();while(quiet.elapsed()<1100){app.processEvents();QThread::msleep(5);}if(reads!=1||QFile::exists(file))return 6;
 pgn::ui::saveDiagnosticCapture(view,file);pgn::ui::saveDiagnosticCapture(view,file);QElapsedTimer wait;wait.start();
 while(!view->property("pgn_capture_saved").isValid()&&wait.elapsed()<15000){app.processEvents();QThread::msleep(5);}
 QFile zip(file);const bool okay=view->property("pgn_capture_saved").toBool()&&zip.open(QIODevice::ReadOnly)&&zip.size()<2097152&&zip.read(2)=="PK"&&reads==1;
 std::cout<<view->property("pgn_capture_status").toString().toStdString()<<'\n';delete view;if(!okay)return 7;
 auto* refused=pgn::ui::createDiagnosticCaptureView(QString::fromLocal8Bit(argv[1]),[]{return QJsonObject{{"private_key","INERT REFUSAL FIXTURE"}};});
 refused->findChild<QPushButton*>("pgn-diagnostics-start")->click();refused->findChild<QPushButton*>("pgn-diagnostics-stop")->click();
 const auto rejected=folder.filePath("refused.zip");pgn::ui::saveDiagnosticCapture(refused,rejected);wait.restart();
 while(!refused->property("pgn_capture_saved").isValid()&&wait.elapsed()<15000){app.processEvents();QThread::msleep(5);}
 const bool rejectedSafely=refused->property("pgn_capture_saved").isValid()&&!refused->property("pgn_capture_saved").toBool()&&!QFile::exists(rejected)&&refused->property("pgn_capture_samples").toInt()==1;
 delete refused;return rejectedSafely?0:8;
}

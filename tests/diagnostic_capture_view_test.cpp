#include "ui/diagnostic_capture_view.h"
#include "ui/mining_startup_status.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QPushButton>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <QJsonDocument>
#include <iostream>
int main(int argc,char** argv){
 QApplication app(argc,argv);if(argc!=2)return 2;QTemporaryDir folder;if(!folder.isValid())return 3;
 QWidget canvas;QLabel startup;
 pgn::ui::presentMiningStartup(&canvas,&startup,"prepare_failed","Missing module: C:/Users/example/private-folder/worker.exe");
 canvas.setProperty("pgn_local_gpu_profiles",0);canvas.setProperty("pgn_worker_stream_error","password=INERT-SENSITIVE");canvas.setProperty("private_key","INERT-NOT-EXPORTED");
 const auto control=pgn::ui::diagnosticMiningStartup(&canvas);const auto controlBytes=QJsonDocument(control).toJson();
 if(startup.isHidden()||control.value("stage")!="prepare_failed"||control.value("gpu_profiles").toInt()!=0||controlBytes.contains("private-folder")||controlBytes.contains("INERT-"))return 13;
 pgn::ui::presentMiningStartup(&canvas,&startup,"ready","Prepared; computation waits for user ON");
 if(!startup.isHidden()||canvas.property("pgn_local_runtime_stage").toString()!="ready")return 14;
 pgn::ui::presentMiningStartup(&canvas,&startup,"gpu_unavailable","NVIDIA hardware query failed");
 if(startup.isHidden()||!startup.text().contains("NVIDIA hardware query failed"))return 15;
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

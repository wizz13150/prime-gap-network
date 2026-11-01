#include "ui/diagnostic_capture_view.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QPushButton>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
int main(int argc,char** argv){
 QApplication app(argc,argv);if(argc!=2)return 2;QTemporaryDir folder;if(!folder.isValid())return 3;
 int reads=0;auto* view=pgn::ui::createDiagnosticCaptureView(QString::fromLocal8Bit(argv[1]),[&]{++reads;return QJsonObject{{"phase","Searching"},{"positions","1000"},{"message","C:/Users/example/device-status"}};});
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

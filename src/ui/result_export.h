#pragma once
#include "app/horizon_user_paths.h"
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>
#include <memory>
namespace pgn::ui {
inline void bindResultExport(QPushButton* button,QLabel* status,const QString& root){
 QObject::connect(button,&QPushButton::clicked,button,[button,status,root]{
  QFile file(pgn::app::mutableDataRoot(root)+"/testnet/ui-runtime/worker-stream.json");
  if(!file.open(QIODevice::ReadOnly)||file.size()>65536){status->setText("No local mining profile is available for export");return;}
  const auto profile=QJsonDocument::fromJson(file.readAll()).object();QStringList arguments;
  for(const auto& value:profile.value("network_arguments").toArray())arguments.append(value.toString());
  const int mode=arguments.indexOf("--network"),state=arguments.indexOf("--state-root"),genesis=arguments.indexOf("--genesis");
  if(mode<0||state<0||genesis<0||state+1>=arguments.size()||genesis+1>=arguments.size()){status->setText("The selected mining profile has no exportable result location");return;}
  const auto destination=QFileDialog::getSaveFileName(button,"Export retained search results",{},"Results and CSV tables (*.zip)");if(destination.isEmpty())return;
  auto output=std::make_shared<QSaveFile>(destination);if(!output->open(QIODevice::WriteOnly)){status->setText("Cannot open the selected export destination");return;}
  auto* process=new QProcess(button);auto* timer=new QTimer(process);timer->setSingleShot(true);
  auto bytes=std::make_shared<qint64>(0);auto failed=std::make_shared<bool>(false);auto prefix=std::make_shared<QByteArray>();
  auto drain=[=]{const auto data=process->readAllStandardOutput();if(prefix->size()<4)prefix->append(data.left(4-prefix->size()));*bytes+=data.size();
   if(*bytes>512LL*1024*1024||output->write(data)!=data.size()){*failed=true;process->kill();}};
  QObject::connect(process,&QProcess::readyReadStandardOutput,process,drain);
  QObject::connect(timer,&QTimer::timeout,process,[=]{*failed=true;process->kill();});
  QObject::connect(process,&QProcess::errorOccurred,button,[=](QProcess::ProcessError error){if(error==QProcess::FailedToStart){output->cancelWriting();button->setEnabled(true);status->setText("Result exporter could not start; source results remain unchanged");process->deleteLater();}});
  QObject::connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),button,[=](int code,QProcess::ExitStatus exit){
   drain();timer->stop();button->setEnabled(true);const auto error=QString::fromUtf8(process->readAllStandardError()).right(400);
   if(!*failed&&code==0&&exit==QProcess::NormalExit&&prefix->startsWith("PK")&&output->commit())status->setText("Results exported · exact endpoints, gap sizes and CSV tables · "+destination);
   else{output->cancelWriting();status->setText("Export was not saved; source results remain unchanged. "+error);}process->deleteLater();
  });
  QStringList command=arguments.mid(0,mode);command.append(QStringList{"--export-results","--state-root",arguments.at(state+1),"--genesis",arguments.at(genesis+1)});
  button->setEnabled(false);status->setText("Exporting retained search results · active work and private audit partitions are excluded…");
  process->setProgram(profile.value("program").toString());process->setArguments(command);timer->start(120000);process->start();
 });
}
}

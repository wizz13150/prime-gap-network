#include "wallet/core_runtime.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <iostream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);
 if(argc!=3){std::cerr<<"Product root and copied read-only runtime metadata required\n";return 2;}
 pgn::wallet::CoreRuntime runtime(QString::fromLocal8Bit(argv[1]),nullptr,true,QString::fromLocal8Bit(argv[2]));
 bool requested=false;int code=1;
 runtime.onStatus=[](const QString& text){std::cerr<<text.toStdString()<<'\n';};
 QTimer clock;clock.setInterval(100);
 QObject::connect(&clock,&QTimer::timeout,&app,[&]{
  if(!runtime.connected()||requested)return;requested=true;
  runtime.readParticipantPage(0,[&](QJsonObject value,QString error){
   std::cout<<QJsonDocument(QJsonObject{{"snapshot",value},{"error",error},{"connected",runtime.connected()}}).toJson(QJsonDocument::Compact).constData()<<'\n';code=error.isEmpty()?0:1;app.quit();
  });
 });
 QTimer::singleShot(12000,&app,&QCoreApplication::quit);clock.start();runtime.reattach();app.exec();return code;
}

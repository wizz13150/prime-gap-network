#include "wallet/core_runtime.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <iostream>
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);if(argc!=3)return 2;
    pgn::wallet::CoreRuntime core(QString::fromLocal8Bit(argv[1]),nullptr,true,QString::fromLocal8Bit(argv[2]));
    bool ready=false;int observations{};
    core.onStatus=[](const QString& text){std::cout<<"STATUS "<<text.toStdString()<<'\n'<<std::flush;};
    core.onObservation=[&](const auto& value){if(value)++observations;};
    core.onReady=[&](qint64 pid){
        if(ready)return;ready=true;
        std::cout<<QJsonDocument(QJsonObject{{"PASS",true},{"new_pid",QString::number(pid)},
            {"wallet_ready",core.walletReady()},{"genesis",core.networkGenesis()},{"observations",observations}})
            .toJson(QJsonDocument::Compact).constData()<<'\n'<<std::flush;
        core.stop();
    };
    core.onStopped=[&]{if(ready)app.exit(0);};
    QTimer::singleShot(90000,&app,[&]{std::cerr<<"Timed out waiting for owned Core migration and wallet readiness\n";app.exit(1);});
    core.launch();return app.exec();
}

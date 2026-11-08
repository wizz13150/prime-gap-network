// Inert native updater fixture. No Core, GPU, network, wallet, or mining work.
// It uses the production data lease and lifecycle JSON contract; files supply
// the test transport so the process-identity/install boundary stays isolated.
#include "app/horizon_ui_lease.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <cstdio>

static bool save(const QString& name,const QByteArray& data){
    QFile file(name);return file.open(QIODevice::WriteOnly)&&file.write(data)==data.size();
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    const auto control=qEnvironmentVariable("PGN_TEST_UPDATE_CONTROL");
    if(control.isEmpty())return 11;
    if(args.size()>1&&(args[1]=="--help"||args[1]=="--self-test"))return 0;
    if(args.size()==3&&args[1]=="--inspect-owned"){
        QFile file(control+"/state."+args[2]);if(!file.open(QIODevice::ReadOnly))return 3;
        const auto data=file.readAll();std::fwrite(data.data(),1,data.size(),stdout);return 0;
    }
    if(args.size()==3&&args[1]=="--update-close-owned"){
        if(!save(control+"/stop."+args[2],"stop"))return 4;
        std::puts("{\"accepted\":true,\"scope\":\"ORDERED_MINING_DRAIN_THEN_OWNED_CORE_STOP\"}");return 0;
    }
    if(QCoreApplication::applicationFilePath()==qEnvironmentVariable("PGN_TEST_UPDATE_FAIL_IMAGE"))return 7;
    const auto data=qEnvironmentVariable("PGN_TEST_UPDATE_DATA");
    const auto genesis=qEnvironmentVariable("PGN_TEST_UPDATE_GENESIS");
    pgn::app::HorizonUiLease lease(data+"/.horizon-ui-"+genesis+".lock");if(!lease.owned())return 12;
    const auto pid=QString::number(QCoreApplication::applicationPid());
    const QJsonObject state{{"pgn_update_application_root",qEnvironmentVariable("PGN_TEST_UPDATE_ROOT")},
        {"pgn_update_data_root",data},{"pgn_update_genesis",genesis},{"pgn_update_explicit_root",false},
        {"window_minimized",false},{"pgn_core_link_state","ready"}};
    if(!save(control+"/state."+pid,QJsonDocument(state).toJson(QJsonDocument::Compact)))return 5;
    for(int n=0;n<12000;++n){if(QFile::exists(control+"/stop."+pid))return 0;QThread::msleep(10);}
    return 6;
}

#include "wallet/core_runtime.h"
#include "wallet/setup_deployment.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <iostream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 2;QFile input(QString::fromLocal8Bit(argv[1]));if(!input.open(QIODevice::ReadOnly)||input.size()>32768)return 3;const auto config=QJsonDocument::fromJson(input.readAll()).object();
 const auto asset=config.value("application_root").toString(),runtimeDirectory=config.value("runtime_directory").toString(),data=config.value("data_directory").toString();if(config.value("deployment").toObject().value("rpc_port")!=48972)return 4;
 QDir().mkpath(asset);QString firstAddress,lastStatus;bool noAutomaticInstall=false;int readyCount=0;
 {
  pgn::wallet::CoreRuntime unconfigured(asset,nullptr,true,runtimeDirectory);unconfigured.launch();noAutomaticInstall=unconfigured.processId()==0&&!QFileInfo::exists(data);if(!noAutomaticInstall)return 5;
 }
 const auto deployment=pgn::wallet::selectedWalletDeployment(config.value("deployment").toObject(),{data,"setup_fixture",false},asset);if(!deployment.error.isEmpty()||!pgn::wallet::publishWalletSetup(runtimeDirectory,deployment).isEmpty()||QFileInfo::exists(data))return 6;
 for(int pass=0;pass<2;++pass){
  pgn::wallet::CoreRuntime runtime(asset,nullptr,true,runtimeDirectory);QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);bool okay=false,stopped=false;
  runtime.onStatus=[&](const QString& text){lastStatus=text;};runtime.onStopped=[&]{stopped=true;loop.quit();};
  runtime.onReady=[&](qint64){++readyCount;runtime.walletRequest("getwalletinfo",{},[&](QJsonValue value,QString error){
   if(!error.isEmpty()||value.toObject().value("walletname")!="setup_fixture"){runtime.stop();return;}
   QFile receipt(runtimeDirectory+"/wallet-core.json");if(!receipt.open(QIODevice::ReadOnly)){runtime.stop();return;}const auto profile=QJsonDocument::fromJson(receipt.readAll()).object();receipt.close();if(profile.value("create_wallet")!=false){runtime.stop();return;}
   if(pass==0)runtime.walletRequest("getnewaddress",{"lifecycle fixture","bech32"},[&](QJsonValue address,QString error){okay=error.isEmpty()&&!address.toString().isEmpty();firstAddress=address.toString();runtime.stop();});
   else runtime.walletRequest("getaddressinfo",{firstAddress},[&](QJsonValue info,QString error){okay=error.isEmpty()&&info.toObject().value("ismine")==true;runtime.stop();});
  });};
  QObject::connect(&timeout,&QTimer::timeout,&loop,[&]{runtime.stop();QTimer::singleShot(8000,&loop,&QEventLoop::quit);});timeout.start(25000);runtime.launch();loop.exec();if(!okay||!stopped){std::cerr<<lastStatus.toStdString();return 7+pass;}
 }
 std::cout<<QJsonDocument(QJsonObject{{"PASS",readyCount==2},{"no_automatic_install",noAutomaticInstall},{"created_after_finish",true},{"create_authorization_cleared",true},{"reopen_preserves_key_ownership",true},{"graceful_stops",2},{"scope","Actual isolated Core setup lifecycle; no live wallet/data or mining touched"}}).toJson(QJsonDocument::Compact).constData()<<'\n';return readyCount==2?0:9;
}

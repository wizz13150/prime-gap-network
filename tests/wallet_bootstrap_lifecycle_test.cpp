#include "wallet/core_runtime.h"
#include "wallet/setup_deployment.h"
#include "engine/local_runtime_client.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QSaveFile>
#include <QTimer>
#include <iostream>

// Explicit isolated fixture: real wallet setup, integrated TLS lifecycle and
// local engine preparation, with no scientific work allocation or computation.
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 2;
 QFile input(QString::fromLocal8Bit(argv[1]));if(!input.open(QIODevice::ReadOnly)||input.size()>32768)return 3;
 const auto config=QJsonDocument::fromJson(input.readAll()).object();input.close();
 const auto root=config.value("application_root").toString(),runtimeDirectory=config.value("runtime_directory").toString(),data=config.value("data_directory").toString();
 const auto deployment=config.value("deployment").toObject();const bool existing=config.value("existing").toBool();
 if(deployment.value("rpc_port")!=48982||QFileInfo::exists(data)!=existing||QFileInfo::exists(runtimeDirectory))return 4;
 const auto selection=pgn::wallet::selectedWalletDeployment(deployment,{data,"bootstrap_fixture",existing},root);
 if(!selection.error.isEmpty()){std::cerr<<selection.error.toStdString();return 5;}
 const auto setupError=pgn::wallet::publishWalletSetup(runtimeDirectory,selection);
 if(!setupError.isEmpty()||(!existing&&QFileInfo::exists(data))){std::cerr<<setupError.toStdString();return 5;}
 pgn::wallet::CoreRuntime runtime(root,nullptr,true,runtimeDirectory);QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);
 bool prepared=false,synchronized=false,stopped=false,requestedStop=false,checking=false;QJsonObject result;QString lastStatus;
 const auto finish=[&]{if(prepared&&synchronized&&!requestedStop){requestedStop=true;runtime.stop();}};
 runtime.onStatus=[&](const QString& text){lastStatus=text;};
 runtime.onStopped=[&]{stopped=true;loop.quit();};
 runtime.onReady=[&](qint64){
  runtime.readLocalConnection([&](QJsonObject connection,QString error){
   if(!error.isEmpty()){result.insert("preparation_error",error);requestedStop=true;runtime.stop();return;}
   const auto bundle=root+"/EngineRuntime",program=bundle+"/network/horizon-engine-host"
#ifdef Q_OS_WIN
    ".exe"
#endif
    ;
   pgn::engine::localRuntimeQuery(&runtime,program,{"--prepare-local","--bundle",bundle,"--state-root",runtimeDirectory+"/prepared"},connection,[&](QJsonObject value,QString error){
    prepared=error.isEmpty()&&value.value("schema")=="PGN_LOCAL_RUNTIME_PROFILES_V1"&&value.value("mining_started")==false&&value.value("allocation_requested")==false&&!value.value("audit_identity").toObject().value("address").toString().isEmpty();
    if(!prepared){result.insert("preparation_error",error);requestedStop=true;runtime.stop();return;}
    result.insert("prepared_profiles",value);finish();
   });
  });
 };
 runtime.onObservation=[&](const std::optional<pgn::wallet::ChainObservation>& observation){
  if(requestedStop||checking||!observation||observation->height<1242||observation->peers.value_or(0)<1)return;
  if(!observation->synchronizing||*observation->synchronizing)return;checking=true;
  runtime.auditRequest("getblockhash",{1242},[&](QJsonValue value,QString error){
   checking=false;synchronized=error.isEmpty()&&value.toString()=="244f8fa23510d532ad33a28327fa0ac194597def267503089ab3dcb6bc1a5f2b";
   if(!synchronized)result.insert("checkpoint_error",error.isEmpty()?QString("Wrong native checkpoint"):error);
   finish();
  });
 };
 QObject::connect(&timeout,&QTimer::timeout,&loop,[&]{result.insert("timeout",true);requestedStop=true;runtime.stop();QTimer::singleShot(10000,&loop,&QEventLoop::quit);});
 timeout.start(240000);runtime.launch();loop.exec();
 result.insert("PASS",prepared&&synchronized&&stopped);result.insert("prepared",prepared);result.insert("synchronized",synchronized);result.insert("stopped",stopped);result.insert("last_status",lastStatus);result.insert("daemon_pid",QString::number(runtime.processId()));
 result.insert("scope","Actual isolated Core+wallet+integrated TLS+fresh local preparation; no ticket allocation or scientific computation");
 std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<'\n';return prepared&&synchronized&&stopped?0:6;
}

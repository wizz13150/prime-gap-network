#include "wallet/core_runtime.h"
#include <QCoreApplication>
#include <QTimer>
#include <iostream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=3)return 2;
 pgn::wallet::CoreRuntime runtime(QString::fromLocal8Bit(argv[1]),nullptr,true,QString::fromLocal8Bit(argv[2]));
 int result=1,step=0;bool started=false;QString failure;
 const auto finish=[&](QString error){failure=error;result=error.isEmpty()?0:1;runtime.stop();QTimer::singleShot(100,&app,&QCoreApplication::quit);};
 std::function<void()> next;
 next=[&]{
  if(step==0){++step;runtime.consoleRequest("echojson","[9007199254740993,1530002999806485841567,0.0000000001]",false,[&](QByteArray raw,QString error){if(!error.isEmpty()||!raw.contains("1530002999806485841567")||!raw.contains("9007199254740993")||!raw.contains("0.0000000001")){finish("Actual Core echo lost exact parameter/response tokens: "+error);return;}next();});}
  else if(step==1){++step;runtime.consoleRequest("getwalletinfo","[]",true,[&](QByteArray raw,QString error){if(!error.isEmpty()||!raw.contains("migration_qa")){finish("Displayed wallet scope failed: "+error);return;}next();});}
  else if(step==2){++step;runtime.consoleRequest("getconnectioncount","[]",false,[&](QByteArray raw,QString error){const auto value=QJsonDocument::fromJson(raw).object().value("result");if(!error.isEmpty()||value.toInt(-1)!=0){finish("Private fixture acquired a peer or read failed: "+error);return;}next();});}
  else if(step==3){++step;runtime.consoleRequest("not_a_core_method","[]",false,[&](QByteArray raw,QString error){if(!raw.isEmpty()||!error.contains("-32601")){finish("Actual Core RPC error not preserved: "+error);return;}next();});}
  else if(step==4){++step;runtime.consoleRequest("getblockchaininfo","[]",false,[&](QByteArray raw,QString error){if(!raw.isEmpty()||!error.contains("changed")){finish("A response was presented after Core stop was requested");return;}next();});runtime.stop();}
  else finish({});
 };
 runtime.onReady=[&](qint64){if(!started){started=true;next();}};
 QTimer::singleShot(45000,&app,[&]{finish("Owned console qualification timed out");});runtime.launch();app.exec();
 if(result){std::cerr<<failure.toStdString()<<'\n';return result;}
 std::cout<<"PASS actual owned Core: exact JSON beyond64bits, displayed wallet, no peers, real RPC error; no mining\n";
}

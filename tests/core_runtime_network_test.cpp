// Public inert fixtures only: real CoreRuntime, never Core/auth/wallet keys.
#include "wallet/core_runtime.h"
#include "wallet/core_profile.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>
namespace {
void need(bool good,const char* message){if(!good)throw std::runtime_error(message);}
void write(const QString& path,const QByteArray& bytes){QDir().mkpath(QFileInfo(path).absolutePath());QFile file(path);need(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size(),"Cannot write public QA fixture");}
QByteArray read(const QString& path){QFile file(path);need(file.open(QIODevice::ReadOnly),"Cannot read public QA fixture");return file.readAll();}
QStringList entries(const QString& path){return QDir(path).entryList(QDir::AllEntries|QDir::NoDotAndDotDot,QDir::Name);}
}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);unsigned cases{};
 for(const bool UItestnet:{false,true})for(const bool profileTestnet:{false,true})for(const bool populated:{false,true}){
  QTemporaryDir temporary;need(temporary.isValid(),"Temporary root unavailable");const auto root=temporary.path();
  const auto runtime=root+(UItestnet?"/data/testnet/ui-runtime":"/data/runtime");QDir().mkpath(runtime);
  const auto chain=profileTestnet?QString("test"):QString("main");
  const auto data=root+(profileTestnet?"/data/testnet/QA":"/data/wallet-core/QA");
  const QByteArray marker="Public synthetic existing-chain marker, not a wallet";
  if(populated){QDir().mkpath(data+"/blocks");QDir().mkpath(data+"/chainstate");write(data+"/public-chain-marker.txt",marker);}
  const auto code=root+"/inert-code-never-executed";const QByteArray codeBytes="Public inert code fixture";write(code,codeBytes);
  const bool mismatch=UItestnet!=profileTestnet;
  const auto codeHash=mismatch?QString(64,'a'):QString::fromLatin1(QCryptographicHash::hash(codeBytes,QCryptographicHash::Sha256).toHex());
  const QJsonObject receipt{{"schema","pgn-core-deployment-2"},{"product","PGN Core"},{"chain",chain},{"frontend","daemon"},{"wallet","QA"},
   {"executable",code},{"executable_sha256",codeHash},{"genesis_hash",QString(64,'b')},{"data_directory",data},{"rpc_port",30101},{"peer_port",30102}};
  need(pgn::wallet::CoreProfile::fromJson(receipt).validate(root).isEmpty(),"Public profile must be structurally valid");
  const auto receiptBytes=QJsonDocument(receipt).toJson();write(runtime+"/wallet-core.json",receiptBytes);
  const QByteArray intent="{\"schema\":\"QA_BLOCK_BEFORE_AUTH_OR_LAUNCH\"}";write(runtime+"/wallet-launch-intent.json",intent);
  // Mismatch must be rejected even when a later lifecycle lock cannot be used
  // and the later executable fingerprint deliberately would fail.
  if(mismatch)QDir().mkpath(runtime+"/wallet-owned.lock");
  const auto before=entries(runtime);QString status;
  {
   pgn::wallet::CoreRuntime core(root,nullptr,UItestnet,runtime);QEventLoop wait;QTimer timeout;timeout.setSingleShot(true);timeout.start(3000);
   QObject::connect(&timeout,&QTimer::timeout,&wait,&QEventLoop::quit);
   bool settled=false;
   core.onStatus=[&](const QString& value){status=value;if(value.contains("intent",Qt::CaseInsensitive)||(value.contains("profile",Qt::CaseInsensitive)&&value.contains("mainnet",Qt::CaseInsensitive)&&value.contains("testnet",Qt::CaseInsensitive))){settled=true;wait.quit();}};
   core.launch();if(!settled)wait.exec();
   need(core.processId()==0&&!core.connected(),"Network guard must never start or attach Core in this fixture");
   if(mismatch){
    need(status.contains("mainnet",Qt::CaseInsensitive)&&status.contains("testnet",Qt::CaseInsensitive)&&status.contains("profile",Qt::CaseInsensitive),"Wrong UI/profile network was not rejected before lifecycle/hash guard");
    need(entries(runtime)==before,"Wrong-network runtime wrote lifecycle files");
   }else{
    need(core.chainName()==chain&&status.contains("intent",Qt::CaseInsensitive),"Matching network must pass the network guard and stop at inert intent");
   }
  }
  need(read(runtime+"/wallet-core.json")==receiptBytes&&read(runtime+"/wallet-launch-intent.json")==intent,"Network check changed selected configuration/intent");
  need(QFileInfo::exists(data)==populated,"Network check generated a new chain/data directory");
  if(populated)need(read(data+"/public-chain-marker.txt")==marker,"Network check changed existing synthetic chain data");
  ++cases;
 }
 std::cout<<"PASS "<<cases<<" real CoreRuntime expected-network cases: both ctor modes, both profile chains, existing/missing data; no auth/Core/wallet/backend\n";
}

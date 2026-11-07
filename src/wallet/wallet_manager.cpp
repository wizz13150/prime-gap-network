#include "wallet/wallet_manager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

namespace pgn::wallet {
QString walletRpcPath(const QString& name){return "/wallet/"+QString::fromLatin1(QUrl::toPercentEncoding(name));}
QString validateNewWalletName(const QString& name){
 static const QRegularExpression pattern("^[A-Za-z0-9_-]{1,64}$");
 return pattern.match(name).hasMatch()?QString{}:QString("Use 1–64 letters, digits, underscores or hyphens. Paths are opened with Open wallet.");
}
WalletManager::WalletManager(WalletTransport transport,QObject* parent):QObject(parent),transport_(std::move(transport)){}
QString WalletManager::walletDirectory()const{return QDir(dataDirectory_).filePath(chain_=="test"?"testnet/wallets":"wallets");}
bool WalletManager::isLoaded(const QString& name)const{for(const auto& row:wallets_)if(row.toObject().value("name")==name&&row.toObject().value("loaded").toBool())return true;return false;}
void WalletManager::changed(const QString& message){message_=message;if(onChanged)onChanged();}
void WalletManager::invalidate(){++epoch_;++displayGeneration_;ready_=busy_=selecting_=displayOperation_=false;restore_.reset();wallets_={};changed("Connect the verified PGN Core to manage wallets.");}
void WalletManager::activate(const QString& genesis,const QString& chain,const QString& directory,const QString& service,const QString& state){
 ++epoch_;++displayGeneration_;genesis_=genesis;chain_=chain;dataDirectory_=QDir::cleanPath(directory);service_=display_=service;stateFile_=state;ready_=true;busy_=selecting_=displayOperation_=false;restore_.reset();
 refresh();
}
bool WalletManager::begin(){
 if(!ready_){changed("A verified PGN Core connection is required.");return false;}
 if(busy())return false;
 busy_=true;changed("Waiting for PGN Core…");return true;
}
void WalletManager::refresh(){
 if(!begin())return;const auto token=epoch_;const QPointer<WalletManager> weak(this);
 transport_("listwallets",{}, {},[weak,token](QJsonValue loaded,QString error){
  if(!weak||weak->epoch_!=token)return;
  if(!error.isEmpty()||!loaded.isArray()||loaded.toArray().size()>1000){weak->busy_=false;weak->changed(error.isEmpty()?"Core returned an invalid wallet list.":error);return;}
  weak->transport_("listwalletdir",{}, {},[weak,token,loaded=loaded.toArray()](QJsonValue available,QString error){
   if(!weak||weak->epoch_!=token)return;
   const auto object=available.toObject();
   if(!error.isEmpty()||!object.value("wallets").isArray()||object.value("wallets").toArray().size()>1000){weak->busy_=false;weak->changed(error.isEmpty()?"Core returned an invalid wallet directory.":error);return;}
   QSet<QString> names,active;for(const auto& value:loaded){if(!value.isString()||value.toString().size()>4096){weak->busy_=false;weak->changed("Core returned an invalid wallet name.");return;}names.insert(value.toString());active.insert(value.toString());}
   for(const auto& value:object.value("wallets").toArray()){if(!value.isObject()||!value.toObject().value("name").isString()||value.toObject().value("name").toString().size()>4096){weak->busy_=false;weak->changed("Core returned an invalid available wallet.");return;}names.insert(value.toObject().value("name").toString());}
   auto sorted=names.values();std::sort(sorted.begin(),sorted.end());weak->wallets_={};
   for(const auto& name:sorted)weak->wallets_.append(QJsonObject{{"name",name},{"loaded",active.contains(name)},{"displayed",name==weak->display_},{"service",name==weak->service_}});
   weak->busy_=false;weak->changed("Wallet list refreshed.");
   // Restore only a wallet independently reported loaded by this Core. Core's
   // load_on_startup owns persistence of loaded wallets; no hidden creation.
   QFile saved(weak->stateFile_);if(saved.open(QIODevice::ReadOnly)&&saved.size()<=16384){
    const auto json=QJsonDocument::fromJson(saved.readAll()).object();const auto name=json.value("display_wallet").toString();
    if(json.value("schema")=="PGN_WALLET_VIEW_V1"&&json.value("genesis")==weak->genesis_&&json.value("chain")==weak->chain_&&json.value("data_directory")==weak->dataDirectory_&&json.value("service_wallet")==weak->service_&&name!=weak->display_&&active.contains(name)){
     if(weak->displayOperation_)weak->restore_=name;else weak->selectVerified(name,false);
    }
   }
  });
 });
}
bool WalletManager::holdDisplayOperation(){
 if(!ready_||selecting_||displayOperation_)return false;
 displayOperation_=true;changed("A wallet operation is in progress. Keep this wallet selected until Core confirms the result.");return true;
}
void WalletManager::finishDisplayOperation(quint64 generation){
 if(generation!=displayGeneration_||!displayOperation_)return;
 displayOperation_=false;changed("Wallet operation finished.");
 if(restore_){const auto name=*restore_;restore_.reset();selectVerified(name,false);}
}
QString WalletManager::persistDisplay(const QString& name){
 if(stateFile_.isEmpty())return "Wallet selected for this session; a settings path is unavailable.";
 const auto bytes=QJsonDocument(QJsonObject{{"schema","PGN_WALLET_VIEW_V1"},{"genesis",genesis_},{"chain",chain_},{"data_directory",dataDirectory_},{"service_wallet",service_},{"display_wallet",name}}).toJson(QJsonDocument::Compact);
 QSaveFile file(stateFile_);if(bytes.size()>16384||!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return "Wallet selected for this session; the display selection could not be saved.";
 return {};
}
void WalletManager::selectVerified(const QString& name,bool save){
 if(!begin())return;selecting_=true;const auto token=epoch_;const QPointer<WalletManager> weak(this);
 transport_("getwalletinfo",{},name.isNull()?QString(""):name,[weak,token,name,save](QJsonValue value,QString error){
  if(!weak||weak->epoch_!=token)return;weak->busy_=weak->selecting_=false;
  if(!error.isEmpty()||!value.isObject()||value.toObject().value("walletname").toString()!=name){weak->changed(error.isEmpty()?"Loaded wallet identity did not match the selection.":error);return;}
  weak->display_=name;++weak->displayGeneration_;
  for(auto it=weak->wallets_.begin();it!=weak->wallets_.end();++it){auto row=it->toObject();row.insert("displayed",row.value("name")==name);*it=row;}
  const auto warning=save?weak->persistDisplay(name):QString{};weak->changed(warning.isEmpty()?"Displayed wallet changed. Mining and audit service wallet is unchanged.":warning);
 });
}
void WalletManager::select(const QString& name){
 if(name==display_)return;
 for(const auto& row:wallets_)if(row.toObject().value("name")==name&&row.toObject().value("loaded").toBool()){selectVerified(name,true);return;}
 changed("Load the wallet before selecting it.");
}
void WalletManager::create(const QString& name){const auto error=validateNewWalletName(name);if(!error.isEmpty()){changed(error);return;}operate("createwallet",name,true);}
void WalletManager::open(const QString& path){
 const QFileInfo info(path);
 if(!info.isAbsolute()||!info.exists()||(!info.isDir()&&!(info.isFile()&&info.suffix().compare("dat",Qt::CaseInsensitive)==0))){changed("Choose an existing wallet directory or .dat wallet file using an absolute path.");return;}
 operate("loadwallet",QDir::fromNativeSeparators(info.canonicalFilePath()),false);
}
void WalletManager::load(const QString& name){
 for(const auto& row:wallets_)if(row.toObject().value("name")==name){if(row.toObject().value("loaded").toBool()){changed("This wallet is already loaded.");return;}operate("loadwallet",name,false);return;}
 changed("Refresh and choose a wallet from the available list, or use Open wallet.");
}
void WalletManager::operate(const QString& method,const QString& name,bool create){
 if(!begin())return;const auto token=epoch_;const QPointer<WalletManager> weak(this);
 // PGN Core fork: parameter 5 is load_on_startup, parameter 6 external_signer.
 // It always creates descriptor wallets. Do not copy an older Bitcoin arity.
 const auto args=create?QJsonArray{name,false,false,QJsonValue{},false,true,false}:QJsonArray{name,true};
 transport_(method,args,{},[weak,token](QJsonValue value,QString error){
  if(!weak||weak->epoch_!=token)return;weak->busy_=false;
  if(!error.isEmpty()){weak->changed(error+" Existing wallet data has been preserved. Refresh before retrying.");return;}
  if(!value.isObject()||!value.toObject().value("name").isString()){weak->changed("Core did not confirm the wallet name. Refresh before retrying.");return;}
  weak->changed("Wallet loaded. Select it to display its balances and history.");weak->refresh();
 });
}
void WalletManager::unload(const QString& name){
 if(name==service_){changed("This wallet is reserved for mining and audit services. It stays loaded so signed work keeps its original wallet.");return;}
 if(name==display_){changed("Select another wallet before closing the displayed wallet.");return;}
 bool loaded=false;for(const auto& row:wallets_)loaded|=row.toObject().value("name")==name&&row.toObject().value("loaded").toBool();
 if(!loaded){changed("This wallet is not loaded.");return;}
 if(!begin())return;const auto token=epoch_;const QPointer<WalletManager> weak(this);
 transport_("unloadwallet",{name,false},{},[weak,token](QJsonValue,QString error){if(!weak||weak->epoch_!=token)return;weak->busy_=false;if(!error.isEmpty()){weak->changed(error);return;}weak->refresh();});
}
}

#include "wallet/public_work_snapshot.h"
#include "wallet/audit_activity_snapshot.h"
#include "wallet/ticket_atlas_snapshot.h"
#include "wallet/confirmed_snapshot_anchor.h"
#include "wallet/explorer_projection.h"
#include <algorithm>
#include "wallet/core_runtime.h"
#include "wallet/core_rpc_stream.h"
#include "wallet/setup_deployment.h"
#include "wallet/participant_snapshot.h"
#include "wallet/network_metrics_snapshot.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStringList>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QCoreApplication>
#include <QUuid>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkProxy>
#include <QtConcurrent/QtConcurrentRun>
#include <cerrno>
#include <limits>
#ifdef Q_OS_LINUX
#include <signal.h>
#endif
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace pgn::wallet {
namespace {
#ifdef Q_OS_WIN
bool processIdentity(HANDLE handle,QString& birth,QString& image){
 FILETIME created,exited,kernel,user;
 if(!GetProcessTimes(handle,&created,&exited,&kernel,&user))return false;
 const quint64 ticks=(static_cast<quint64>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
 birth=QString::number(ticks);wchar_t path[32768];DWORD count=32768;
 if(!QueryFullProcessImageNameW(handle,0,path,&count))return false;
 image=QString::fromWCharArray(path,count);return true;
}
#elif defined(Q_OS_LINUX)
bool processIdentity(qint64 pid,QString& birth,QString& image){
 if(pid<=0||pid>std::numeric_limits<int>::max())return false;
 const auto root=QString("/proc/%1/").arg(pid);QFile stat(root+"stat");if(!stat.open(QIODevice::ReadOnly))return false;const auto bytes=stat.read(8193);if(bytes.size()>8192)return false;const auto end=bytes.lastIndexOf(')');if(end<0)return false;const auto fields=bytes.sliced(end+2).split(' ');if(fields.size()<=19||fields[0]=="Z"||fields[0]=="X")return false;bool valid=false;const auto ticks=fields[19].toULongLong(&valid);if(!valid||!ticks)return false;
 QFile boot("/proc/sys/kernel/random/boot_id");if(!boot.open(QIODevice::ReadOnly))return false;const auto id=boot.read(128).trimmed();if(id.size()!=36)return false;image=QFileInfo(root+"exe").symLinkTarget();if(image.isEmpty()||!QFileInfo(image).isFile())return false;birth=QString::fromLatin1(id)+":"+QString::number(ticks);return true;
}

#endif
}
CoreRuntime::CoreRuntime(QString root,QObject* parent,bool testnet,QString directory):QObject(parent),root_(std::move(root)),runtimeDirectory_(directory.isEmpty()?root_+(testnet?"/data/testnet/ui-runtime":"/data/runtime"):std::move(directory)),expectedChain_(testnet?QStringLiteral("test"):QStringLiteral("main")){
 profile_.chain=expectedChain_;
 clock_.setInterval(1000);connect(&clock_,&QTimer::timeout,this,[this]{inspect();});
 network_.setProxy(QNetworkProxy::NoProxy);
 walletManager_=new WalletManager([this](const QString& method,const QJsonArray& params,const QString& name,WalletReply consume){
  if(!connected_||!processAlive()){consume({},"Verified Core connection required");return;}
  const auto owner=epoch();rpc(method,params,[this,owner,consume=std::move(consume)](QJsonValue value,QString error){
   if(!owner.matches(epoch())||!connected_||!processAlive()){consume({},"Core connection changed; wallet operation must be rechecked");return;}consume(value,error);
  },!name.isNull(),120000,name);
 },this);
 // Redirect to preserved files rather than allowing an unread process pipe
 // to block the wallet. They contain no candidate stream from the GPU.
 connect(&verifier_,&QFutureWatcher<QString>::finished,this,[this]{
  const QString digest=verifier_.result();
  if(digest.compare(profile_.executableSha256,Qt::CaseInsensitive)!=0){failStart("Core executable fingerprint mismatch; not launched");return;}
  // The intent survives a crash between detachment and durable ownership.
  // A valid live owner can settle it; an unknown child cannot justify retry.
  QFile intent(runtimeDirectory_+"/wallet-launch-intent.json");
  const bool pending=intent.exists();
  if(pending){
   if(!intent.open(QIODevice::ReadOnly)||intent.size()>8192){failStart("Unresolved Core launch intent cannot be read; no new client launched");return;}
   const auto invalid=verifyCoreLaunchIntent(profile_,QJsonDocument::fromJson(intent.readAll()).object());intent.close();
   if(!invalid.isEmpty()){failStart(invalid);return;}
  }
  if(recoverOwned()){
   if(pending&&!intent.remove())report("OWNED CORE RECOVERED · launch intent retained for repair");
   return;
  }
  if(!launching_)return; // Invalid/ambiguous existing owner fails closed.
  if(pending){failStart("An unresolved detached Core may still exist; preserved launch intent blocks a duplicate client");return;}
  if(!allowNew_){failStart("No live owned Core to reattach; no new client launched");return;}
   const auto setupError=activatePendingWalletSetup(runtimeDirectory_,root_,profile_);
   if(!setupError.isEmpty()){failStart(setupError);return;}
  if(!QDir().mkpath(profile_.dataDirectory)){failStart("Private PGN wallet directory could not be created");return;}
  QProcess launcher;launcher.setProgram(profile_.executable);
  QStringList args{"-datadir="+profile_.dataDirectory,profile_.chain=="test"?QString("-testnet=1"):QString("-chain=main"),"-server=1",
   "-rpcbind=127.0.0.1","-rpcallowip=127.0.0.1","-rpcport="+QString::number(profile_.rpcPort),
   "-port="+QString::number(profile_.peerPort),"-dnsseed=0","-fixedseeds=0","-discover=0",
   // The managed desktop client joins through outbound peers/the TLS relay.
   // It does not need a public P2P listener or automatic router mappings.
   // Standalone seed/server nodes are launched separately and are unchanged.
   "-listen=0","-listenonion=0","-natpmp=0"};
  if(profile_.frontend=="qt")args.prepend("-min");
  for(const auto& peer:profile_.peers)args.append("-connect="+peer);
  launcher.setArguments(args);launcher.setWorkingDirectory(QFileInfo(profile_.executable).absolutePath());
  launcher.setStandardOutputFile(profile_.dataDirectory+"/core.stdout",QIODevice::Append);
  launcher.setStandardErrorFile(profile_.dataDirectory+"/core.stderr",QIODevice::Append);
  QSaveFile prepared(intent.fileName());
  const auto journal=QJsonDocument(coreLaunchIntent(profile_,QUuid::createUuid().toString(QUuid::Id128),
      QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))).toJson(QJsonDocument::Compact);
  if(!prepared.open(QIODevice::WriteOnly)||prepared.write(journal)!=journal.size()||!prepared.commit()){
   failStart("Core launch intent could not be persisted; no client launched");return;
  }
  if(!launcher.startDetached(&pid_)){
   // startDetached returned false: no owned child was created. Removal only
   // settles this metadata intent, never wallet data or a process.
   if(!intent.remove()){failStart("Core did not start; launch intent retained for repair");return;}
   failStart("Owned Core client could not be launched");return;
  }
#ifdef Q_OS_WIN
  processHandle_=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(pid_));
#endif
  if(
#ifdef Q_OS_WIN
     !processHandle_||
#endif
     !persistOwned()){failStart("Core ownership could not be persisted; client left intact, integration/mutations refused");return;}
  if(!intent.remove()){failStart("Core owner saved but launch intent could not be settled; reattach exact owner before any new launch");return;}
  launching_=false;
  clock_.start();report("CORE STARTED · checking exact chain identity");inspect();
 });
}
CoreRuntime::~CoreRuntime(){
 clock_.stop();
 connected_=false;peerBootstrap_.reset();
 // Detached Core owns its lifetime/data. Closing the GUI does not kill it.
#ifdef Q_OS_WIN
 if(processHandle_)CloseHandle(static_cast<HANDLE>(processHandle_));
#endif
}
void CoreRuntime::failStart(const QString& error){
 setLinkState(CoreLinkState::Rejected,error);
 connected_=false;
  peerBootstrap_.reset();
 ++identityGeneration_;
 launching_=false;walletChecking_=walletReady_=false;
 // If detachment succeeded but binding failed, never retry another launch in
 // this GUI. The Core data lock/unknown client must be inspected externally.
 if(pid_>0)orphanedStart_=true;
#ifdef Q_OS_WIN
 if(processHandle_){CloseHandle(static_cast<HANDLE>(processHandle_));processHandle_=nullptr;}
#endif
 pid_=0;processBirth_.clear();if(ownershipLock_)ownershipLock_->unlock();report(error);
}
bool CoreRuntime::persistOwned(){
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
 QString image;
#ifdef Q_OS_WIN
 if(!processIdentity(static_cast<HANDLE>(processHandle_),processBirth_,image))return false;
#else
 if(!processIdentity(pid_,processBirth_,image))return false;
#endif
 const auto binding=coreOwnershipBinding(profile_,pid_,processBirth_);
 if(!verifyCoreOwnership(profile_,binding,pid_,processBirth_,image).isEmpty())return false;
 QSaveFile file(runtimeDirectory_+"/wallet-owned.json");
 if(!file.open(QIODevice::WriteOnly))return false;
 const auto bytes=QJsonDocument(binding).toJson(QJsonDocument::Compact);
 return file.write(bytes)==bytes.size()&&file.commit();
#else
 return false;
#endif
}
bool CoreRuntime::recoverOwned(){
 QFile file(runtimeDirectory_+"/wallet-owned.json");if(!file.exists())return false;
 if(!file.open(QIODevice::ReadOnly)||file.size()>8192){failStart("Saved Core ownership cannot be read safely");return false;}
 const auto saved=QJsonDocument::fromJson(file.readAll()).object();
 bool valid=false;const auto savedPid=saved.value("pid").toString().toLongLong(&valid);
 const auto savedBirth=saved.value("birth").toString();
 if(!valid||savedPid<=0||savedPid>0xffffffffLL||
    !verifyCoreOwnership(profile_,saved,savedPid,savedBirth,profile_.executable).isEmpty()){
  failStart("Saved Core owner belongs to another deployment or is invalid; no second client launched");return false;
 }
#ifdef Q_OS_WIN
 HANDLE handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(savedPid));
 if(!handle){
  if(GetLastError()==ERROR_INVALID_PARAMETER)return false; // Exact PID no longer exists.
  failStart("Saved Core process cannot be inspected; no second client launched");return false;
 }
 if(WaitForSingleObject(handle,0)==WAIT_OBJECT_0){CloseHandle(handle);return false;}
 QString birth,image;const bool identified=processIdentity(handle,birth,image);
 if(!identified){CloseHandle(handle);failStart("Saved Core process identity cannot be read; no second client launched");return false;}
 // A different creation time proves that the saved process no longer exists.
 // Leave the new occupant untouched and use the ordinary absent-owner path.
 // Launch intents and Core's data lock still govern any new launch.
 if(birth!=savedBirth){CloseHandle(handle);report("Previous Core process exited; Windows reused its number");return false;}
 if(!verifyCoreOwnership(profile_,saved,savedPid,birth,image).isEmpty()){
  CloseHandle(handle);failStart("Saved Core image changed within the same process; no integration or second launch");return false;
 }
 processHandle_=handle;pid_=savedPid;processBirth_=birth;launching_=false;
 clock_.start();report("EXISTING OWNED CORE RECOVERED · checking exact chain identity");inspect();return true;
#elif defined(Q_OS_LINUX)
 QString birth,image;if(!processIdentity(savedPid,birth,image)){if(::kill(pid_t(savedPid),0)<0&&errno==ESRCH)return false;failStart("Saved Core process identity could not be inspected");return false;}
 if(birth!=savedBirth){report("Previous Core process exited; its number was reused");return false;}
 if(!verifyCoreOwnership(profile_,saved,savedPid,birth,image).isEmpty()){failStart("Saved Core image changed within the same process");return false;}pid_=savedPid;processBirth_=birth;launching_=false;clock_.start();report("EXISTING OWNED CORE RECOVERED · checking exact chain identity");inspect();return true;
#else
 failStart("Native process ownership is not qualified on this platform");return false;
#endif
}
bool CoreRuntime::processAlive(){
#ifdef Q_OS_WIN
 if(processHandle_&&WaitForSingleObject(static_cast<HANDLE>(processHandle_),0)==WAIT_TIMEOUT)return true;
 if(processHandle_){CloseHandle(static_cast<HANDLE>(processHandle_));processHandle_=nullptr;}
#elif defined(Q_OS_LINUX)
 if(pid_>0){QString birth,image;if(processIdentity(pid_,birth,image)&&birth==processBirth_&&QFileInfo(image).canonicalFilePath()==QFileInfo(profile_.executable).canonicalFilePath())return true;}
#endif
 if(pid_){++identityGeneration_;pid_=0;processBirth_.clear();clock_.stop();connected_=false;checking_=false;stopping_=false;walletChecking_=walletReady_=false;
   peerBootstrap_.reset();
  walletManager_->invalidate();setLinkState(CoreLinkState::Stopped,"The owned Core process exited");
  if(onObservation)onObservation(std::nullopt);
  if(ownershipLock_)ownershipLock_->unlock();
  if(onStopped)onStopped();report("CORE PROCESS EXITED · wallet detached from this view");}
 return false;
}
void CoreRuntime::report(const QString& text){if(onStatus)onStatus(text);}
void CoreRuntime::setLinkState(CoreLinkState state,const QString& detail){
 const auto bounded=detail.left(480);
 if(linkState_==state&&linkDetail_==bounded)return;
 linkState_=state;linkDetail_=bounded;
 if(onLinkState)onLinkState(state,bounded);
}
void CoreRuntime::launch(){
 begin(true);
}
void CoreRuntime::reattach(){
 if(QFileInfo::exists(runtimeDirectory_+"/wallet-owned.json"))begin(false);
}
void CoreRuntime::begin(bool allowNew){
 if(orphanedStart_){report("An unbound detached Core may still exist; no second launch permitted");return;}
 if(launching_||processAlive()){report("The owned Core client is already starting/running");return;}
 ++identityGeneration_;
 setLinkState(CoreLinkState::Starting,"Checking the owned Core deployment");
 QFile receipt(runtimeDirectory_+"/wallet-core.json");
 if(!receipt.open(QIODevice::ReadOnly)||receipt.size()>32768){failStart("No verified PGN Core build/deployment receipt; no wallet launched");return;}
 const auto configuredProfile=CoreProfile::fromJson(QJsonDocument::fromJson(receipt.readAll()).object());
 const QString invalid=configuredProfile.validate(root_);if(!invalid.isEmpty()){failStart(invalid);return;}
 if(configuredProfile.chain!=expectedChain_){
  failStart(QString("This wallet is set to %1, but the saved Core profile is for %2. Open wallet setup to use a matching profile.")
      .arg(expectedChain_=="test"?QString("testnet"):QString("mainnet"),configuredProfile.chain=="test"?QString("testnet"):QString("mainnet")));return;
 }
 profile_=configuredProfile;
 if(!QDir().mkpath(runtimeDirectory_)){failStart("Wallet ownership directory unavailable");return;}
 ownershipLock_=std::make_unique<QLockFile>(runtimeDirectory_+"/wallet-owned.lock");
 if(!ownershipLock_->tryLock(0)){failStart("Another PGN GUI owns wallet lifecycle; no duplicate client launched");return;}
 allowNew_=allowNew;
 launching_=true;report("Checking pinned Core executable once, outside the GUI/mining loop");
 const QString executable=profile_.executable;
 verifier_.setFuture(QtConcurrent::run([executable]{
  QFile file(executable);if(!file.open(QIODevice::ReadOnly))return QString{};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while(!file.atEnd()){const auto bytes=file.read(1048576);if(bytes.isEmpty()&&file.error()!=QFile::NoError)return QString{};hash.addData(bytes);}
  return QString::fromLatin1(hash.result().toHex());
 }));
}
void CoreRuntime::rpc(const QString& method,const QJsonArray& params,std::function<void(QJsonValue,QString)> consume,bool wallet,int timeoutMs,const QString& walletName){
 QFile cookie(profile_.networkDataDirectory()+"/.cookie");
 if(!cookie.open(QIODevice::ReadOnly)||cookie.size()>4096){consume({},"Core RPC cookie is not ready");return;}
 const QString endpoint=wallet?walletRpcPath(walletName.isNull()?profile_.walletName:walletName):QString("/");
 QNetworkRequest request(QUrl::fromEncoded(("http://127.0.0.1:"+QString::number(profile_.rpcPort)+endpoint).toUtf8()));
 request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
 request.setRawHeader("Authorization","Basic "+cookie.readAll().trimmed().toBase64());
 request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
 request.setTransferTimeout(std::clamp(timeoutMs,1000,120000));
 const QString requestId=QUuid::createUuid().toString(QUuid::WithoutBraces);
 const QJsonObject call{{"jsonrpc","2.0"},{"id",requestId},{"method",method},{"params",params}};
 auto* reply=network_.post(request,QJsonDocument(call).toJson(QJsonDocument::Compact));
 readBoundedCoreRpcReply(reply,this,coreRpcResponseLimit(method),[reply,requestId,method,consume=std::move(consume)](QByteArray bytes,bool oversized){
  const auto error=reply->error();const auto http=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const auto detail=reply->errorString().left(160);reply->deleteLater();
  if(oversized){consume({},QString("Core RPC response exceeds %1 byte bound").arg(coreRpcResponseLimit(method)));return;}
  const auto decoded=decodeCoreRpcResponse(bytes,requestId,method);
  if(error!=QNetworkReply::NoError&&!verifiedRpcHttpError(decoded,http)){consume({},QString("Core RPC read failed (%1): %2").arg(int(error)).arg(detail));return;}
  consume(decoded.value,decoded.error);
 });
}
void CoreRuntime::walletRequest(const QString& method,const QJsonArray& params,std::function<void(QJsonValue,QString)> consume){
 static const QStringList allowed{"getaddressinfo","getwalletinfo","getbalances","listtransactions","listreceivedbyaddress","getnewaddress","validateaddress","sendtoaddress","walletpassphrase","walletlock"};
 if(!allowed.contains(method)||!displayWalletReady()||!processAlive()){consume({},"Verified loaded display wallet required");return;}
 const auto owner=epoch();const auto display=walletManager_->displayGeneration();const auto name=walletManager_->displayWallet();
 if(walletManager_->selecting()){consume({},"Wait for the wallet selection to finish");return;}
 const bool mutating=QStringList{"getnewaddress","sendtoaddress","walletpassphrase","walletlock"}.contains(method);
 if(mutating&&!walletManager_->holdDisplayOperation()){consume({},"Wait for the current wallet operation to finish");return;}
 rpc(method,params,[this,owner,display,mutating,consume=std::move(consume)](QJsonValue value,QString error){
    if(mutating)walletManager_->finishDisplayOperation(display);
   if(!owner.matches(epoch())||!connected_||!processAlive()||display!=walletManager_->displayGeneration()){consume({},"Displayed wallet changed; result discarded");return;}
   consume(value,error);
  },true,3000,name.isNull()?QString(""):name);
}
void CoreRuntime::serviceWalletRequest(const QString& method,const QJsonArray& params,WalletReply consume){
 if(method!="getaddressinfo"||!connected_||!walletReady_||!processAlive()){consume({},"Verified service wallet required");return;}
 const auto owner=epoch();rpc(method,params,[this,owner,consume=std::move(consume)](QJsonValue value,QString error){if(!owner.matches(epoch())||!connected_||!processAlive()){consume({},"Service wallet connection changed");return;}consume(value,error);},true);
}
void CoreRuntime::importPrivateDescriptors(const QJsonArray& descriptors,bool scanHistory,std::function<void(QJsonValue,QString)> consume){
 if(!displayWalletReady()||!processAlive()||descriptors.isEmpty()||descriptors.size()>1000){consume({},"Verified wallet and1..1000private descriptors required");return;}
 const QRegularExpression pattern("^wpkh\\(prv[0-9a-f]{64}\\)#[a-z0-9]{8}$");QJsonArray requests;
 for(const auto& value:descriptors){if(!value.isString()||!pattern.match(value.toString()).hasMatch()){consume({},"Unsupported native PGN private-key descriptor");return;}requests.append(QJsonObject{{"desc",value},{"timestamp",scanHistory?QJsonValue(0):QJsonValue("now")},{"active",false},{"label","Horizon vanity"}});}
 const auto owner=epoch();const auto count=descriptors.size();const auto display=walletManager_->displayGeneration();const auto name=walletManager_->displayWallet();
 if(!walletManager_->holdDisplayOperation()){consume({},"Wait for the current wallet operation to finish");return;}
 QJsonArray params;params.append(requests);
 rpc("importdescriptors",params,[this,owner,count,display,consume=std::move(consume)](QJsonValue value,QString error){
  walletManager_->finishDisplayOperation(display);
  if(!owner.matches(epoch())||!connected_||!processAlive()||display!=walletManager_->displayGeneration()){consume({},"Wallet connection changed; import outcome must be rechecked in that wallet");return;}
  const QRegularExpression secret("prv[0-9a-fA-F]{64}");error.replace(secret,"[private key]");
  if(!error.isEmpty()){consume({},"Import outcome could not be confirmed. Recheck the addresses before retrying. "+error);return;}
  if(!value.isArray()||value.toArray().size()!=count){consume({},"Import reply incomplete; recheck the selected addresses");return;}
  QJsonArray safe;
  for(const auto& entry:value.toArray()){auto result=entry.toObject();auto failure=result.value("error").toObject();auto message=failure.value("message").toString();message.replace(secret,"[private key]");if(!failure.isEmpty()){failure.insert("message",message);result.insert("error",failure);}safe.append(result);}
  consume(safe,{});
 },true,120000,name.isNull()?QString(""):name);
}
void CoreRuntime::auditRequest(const QString& method,const QJsonArray& params,std::function<void(QJsonValue,QString)> consume){
 static const QStringList allowed{"getblock","getpgnauditevent","getpgnauditstate","getpgnreports","getpgnscientificwork","getpgnbootstraptemplate","submitblock","getblockhash","getbestblockhash","getblockheader"};
 if(!allowed.contains(method)||!connected_||!processAlive()){consume({},"Verified Core audit connection required");return;}
 const auto owner=epoch();rpc(method,params,[this,owner,consume=std::move(consume)](QJsonValue value,QString error){
  if(!owner.matches(epoch())||!connected_||!processAlive()){consume({},"Core audit epoch changed; result discarded");return;}
  consume(value,error);
 });
}
void CoreRuntime::signAuditChallenge(const QString& address,const QString& challenge,std::function<void(QJsonValue,QString)> consume){
 if(address.isEmpty()||!challenge.startsWith("PGN/UNAPPROVED/SCRIPT-WORK/1:")||challenge.size()>256||!connected_||!walletReady_||!processAlive()){consume({},"Verified native audit challenge required");return;}
 const auto owner=epoch();rpc("signmessage",{address,challenge},[this,owner,consume=std::move(consume)](QJsonValue value,QString error){
  if(!owner.matches(epoch())||!connected_||!processAlive()){consume({},"Wallet audit epoch changed; signature discarded");return;}
  consume(value,error);
 },true);
}
void CoreRuntime::readLocalConnection(std::function<void(QJsonObject,QString)> consume){
 if(!connected_||!walletReady_||!processAlive()){consume({},"Verified initialized wallet required before preparing a local worker");return;}
 if(profile_.chain!="test"||profile_.walletName.isEmpty()){consume({},"Local testnet worker requires an explicit testnet wallet");return;}
 const auto cookie=profile_.networkDataDirectory()+"/.cookie";
 if(!QFileInfo(cookie).isFile()){consume({},"Owned Core authentication file is unavailable");return;}
 consume(QJsonObject{{"schema","PGN_CORE_LOCAL_CONNECTION_V1"},{"genesis",profile_.genesisHash},
  {"rpc_port",profile_.rpcPort},{"cookie_file",cookie},{"wallet",profile_.walletName},
  {"data_directory",profile_.dataDirectory},{"daemon_pid",QString::number(pid_)},
  {"daemon_birth",processBirth_},{"identity_generation",QString::number(identityGeneration_)}},{});
}
void CoreRuntime::readPeerSnapshot(std::function<void(QJsonArray,QString)> consume){
 if(!connected_||!processAlive()){consume({},"Verified Core connection required");return;}
 const auto owner=epoch();
 rpc("getpeerinfo",{},[this,owner,consume=std::move(consume)](QJsonValue value,QString error){
  if(!owner.matches(epoch())||!connected_||!processAlive()){consume({},"Core connection changed");return;}
  if(!error.isEmpty()||!value.isArray()||value.toArray().size()>256){consume({},error.isEmpty()?QString("Invalid or oversized peer snapshot"):error);return;}
  consume(value.toArray(),{});
 });
}
void CoreRuntime::readNetworkSnapshot(std::function<void(QJsonObject,QString)> consume){
 if(!connected_||!processAlive()){consume({},"Verified Core connection required");return;}
 const auto owner=epoch();
 rpc("getpgnstate",{},[this,owner,consume=std::move(consume)](QJsonValue value,QString error){
  if(!owner.matches(epoch())||!connected_||!processAlive()){consume({},"Core epoch changed");return;}
  if(!error.isEmpty()||!value.isObject()){consume({},error.isEmpty()?QString("Native state unavailable"):error);return;}
  const auto state=value.toObject();const auto hash=state.value("bestblockhash").toString();
  if(state.value("genesis").toString()!=profile_.genesisHash||hash.size()!=64||state.value("height").toInteger(-1)<0){consume({},"Native state identity mismatch");return;}
  networkHeaders(owner,state,hash,12,{},std::move(consume));
 });
}
void CoreRuntime::networkHeaders(const CoreEpoch& owner,QJsonObject state,const QString& hash,int left,QJsonArray rows,std::function<void(QJsonObject,QString)> consume){
 rpc("getblockheader",{hash,true},[this,owner,state,hash,left,rows=std::move(rows),consume=std::move(consume)](QJsonValue value,QString error)mutable{
  if(!owner.matches(epoch())||!connected_||!processAlive()){consume({},"Core epoch changed");return;}
  const auto header=value.toObject();const auto height=header.value("height").toInteger(-1);
  if(!error.isEmpty()||header.value("hash").toString()!=hash||height!=state.value("height").toInteger()-rows.size()||!header.value("nTx").isDouble()||header.value("nTx").toInteger(-1)<1){
   consume({},error.isEmpty()?QString("Native ancestor identity mismatch"):error);return;
  }
  rows.append(header);
  if(height>0&&left>1){const auto previous=header.value("previousblockhash").toString();if(previous.size()!=64){consume({},"Native parent missing");return;}
   networkHeaders(owner,state,previous,left-1,std::move(rows),std::move(consume));return;}
  verifyConfirmedSnapshotAnchor(state,[this,owner]{return owner.matches(epoch())&&connected_&&processAlive();},
   [this](qint64 height,auto done){rpc("getblockhash",{height},std::move(done));},
   [state,rows,consume=std::move(consume)](QString error){
    if(!error.isEmpty()){consume({},error);return;}
    auto result=state;result.insert("blocks",rows);result.insert("observed_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));consume(result,{});
   });
 });
}
void CoreRuntime::readWorkView(const QString& kind,int offset,int limit,const QString& request,std::function<void(QJsonObject,QString)> consume){
 static const QRegularExpression canonical("^[0-9a-f]{64}$");
 if(!connected_||!processAlive()||offset<0||limit<1||limit>100||(!request.isEmpty()&&(!canonical.match(request).hasMatch()||request==QString(64,'0')||offset!=0||limit!=1))){consume({},"Verified Core connection and bounded work lookup required");return;}
 const auto owner=epoch();QJsonArray params{kind,offset,limit};if(!request.isEmpty())params.append(request);
 rpc("getpgnworkview",params,[this,owner,kind,offset,limit,request,consume=std::move(consume)](QJsonValue value,QString error){
  if(!error.isEmpty()||!owner.matches(epoch())||!connected_||!processAlive()){consume({},error.isEmpty()?QString("Core work view connection changed"):error);return;}
  const auto view=value.toObject();const auto invalid=validatePublicWorkSnapshot(view,profile_.genesisHash,kind,offset,limit,request);if(!invalid.isEmpty()){consume({},invalid);return;}
  verifyConfirmedSnapshotAnchor(view.value("state").toObject(),[this,owner]{return owner.matches(epoch())&&connected_&&processAlive();},[this](qint64 height,auto done){rpc("getblockhash",{height},std::move(done));},[view,consume=std::move(consume)](QString error){consume(error.isEmpty()?view:QJsonObject{},error);});
 });
}
void CoreRuntime::readAuditActivity(qint64 afterHeight,const QString& afterHash,const QJsonArray& watched,std::function<void(QJsonObject,QString)> consume){
 static const QRegularExpression hash("^[0-9a-f]{64}$");
 if(!connected_||!processAlive()||afterHeight<0||watched.size()>64||(afterHeight>0&&!hash.match(afterHash).hasMatch())){consume({},"Verified Core and bounded audit cursor required");return;}
 const auto owner=epoch();
 rpc("getpgnauditactivity",{afterHeight,64,watched},[this,owner,afterHeight,afterHash,watched,consume=std::move(consume)](QJsonValue result,QString error){
  if(!error.isEmpty()||!owner.matches(epoch())||!connected_||!processAlive()){consume({},error.isEmpty()?QString("Core audit activity connection changed"):error);return;}
  const auto view=result.toObject();const auto invalid=validateAuditActivity(view,profile_.genesisHash,afterHeight,64,watched);if(!invalid.isEmpty()){consume({},invalid);return;}
  auto verify=[this,owner,consume=std::move(consume)](QJsonObject value)mutable{
   verifyConfirmedSnapshotAnchor(value.value("state").toObject(),[this,owner]{return owner.matches(epoch())&&connected_&&processAlive();},[this](qint64 height,auto done){rpc("getblockhash",{height},std::move(done));},[value,consume=std::move(consume)](QString reason){consume(reason.isEmpty()?value:QJsonObject{},reason);});
  };
  if(!afterHeight||view.value("rebase").toBool()){verify(view);return;}
  // A fork can grow beyond the previous height. Compare its old ancestor;
  // comparing only the new height would incorrectly animate that fork's past.
  rpc("getblockhash",{afterHeight},[this,owner,view,afterHash,verify=std::move(verify)](QJsonValue previous,QString reason)mutable{
   auto value=view;if(!reason.isEmpty()||previous.toString()!=afterHash||!owner.matches(epoch())||!connected_||!processAlive())value.insert("rebase",true);verify(value);
  });
 });
}
void CoreRuntime::readTicketAtlas(const QString& start,const QString& end,int bins,std::function<void(QJsonObject,QString)> consume){
 if(!connected_||!processAlive()||bins<1||bins>512||start.isEmpty()!=end.isEmpty()){consume({},"Verified Core connection and bounded atlas required");return;}
 const auto owner=epoch();QJsonArray params{start.isEmpty()?QJsonValue{}:QJsonValue{start},end.isEmpty()?QJsonValue{}:QJsonValue{end},bins};
 rpc("getpgnticketatlas",params,[this,owner,start,end,bins,consume=std::move(consume)](QJsonValue value,QString error){
  if(!error.isEmpty()||!owner.matches(epoch())||!connected_||!processAlive()){consume({},error.isEmpty()?QString("Core atlas connection changed"):error);return;}
  const auto view=value.toObject();const auto invalid=validateTicketAtlas(view,profile_.genesisHash,start,end,bins);if(!invalid.isEmpty()){consume({},invalid);return;}
  verifyConfirmedSnapshotAnchor(view,[this,owner]{return owner.matches(epoch())&&connected_&&processAlive();},[this](qint64 height,auto done){rpc("getblockhash",{height},std::move(done));},[view,consume=std::move(consume)](QString error){consume(error.isEmpty()?view:QJsonObject{},error);});
 });
}
void CoreRuntime::readTicketPage(int offset,std::function<void(QJsonObject,QString)> consume){
 const auto owner=epoch();
 readWorkView("tickets",offset,100,{},[this,owner,consume=std::move(consume)](QJsonObject view,QString error){
  if(!error.isEmpty()){consume({},error);return;}
  auto state=view.value("state").toObject();state.insert("reports",view.value("reports"));state.insert("audits",view.value("audits"));
  QJsonArray ids;for(const auto& item:state.value("leases").toArray())if(item.toObject().contains("report_root"))ids.append(item.toObject().value("request"));
  if(ids.isEmpty()||!view.value("audits").toArray().isEmpty()){consume(state,{});return;}
  // Ticket pages intentionally omit audit joins. Read only their submitted IDs
  // in one bounded batch, and reject a change of tip across the two reads.
  rpc("getpgnauditstate",{0,ids.size(),QJsonValue{},ids},[this,owner,state,ids,consume=std::move(consume)](QJsonValue value,QString error)mutable{
   if(!error.isEmpty()||!owner.matches(epoch())||!connected_||!processAlive()){consume({},error.isEmpty()?QString("Core audit page connection changed"):error);return;}
   QSet<QString> expected,seen;for(const auto& id:ids)expected.insert(id.toString());
   if(!value.isArray()||value.toArray().size()!=ids.size()){consume({},"Ticket audit batch incomplete; retaining last verified page");return;}
   for(const auto& entry:value.toArray()){const auto row=entry.toObject();const auto id=row.value("request_id").toString();if(!expected.contains(id)||seen.contains(id)||!row.value("jobs").isArray()||row.value("jobs").toArray().size()>2||!row.value("audited").isBool()){consume({},"Ticket audit batch identity invalid");return;}seen.insert(id);}
   state.insert("audits",value);
   rpc("getbestblockhash",{},[this,owner,state,consume=std::move(consume)](QJsonValue hash,QString error){
    if(!error.isEmpty()||!owner.matches(epoch())||!connected_||!processAlive()||hash!=state.value("bestblockhash")){consume({},error.isEmpty()?QString("Ticket audit page changed branch; retrying"):error);return;}
    consume(state,{});
   });
  });
 });
}
void CoreRuntime::readTicketDetails(const QString& request,std::function<void(QJsonObject,QString)> consume){
 readWorkView("tickets",0,1,request,[consume=std::move(consume)](QJsonObject view,QString error){
  if(!error.isEmpty()){consume({},error);return;}auto state=view.value("state").toObject();if(state.value("leases").toArray().size()!=1){consume({},"Ticket is not on the inspected branch");return;}state.insert("reports",view.value("reports"));state.insert("audits",view.value("audits"));consume(state,{});
 });
}
void CoreRuntime::readBlockAtHeight(int height,std::function<void(QJsonObject,QString)> consume){
 if(height<0||!connected_||!processAlive()){consume({},"Valid current-chain height required");return;}const auto owner=epoch();rpc("getblockhash",{height},[this,owner,consume=std::move(consume)](QJsonValue hash,QString error){if(!error.isEmpty()||!owner.matches(epoch())){consume({},error.isEmpty()?QString("Core epoch changed"):error);return;}readBlockDetails(hash.toString(),std::move(consume));});
}
void CoreRuntime::readBlockPage(int height,std::function<void(QJsonObject,QString)> consume){
 if(!connected_||!processAlive()){consume({},"Verified Core connection required");return;}const auto owner=epoch();rpc("getpgnstate",{},[this,owner,height,consume=std::move(consume)](QJsonValue value,QString error){
  auto state=value.toObject();if(!error.isEmpty()||!owner.matches(epoch())||state.value("genesis")!=profile_.genesisHash){consume({},error.isEmpty()?QString("Block page chain identity changed"):error);return;}const auto top=state.value("height").toInt();const int start=height<0?top:std::min(height,top);state.insert("tip_height",top);state.insert("height",start);
  rpc("getblockhash",{start},[this,owner,state,consume=std::move(consume)](QJsonValue hash,QString error){if(!error.isEmpty()||!owner.matches(epoch())){consume({},error.isEmpty()?QString("Block page epoch changed"):error);return;}networkHeaders(owner,state,hash.toString(),25,{},[consume](QJsonObject page,QString error){if(error.isEmpty())page.insert("height",page.value("tip_height"));consume(page,error);});});
 });
}
void CoreRuntime::readAuditDetails(const QString& request,std::function<void(QJsonObject,QString)> consume){
 readWorkView("audits",0,1,request,[consume=std::move(consume)](QJsonObject view,QString error){
  if(!error.isEmpty()){consume({},error);return;}const auto state=view.value("state").toObject();if(view.value("reports").toArray().size()!=1){consume({},"Confirmed audit report unavailable at the inspected branch");return;}
  consume({{"genesis",state.value("genesis")},{"bestblockhash",state.value("bestblockhash")},{"height",state.value("height")},{"reports",view.value("reports")},{"audits",view.value("audits")}},{ });
 });
}
void CoreRuntime::readAuditSnapshot(int offset,std::function<void(QJsonObject,QString)> consume){
 readWorkView("audits",offset,100,{},[offset,consume=std::move(consume)](QJsonObject view,QString error){
  if(!error.isEmpty()){consume({},error);return;}const auto state=view.value("state").toObject();consume({{"genesis",state.value("genesis")},{"tip",state.value("bestblockhash")},{"height",state.value("height")},{"offset",offset},{"reports",view.value("reports")},{"audits",view.value("audits")},{"observed_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}},{});
 });
}
void CoreRuntime::readNetworkMetrics(int spanSeconds,std::function<void(QJsonObject,QString)> consume){
 if(spanSeconds<60||spanSeconds>604800||!connected_||!processAlive()){consume({},"Verified Core connection and bounded metrics history required");return;}
 const auto owner=epoch();
 rpc("getpgnnetworkmetrics",{256,spanSeconds,600},[this,owner,consume=std::move(consume)](QJsonValue value,QString error){
  const auto state=value.toObject();
  if(!error.isEmpty()||!connected_||!processAlive()||!owner.matches(epoch())){consume({},error.isEmpty()?QString("Network metrics Core epoch changed"):error);return;}
  const auto invalid=validateNetworkMetrics(state,profile_.genesisHash);if(!invalid.isEmpty()){consume({},invalid);return;}
  rpc("getblockhash",{state.value("height")},[this,owner,state,consume=std::move(consume)](QJsonValue hash,QString error){
   if(!error.isEmpty()||!connected_||!processAlive()||!owner.matches(epoch())||hash!=state.value("bestblockhash")){consume({},error.isEmpty()?QString("Network metrics branch changed"):error);return;}
   consume(state,{});
  });
 });
}
void CoreRuntime::readParticipantPage(int offset,std::function<void(QJsonObject,QString)> consume){
 if(offset<0||!connected_||!processAlive()){consume({},"Verified Core connection required");return;}
 const auto owner=epoch();
 rpc("getpgnparticipants",{offset,100},[this,owner,consume=std::move(consume)](QJsonValue value,QString error){
  const auto state=value.toObject();
  if(!error.isEmpty()||!connected_||!processAlive()||!owner.matches(epoch())){
   consume({},error.isEmpty()?QString("Participant Core epoch changed"):error);return;
  }
  const auto invalid=validateParticipantSnapshot(state,profile_.genesisHash);
  if(!invalid.isEmpty()){consume({},invalid);return;}
  consume(state,{});
 });
}
void CoreRuntime::readBlockDetails(const QString& hash,std::function<void(QJsonObject,QString)> consume){readBlockTransactions(hash,0,std::move(consume));}
void CoreRuntime::readBlockTransactions(const QString& hash,int offset,std::function<void(QJsonObject,QString)> consume){
 const auto owner=epoch();
 readExplorerBlockPage(hash,offset,[this,owner]{return owner.matches(epoch())&&connected_&&processAlive();},
  [this](const QString& method,const QJsonArray& params,TransactionReply done){rpc(method,params,std::move(done));},std::move(consume));
}
void CoreRuntime::readTransactionDetails(const QString& id,const QString& block,std::function<void(QJsonObject,QString)> consume){readTransactionPage(id,block,0,std::move(consume));}
void CoreRuntime::readTransactionPage(const QString& id,const QString& block,int offset,std::function<void(QJsonObject,QString)> consume){
 const auto owner=epoch();
 readExplorerTransactionPage(id,block,offset,[this,owner]{return owner.matches(epoch())&&connected_&&processAlive();},
  [this](const QString& method,const QJsonArray& params,TransactionReply done){rpc(method,params,std::move(done));},std::move(consume));
}
void CoreRuntime::readDifficultyHeader(const ChainObservation& anchor,const QString& hash,std::function<void(QJsonObject,QString,QDateTime)> consume){
 static const QRegularExpression pattern("^[a-fA-F0-9]{64}$");
 if(!connected_||!anchor.valid()||!anchor.owner.matches(epoch())||anchor.genesis!=profile_.genesisHash.toLower()||!pattern.match(hash).hasMatch()||!processAlive()){
  consume({},"Historical header read requires the same verified Core epoch and pinned chain",{});return;
 }
 const auto ownerEpoch=epoch();
 rpc("getblockheader",{hash.toLower(),true},[this,ownerEpoch,hash,consume=std::move(consume)](QJsonValue value,QString error){
  if(!ownerEpoch.matches(epoch())||!connected_||!processAlive()){consume({},"Core epoch changed during historical read",{});return;}
  if(!error.isEmpty()||!value.isObject()||value.toObject().value("hash").toString().compare(hash,Qt::CaseInsensitive)!=0){consume({},error.isEmpty()?QString("Core historical header identity mismatch"):error,{});return;}
  consume(value.toObject(),{},QDateTime::currentDateTimeUtc());
 });
}
void CoreRuntime::ensureWallet(const CoreEpoch& owner){
 if(!owner.matches(epoch())||!connected_||!processAlive()||walletChecking_)return;
 if(profile_.walletName.isEmpty()){walletInitialized(owner);return;}
 walletChecking_=true;
 rpc("listwallets",{},[this,owner](QJsonValue loaded,QString error){
  if(!owner.matches(epoch())||!processAlive())return;
  if(!error.isEmpty()){walletChecking_=false;report("Wallet list unavailable; Core remains intact");return;}
  for(const auto& value:loaded.toArray())if(value.toString()==profile_.walletName){verifyLoadedWallet(owner);return;}
  rpc("listwalletdir",{},[this,owner](QJsonValue value,QString error){
   if(!owner.matches(epoch())||!processAlive())return;
   if(!error.isEmpty()){walletChecking_=false;report("Wallet directory unavailable; nothing created");return;}
   bool exists=false;for(const auto& wallet:value.toObject().value("wallets").toArray())exists=exists||wallet.toObject().value("name").toString()==profile_.walletName;
   if(!exists&&!(profile_.userSelectedDirectory&&profile_.createWallet)){walletChecking_=false;report("Selected wallet is unavailable; setup or restore it explicitly");return;}
   const auto method=exists?QString("loadwallet"):QString("createwallet");
   const QJsonArray args=exists?QJsonArray{profile_.walletName,true}:QJsonArray{profile_.walletName,false,false,QJsonValue{},false,true,false};
   rpc(method,args,[this,owner](QJsonValue,QString error){
    if(!owner.matches(epoch())||!processAlive())return;
    if(!error.isEmpty()){walletChecking_=false;report("Wallet setup/open failed; existing data preserved");return;}
    verifyLoadedWallet(owner);
   });
  });
 });
}
void CoreRuntime::verifyLoadedWallet(const CoreEpoch& owner){
 rpc("getwalletinfo",{},[this,owner](QJsonValue value,QString error){
  if(!owner.matches(epoch())||!processAlive())return;
  if(!error.isEmpty()||value.toObject().value("walletname").toString()!=profile_.walletName){walletChecking_=false;report("Loaded wallet identity could not be verified");return;}
  walletInitialized(owner);
 },true);
}
void CoreRuntime::walletInitialized(const CoreEpoch& owner){
 walletChecking_=false;if(!owner.matches(epoch())||!connected_||!processAlive())return;
 if(profile_.userSelectedDirectory&&profile_.createWallet){
  QFile input(runtimeDirectory_+"/wallet-core.json");
  if(!input.open(QIODevice::ReadOnly)||input.size()>32768){report("Wallet is retained; setup confirmation could not be saved");return;}
  auto json=QJsonDocument::fromJson(input.readAll()).object();input.close();
  if(coreOwnershipBinding(CoreProfile::fromJson(json),pid_,processBirth_)!=coreOwnershipBinding(profile_,pid_,processBirth_)){report("Wallet setup configuration changed; no receipt overwritten");return;}
  json.insert("create_wallet",false);const auto bytes=QJsonDocument(json).toJson(QJsonDocument::Compact);QSaveFile output(runtimeDirectory_+"/wallet-core.json");
  if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit()){report("Wallet is retained; setup confirmation could not be committed");return;}
  profile_.createWallet=false;
 }
 if(!walletReady_){walletReady_=true;if(!walletManager_->ready())walletManager_->activate(profile_.genesisHash,profile_.chain,profile_.dataDirectory,profile_.walletName,runtimeDirectory_+"/wallet-view.json");if(onReady)onReady(pid_);}
}
void CoreRuntime::inspect(){
 if(!processAlive()||checking_||stopping_)return;checking_=true;
 const auto ownerEpoch=epoch();
 rpc("getblockchaininfo",{},[this,ownerEpoch](QJsonValue chain,QString error){
  if(!ownerEpoch.matches(epoch())||!processAlive())return;
   if(!error.isEmpty()){checking_=false;connected_=false;setLinkState(CoreLinkState::RpcUnavailable,error);if(onObservation)onObservation(std::nullopt);report("CORE READ UNAVAILABLE · retrying automatically");return;}
  const auto observedUtc=QDateTime::currentDateTimeUtc();
  rpc("getblockhash",{0},[this,chain,ownerEpoch,observedUtc](QJsonValue genesis,QString error){
   if(!ownerEpoch.matches(epoch())||!processAlive())return;
    if(!error.isEmpty()){checking_=false;connected_=false;setLinkState(CoreLinkState::RpcUnavailable,error);if(onObservation)onObservation(std::nullopt);report("CORE READ UNAVAILABLE · retrying automatically");return;}
   const auto projected=projectChainObservation(ownerEpoch,profile_,chain.toObject(),genesis.toString(),observedUtc);
    if(!projected.observation){checking_=false;connected_=false;clock_.stop();setLinkState(CoreLinkState::Rejected,projected.error);if(onObservation)onObservation(std::nullopt);
    report(projected.error+" · refusing wallet integration/RPC mutations; client left intact");return;}
   if(!connected_)connected_=true;
    if(!walletManager_->ready())walletManager_->activate(profile_.genesisHash,profile_.chain,profile_.dataDirectory,profile_.walletName,runtimeDirectory_+"/wallet-view.json");
    ensurePeerBootstrap();
   if(!walletReady_&&!walletChecking_)ensureWallet(ownerEpoch);
    rpc("getconnectioncount",{},[this,ownerEpoch,observation=*projected.observation](QJsonValue peers,QString)mutable{
      if(!ownerEpoch.matches(epoch())||!processAlive())return;
     if(peers.isDouble()&&peers.toDouble()>=0&&peers.toInteger(-1)>=0&&double(peers.toInteger())==peers.toDouble())observation.peers=peers.toInteger();
      const auto publish=[this,ownerEpoch](const ChainObservation& value){
       if(!ownerEpoch.matches(epoch())||!processAlive())return;checking_=false;setLinkState(CoreLinkState::Ready);
       if(onObservation)onObservation(value);
       report("PGN CORE · height "+QString::number(value.height)+
        (!value.synchronizing?" · SYNC STATUS UNAVAILABLE":*value.synchronizing?" · SYNCHRONIZING":value.oldTipAt(QDateTime::currentDateTimeUtc())?" · CONNECTED · CHAIN IDLE":" · CONNECTED"));
      };
      if(!peerBootstrap_){publish(observation);return;}
      const auto endpoint=peerBootstrap_->localEndpoint();
      rpc("getpeerinfo",{},[observation,endpoint,publish](QJsonValue value,QString error)mutable{
       if(!error.isEmpty()||!value.isArray()||value.toArray().size()>512){observation.synchronizing.reset();publish(observation);return;}
       publish(projectNativeSynchronization(observation,value.toArray(),endpoint));
      });
    });
  });
 });
}
void CoreRuntime::stop(){
 if(!connected_||!processAlive()||stopping_)return;stopping_=true;clock_.start();
 const auto ownerEpoch=epoch();
 rpc("stop",{},[this,ownerEpoch](QJsonValue,QString error){
  if(!ownerEpoch.matches(epoch())||!processAlive())return;
  if(!error.isEmpty()){stopping_=false;report("Graceful Core stop unavailable; client left intact");}
  else report("CORE SHUTDOWN REQUESTED · waiting for actual process exit");
 });
}
void CoreRuntime::ensurePeerBootstrap(){
 if(peerBootstrap_||profile_.chain!="test"||!profile_.peers.isEmpty()||!connected_||!processAlive())return;
 const auto folder=QCoreApplication::applicationDirPath()+"/EngineRuntime/network/";
 QFile descriptor(folder+"bootstrap.json");if(!descriptor.exists())return;
 if(!descriptor.open(QIODevice::ReadOnly)||descriptor.size()>8192){report("Integrated bootstrap descriptor cannot be read safely");return;}
 const auto value=QJsonDocument::fromJson(descriptor.readAll()).object();descriptor.close();
 const auto error=bootstrapDescriptorError(value,profile_.genesisHash);if(!error.isEmpty()){report(error);return;}
 const auto program=folder+"horizon-engine-host"
#ifdef Q_OS_WIN
  ".exe"
#endif
  ;
 if(!QFileInfo::exists(program)){report("Integrated network component is unavailable");return;}
 peerBootstrap_=std::make_unique<PeerBootstrap>(program,value,this);
 peerBootstrap_->onReady=[this](const QString& peer){
  if(!connected_||!processAlive())return;
  const auto owner=epoch();rpc("addnode",{peer,"add"},[this,owner,peer](QJsonValue,QString error){
   if(!owner.matches(epoch()))return;
   if(!error.isEmpty())report("Core bootstrap attachment: "+error);
   // Persistent addnode retry may be sleeping for60s after an older attempt.
   // Attempt this verified local relay now; retain the persistent retry too.
   rpc("addnode",{peer,"onetry"},[](QJsonValue,QString){});
  });
 };
 peerBootstrap_->onRemoved=[this](const QString& peer){if(connected_&&processAlive())rpc("addnode",{peer,"remove"},[](QJsonValue,QString){});};
 peerBootstrap_->onStatus=[this](const QString& text){report(text);};peerBootstrap_->launch();
}
}

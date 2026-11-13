#include "wallet/core_console_rpc.h"
#include "wallet/public_work_snapshot.h"
#include "wallet/audit_activity_snapshot.h"
#include "wallet/ticket_atlas_snapshot.h"
#include "wallet/confirmed_snapshot_anchor.h"
#include "wallet/explorer_projection.h"
#include <algorithm>
#include "wallet/core_runtime.h"
#include "social/social_signing_contract.h"
#include "social/social_rpc_queue.h"
#include "social/social_leaderboard.h"
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
#include <QStorageInfo>
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
#include <sys/stat.h>
#include <unistd.h>
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
bool processIdentity(qint64 pid,QString& birth,QString& image,bool* exited=nullptr){
 if(exited)*exited=false;
 if(pid<=0||pid>std::numeric_limits<int>::max())return false;
 const auto root=QString("/proc/%1/").arg(pid);QFile stat(root+"stat");if(!stat.open(QIODevice::ReadOnly))return false;const auto bytes=stat.read(8193);if(bytes.size()>8192)return false;const auto end=bytes.lastIndexOf(')');if(end<0)return false;const auto fields=bytes.sliced(end+2).split(' ');if(fields.size()<=19)return false;
 bool valid=false;const auto ticks=fields[19].toULongLong(&valid);if(!valid||!ticks)return false;
 QFile boot("/proc/sys/kernel/random/boot_id");if(!boot.open(QIODevice::ReadOnly))return false;const auto id=boot.read(128).trimmed();if(id.size()!=36)return false;birth=QString::fromLatin1(id)+":"+QString::number(ticks);
  if(fields[0]=="Z"||fields[0]=="X"){if(exited)*exited=true;return false;}
  image=QFileInfo(root+"exe").symLinkTarget();if(image.isEmpty()||!QFileInfo(image).isFile())return false;birth=QString::fromLatin1(id)+":"+QString::number(ticks);return true;
}

#endif

QByteArray nativeFileIdentity(const QString& path,bool directory){
 const QFileInfo info(path);
 if(!info.exists()||info.isSymLink()||info.isDir()!=directory)return {};
#ifdef Q_OS_WIN
 if(info.isJunction())return {};
 QString native=QDir::toNativeSeparators(info.absoluteFilePath());
 if(!native.startsWith("\\\\?\\"))native=native.startsWith("\\\\")?"\\\\?\\UNC\\"+native.mid(2):"\\\\?\\"+native;
 HANDLE handle=CreateFileW(reinterpret_cast<const wchar_t*>(native.utf16()),FILE_READ_ATTRIBUTES,
  FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,directory?FILE_FLAG_BACKUP_SEMANTICS:0,nullptr);
 if(handle==INVALID_HANDLE_VALUE)return {};
 BY_HANDLE_FILE_INFORMATION identity{};const bool okay=GetFileInformationByHandle(handle,&identity);CloseHandle(handle);
 if(!okay||(identity.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))return {};
 return QByteArray::number(identity.dwVolumeSerialNumber,16)+":"+
  QByteArray::number((quint64(identity.nFileIndexHigh)<<32)|identity.nFileIndexLow,16);
#elif defined(Q_OS_LINUX)
 struct stat value{};const auto encoded=QFile::encodeName(info.canonicalFilePath());
 if(::stat(encoded.constData(),&value)!=0)return {};
 return QByteArray::number(quint64(value.st_dev),16)+":"+QByteArray::number(quint64(value.st_ino),16);
#else
 return {};
#endif
}
QByteArray boundedProfileBytes(const QString& runtime){
 QFile file(runtime+"/wallet-core.json");
 if(!file.open(QIODevice::ReadOnly)||file.size()>32768)return {};
 return file.readAll();
}
bool defaultBlocksLayout(const CoreProfile& profile){
 // This is a bounded rejection of path overrides, not a second Core config
 // parser. Unknown/indirect layouts keep manual launch and disable auto retry.
 const auto indirect=[](QString key){
  key=key.trimmed().toLower();while(key.startsWith('-'))key.remove(0,1);
  key=key.section('.',-1);if(key.startsWith("no"))key.remove(0,2);
  return key=="blocksdir"||key=="includeconf"||key=="conf"||key=="settings";
 };
 for(const auto& name:{"pgn.conf","riecoin.conf"}){
  const auto path=profile.dataDirectory+"/"+name;QFile file(path);
  if(!file.exists())continue;
  if(nativeFileIdentity(path,false).isEmpty()||!file.open(QIODevice::ReadOnly)||file.size()>65536)return false;
  for(const auto& raw:file.readAll().split('\n')){
   const auto line=QString::fromUtf8(raw).trimmed();
   if(line.isEmpty()||line.startsWith('#'))continue;
   if(line.startsWith('[')){if(!line.endsWith(']'))return false;continue;}
   const auto equals=line.indexOf('=');if(equals<0||indirect(line.left(equals)))return false;
  }
 }
 const auto path=profile.networkDataDirectory()+"/settings.json";QFile settings(path);
 if(settings.exists()){
  if(nativeFileIdentity(path,false).isEmpty()||!settings.open(QIODevice::ReadOnly)||settings.size()>65536)return false;
  QJsonParseError error;const auto document=QJsonDocument::fromJson(settings.readAll(),&error);
  if(error.error!=QJsonParseError::NoError||!document.isObject())return false;
  const auto object=document.object();for(auto i=object.begin();i!=object.end();++i)if(indirect(i.key()))return false;
 }
 return true;
}
CoreProfilePreparation prepareRecoveryProfile(const QString& root,const QString& runtime,const QString& chain,const QByteArray& expected){
 CoreProfilePreparation result;
 const auto bytes=boundedProfileBytes(runtime);
 if(bytes.isEmpty()||QCryptographicHash::hash(bytes,QCryptographicHash::Sha256)!=expected){
  result.error="Core recovery stopped: the approved profile changed";return result;
 }
 for(const auto& name:{"wallet-pending-setup.json","wallet-core-rebind.json","wallet-launch-intent.json"})
  if(QFileInfo::exists(runtime+"/"+name)){result.error="Core recovery stopped: a pending setup, relocation or launch needs explicit resolution";return result;}
 result.profile=CoreProfile::fromJson(QJsonDocument::fromJson(bytes).object());
 if(result.profile.chain!=chain){result.error="Core recovery stopped: network selection changed";return result;}
 result.error=result.profile.validate(root);
 if(!result.error.isEmpty())return result;
 QFile executable(result.profile.executable);QCryptographicHash hash(QCryptographicHash::Sha256);
 if(!executable.open(QIODevice::ReadOnly)||!hash.addData(&executable)){result.error="Core recovery could not read the approved executable";return result;}
 result.digest=QString::fromLatin1(hash.result().toHex());return result;
}

CoreOwnerPresence coreOwnerPresence(const CoreProfile& profile,const QJsonObject& saved){
 bool valid=false;const auto pid=saved.value("pid").toString().toLongLong(&valid);
 const auto savedBirth=saved.value("birth").toString();
 if(!valid||pid<=0||pid>0xffffffffLL||savedBirth.isEmpty()||
    !verifyCoreOwnership(profile,saved,pid,savedBirth,profile.executable).isEmpty())return CoreOwnerPresence::Unknown;
#ifdef Q_OS_WIN
 HANDLE handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(pid));
 if(!handle)return GetLastError()==ERROR_INVALID_PARAMETER?CoreOwnerPresence::Absent:CoreOwnerPresence::Unknown;
 if(WaitForSingleObject(handle,0)==WAIT_OBJECT_0){CloseHandle(handle);return CoreOwnerPresence::Absent;}
 QString birth,image;const auto identified=processIdentity(handle,birth,image);CloseHandle(handle);
 if(!identified)return CoreOwnerPresence::Unknown;
#elif defined(Q_OS_LINUX)
 if(pid>std::numeric_limits<int>::max())return CoreOwnerPresence::Unknown;
 QString birth,image;
 bool exited{};
 if(!processIdentity(pid,birth,image,&exited))return exited||(::kill(pid_t(pid),0)<0&&errno==ESRCH)?CoreOwnerPresence::Absent:CoreOwnerPresence::Unknown;
#else
 return CoreOwnerPresence::Unknown;
#endif
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
 if(birth!=savedBirth)return CoreOwnerPresence::Absent;
 return verifyCoreOwnership(profile,saved,pid,birth,image).isEmpty()?CoreOwnerPresence::Live:CoreOwnerPresence::Unknown;
#endif
}
}
CoreRuntime::CoreRuntime(QString root,QObject* parent,bool testnet,QString directory):QObject(parent),root_(std::move(root)),runtimeDirectory_(directory.isEmpty()?root_+(testnet?"/data/testnet/ui-runtime":"/data/runtime"):std::move(directory)),expectedChain_(testnet?QStringLiteral("test"):QStringLiteral("main")){
 profile_.chain=expectedChain_;
 clock_.setInterval(1000);connect(&clock_,&QTimer::timeout,this,[this]{inspect();});
 recoveryTime_.start();recoveryClock_.setInterval(5000);
 connect(&recoveryClock_,&QTimer::timeout,this,[this]{recoveryTick();});
 network_.setProxy(QNetworkProxy::NoProxy);
 walletManager_=new WalletManager([this](const QString& method,const QJsonArray& params,const QString& name,WalletReply consume){
  if(!connected_||!processAlive()){consume({},"Verified Core connection required");return;}
  const auto owner=epoch();rpc(method,params,[this,owner,consume=std::move(consume)](QJsonValue value,QString error){
   if(!owner.matches(epoch())||!connected_||!processAlive()){consume({},"Core connection changed; wallet operation must be rechecked");return;}consume(value,error);
  },!name.isNull(),120000,name);
 },this);
 // Redirect to preserved files rather than allowing an unread process pipe
 // to block the wallet. They contain no candidate stream from the GPU.
 connect(&verifier_,&QFutureWatcher<CoreProfilePreparation>::finished,this,[this]{
  // STOP/maintenance may arrive while the immutable hash task is in flight.
  // Discard its result before any launch intent, directory or owner write.
  if(!recovery_.permits(preparationGeneration_)){
   launching_=false;allowNew_=false;if(ownershipLock_)ownershipLock_->unlock();
   setLinkState(CoreLinkState::Stopped,"Core startup was cancelled by an explicit stop or update");
   report("CORE STARTUP CANCELLED");return;
  }
  const auto preparation=verifier_.result();
  if(!preparation.error.isEmpty()){failStart(preparation.error);return;}
  if(preparationAutomatic_&&(!recoveryBindingValid_||!recoveryBindingMatches(readRecoveryStorage(preparation.profile,runtimeDirectory_))||!departedOwnerAbsent())){
   failStart("Core recovery stopped: profile, data folder or storage identity changed");return;
  }
  profile_=preparation.profile;if(!preparation.notice.isEmpty())report(preparation.notice);
  managedUpgradePending_=preparation.ownedUpgradePending;
  const QString digest=preparation.digest;
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
   const auto setupError=preparationAutomatic_?QString{}:activatePendingWalletSetup(runtimeDirectory_,root_,profile_);
   if(!setupError.isEmpty()){failStart(setupError);return;}
  if(preparationAutomatic_&&!QFileInfo(profile_.dataDirectory).isDir()){failStart("Core recovery will not create a missing data directory");return;}
  if(!preparationAutomatic_&&!QDir().mkpath(profile_.dataDirectory)){failStart("Private PGN wallet directory could not be created");return;}
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
  if(!recovery_.permits(preparationGeneration_)){launching_=false;if(ownershipLock_)ownershipLock_->unlock();return;}
  captureRecoveryBinding();
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
  recoveryClock_.start();clock_.start();report("CORE STARTED · checking exact chain identity");inspect();
 });
}
CoreRuntime::~CoreRuntime(){
 recovery_.suppress();recoveryClock_.stop();
 verifier_.waitForFinished();
 clock_.stop();
 connected_=false;peerBootstrap_.reset();
 // Detached Core owns its lifetime/data. Closing the GUI does not kill it.
#ifdef Q_OS_WIN
 if(processHandle_)CloseHandle(static_cast<HANDLE>(processHandle_));
#endif
}
void CoreRuntime::failStart(const QString& error){
 recovery_.attention();recoveryClock_.stop();
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
  captureRecoveryBinding();recoveryClock_.start();clock_.start();report("EXISTING OWNED CORE RECOVERED · checking exact chain identity");inspect();return true;
#elif defined(Q_OS_LINUX)
 QString birth,image;if(!processIdentity(savedPid,birth,image)){if(::kill(pid_t(savedPid),0)<0&&errno==ESRCH)return false;failStart("Saved Core process identity could not be inspected");return false;}
 if(birth!=savedBirth){report("Previous Core process exited; its number was reused");return false;}
 if(!verifyCoreOwnership(profile_,saved,savedPid,birth,image).isEmpty()){failStart("Saved Core image changed within the same process");return false;}pid_=savedPid;processBirth_=birth;launching_=false;captureRecoveryBinding();recoveryClock_.start();clock_.start();report("EXISTING OWNED CORE RECOVERED · checking exact chain identity");inspect();return true;
#else
 failStart("Native process ownership is not qualified on this platform");return false;
#endif
}
bool CoreRuntime::processAlive(){
 bool confirmedExit=false,exitKindAllowed=true;
#ifdef Q_OS_WIN
 const auto wait=processHandle_?WaitForSingleObject(static_cast<HANDLE>(processHandle_),0):WAIT_FAILED;
 if(wait==WAIT_TIMEOUT)return true;
 if(wait==WAIT_OBJECT_0){
  DWORD code{};confirmedExit=true;
  exitKindAllowed=GetExitCodeProcess(static_cast<HANDLE>(processHandle_),&code)&&(code==EXIT_SUCCESS||code==EXIT_FAILURE);
 }
 if(processHandle_){CloseHandle(static_cast<HANDLE>(processHandle_));processHandle_=nullptr;}
#elif defined(Q_OS_LINUX)
 if(pid_>0){
  QString birth,image;bool exited{};
  if(processIdentity(pid_,birth,image,&exited)&&birth==processBirth_&&QFileInfo(image).canonicalFilePath()==QFileInfo(profile_.executable).canonicalFilePath())return true;
  confirmedExit=(exited&&birth==processBirth_)||(::kill(pid_t(pid_),0)<0&&errno==ESRCH);
 }
#endif
 if(pid_){
  const auto departed=coreOwnershipBinding(profile_,pid_,processBirth_);
  readRecoveryLog(true);
  const bool known=confirmedExit&&exitKindAllowed&&faultEvidence_.knownDiskFull();
  const bool same=recoveryBindingValid_&&recoveryBindingMatches(readRecoveryStorage(profile_,runtimeDirectory_));
  ++identityGeneration_;pid_=0;processBirth_.clear();clock_.stop();connected_=false;checking_=false;stopping_=false;walletChecking_=walletReady_=false;
  peerBootstrap_.reset();walletManager_->invalidate();
  setLinkState(CoreLinkState::Stopped,"The owned Core process exited");
  if(onObservation)onObservation(std::nullopt);
  if(ownershipLock_)ownershipLock_->unlock();
  if(onStopped)onStopped();
  if(restartAfterManagedUpgrade_&&recovery_.permits(recovery_.generation())){
   restartAfterManagedUpgrade_=false;managedUpgradePending_=false;
   const auto generation=recovery_.generation();
   QTimer::singleShot(0,this,[this,generation]{if(recovery_.permits(generation))begin(true);});
  }else if(recovery_.diskExit(known,same,recoveryTime_.elapsed())){
   departedOwner_=departed;recoveryClock_.start();
   setLinkState(CoreLinkState::Stopped,"Core stopped because storage filled; saved wallet and chain are retained. Waiting for stable free space before retrying.");
   report("CORE WAITING FOR STORAGE · automatic recovery is armed");
  }else{
   recoveryClock_.stop();
   report(recovery_.state()==CoreRecoveryPolicy::State::Suppressed?
    "CORE STOPPED BY REQUEST · automatic restart is suspended":
    "CORE STOPPED · cause or ownership could not be verified for automatic recovery; wallet and chain are unchanged");
  }
 }
 return false;
}
CoreRuntime::RecoveryStorage CoreRuntime::readRecoveryStorage(const CoreProfile& profile,const QString& runtime){
 RecoveryStorage sample;
 sample.layoutKnown=defaultBlocksLayout(profile);
 if(!sample.layoutKnown){sample.reason="Configured blocks location is indirect or unverified";return sample;}
 sample.dataPath=QFileInfo(profile.dataDirectory).canonicalFilePath();
 sample.runtimePath=QFileInfo(runtime).canonicalFilePath();
 sample.blocksPath=QFileInfo(profile.networkDataDirectory()+"/blocks").canonicalFilePath();
 if(sample.dataPath.isEmpty()||sample.runtimePath.isEmpty()||sample.blocksPath.isEmpty())return sample;
 sample.dataId=nativeFileIdentity(sample.dataPath,true);sample.runtimeId=nativeFileIdentity(sample.runtimePath,true);
 sample.blocksId=nativeFileIdentity(sample.blocksPath,true);
 QStorageInfo data(sample.dataPath),owner(sample.runtimePath),blocks(sample.blocksPath);data.refresh();owner.refresh();blocks.refresh();
 sample.dataDevice=data.device();sample.runtimeDevice=owner.device();sample.blocksDevice=blocks.device();
 sample.ready=!sample.dataId.isEmpty()&&!sample.runtimeId.isEmpty()&&!sample.blocksId.isEmpty()&&data.isValid()&&data.isReady()&&!data.isReadOnly()&&
  blocks.isValid()&&blocks.isReady()&&!blocks.isReadOnly()&&blocks.bytesAvailable()>=0&&
  owner.isValid()&&owner.isReady()&&!owner.isReadOnly()&&data.bytesAvailable()>=0&&owner.bytesAvailable()>=0;
 if(sample.ready)sample.available=std::min({data.bytesAvailable(),owner.bytesAvailable(),blocks.bytesAvailable()});
 return sample;
}
void CoreRuntime::captureRecoveryBinding(){
 const auto bytes=boundedProfileBytes(runtimeDirectory_);
 recoveryProfileDigest_=bytes.isEmpty()?QByteArray{}:QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
 recoveryStorage_=readRecoveryStorage(profile_,runtimeDirectory_);
 recoveryBindingValid_=!recoveryProfileDigest_.isEmpty()&&recoveryStorage_.layoutKnown&&!recoveryStorage_.dataId.isEmpty()&&
  !recoveryStorage_.runtimeId.isEmpty()&&!recoveryStorage_.blocksId.isEmpty();
 recoveryLog_.path=profile_.networkDataDirectory()+"/debug.log";
 recoveryLog_.identity=nativeFileIdentity(recoveryLog_.path,false);
 recoveryLog_.offset=QFileInfo(recoveryLog_.path).size();
 recoveryLog_.awaitingCreation=launching_&&!QFileInfo::exists(recoveryLog_.path);
 faultEvidence_=CoreFaultEvidence{};
 if(recoveryLog_.identity.isEmpty()&&!recoveryLog_.awaitingCreation)faultEvidence_.invalidate();
}
bool CoreRuntime::recoveryBindingMatches(const RecoveryStorage& sample)const{
 const auto bytes=boundedProfileBytes(runtimeDirectory_);
 return recoveryBindingValid_&&!bytes.isEmpty()&&recoveryStorage_.sameIdentity(sample)&&
  QCryptographicHash::hash(bytes,QCryptographicHash::Sha256)==recoveryProfileDigest_;
}
void CoreRuntime::readRecoveryLog(bool final){
 if(!faultEvidence_.usable())return;
 const auto identity=nativeFileIdentity(recoveryLog_.path,false);
 if(recoveryLog_.awaitingCreation){
  if(identity.isEmpty()){if(final)faultEvidence_.invalidate();return;}
  recoveryLog_.identity=identity;recoveryLog_.offset=0;recoveryLog_.awaitingCreation=false;
 }
 QFile file(recoveryLog_.path);
 if(identity.isEmpty()||identity!=recoveryLog_.identity||!file.open(QIODevice::ReadOnly)||file.size()<recoveryLog_.offset||
    file.size()-recoveryLog_.offset>65536||!file.seek(recoveryLog_.offset)){
  faultEvidence_.invalidate();return;
 }
 const auto wanted=file.size()-recoveryLog_.offset;const auto bytes=file.read(wanted);
 if(bytes.size()!=wanted){faultEvidence_.invalidate();return;}
 recoveryLog_.offset+=bytes.size();
 faultEvidence_.feed(std::string_view(bytes.constData(),std::size_t(bytes.size())),final);
}
bool CoreRuntime::departedOwnerAbsent()const{
 bool valid{};const auto oldPid=departedOwner_.value("pid").toString().toLongLong(&valid);
 const auto oldBirth=departedOwner_.value("birth").toString();
 if(!valid||oldPid<=0||oldPid>std::numeric_limits<int>::max()||!verifyCoreOwnership(profile_,departedOwner_,oldPid,oldBirth,profile_.executable).isEmpty())return false;
 QFile file(runtimeDirectory_+"/wallet-owned.json");
 if(!file.open(QIODevice::ReadOnly)||file.size()>8192||QJsonDocument::fromJson(file.readAll()).object()!=departedOwner_)return false;
#ifdef Q_OS_WIN
 HANDLE handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(oldPid));
 if(!handle)return GetLastError()==ERROR_INVALID_PARAMETER;
 QString birth,image;const bool identified=processIdentity(handle,birth,image);
 const auto wait=WaitForSingleObject(handle,0);CloseHandle(handle);
 // A recycled PID is deliberately not an automatic-recovery witness.
 return identified&&birth==oldBirth&&wait==WAIT_OBJECT_0;
#elif defined(Q_OS_LINUX)
 QString birth,image;bool exited{};
 if(processIdentity(oldPid,birth,image,&exited))return false;
 if(exited)return birth==oldBirth;
 return ::kill(pid_t(oldPid),0)<0&&errno==ESRCH;
#else
 return false;
#endif
}
void CoreRuntime::recoveryTick(){
 const auto now=recoveryTime_.elapsed();
 if(processAlive()){
  readRecoveryLog(false);
  if(connected_&&walletReady_)recovery_.ready(now);
  return;
 }
 if(!recovery_.waiting())return;
 const auto sample=readRecoveryStorage(profile_,runtimeDirectory_);
 if(!recoveryBindingMatches(sample)||!departedOwnerAbsent()){
  recovery_.attention();recoveryClock_.stop();
  report("Core recovery stopped: approved profile, process ownership or storage identity changed; no client launched");return;
 }
 if(recovery_.probe(now,sample.ready,true,true,sample.available))begin(true,true);
}
void CoreRuntime::report(const QString& text){if(onStatus)onStatus(text);}
void CoreRuntime::setLinkState(CoreLinkState state,const QString& detail){
 const auto bounded=detail.left(480);
 if(linkState_==state&&linkDetail_==bounded)return;
 linkState_=state;linkDetail_=bounded;
 if(onLinkState)onLinkState(state,bounded);
}
void CoreRuntime::launch(){
 if(launching_||verifier_.isRunning()){report("The owned Core preparation is still in progress");return;}
 recovery_.explicitStart();begin(true);
}
void CoreRuntime::reattach(){
 if(QFileInfo::exists(runtimeDirectory_+"/wallet-owned.json")){recovery_.explicitStart();begin(false);}
}
void CoreRuntime::begin(bool allowNew,bool automatic){
 if(orphanedStart_){report("An unbound detached Core may still exist; no second launch permitted");return;}
 if(launching_||verifier_.isRunning()||processAlive()){report("The owned Core client is already starting/running");return;}
 ++identityGeneration_;
 setLinkState(CoreLinkState::Starting,"Checking the owned Core deployment");
 QFile receipt(runtimeDirectory_+"/wallet-core.json");
 if(!receipt.open(QIODevice::ReadOnly)||receipt.size()>32768){failStart("No verified PGN Core build/deployment receipt; no wallet launched");return;}
 const auto configuredProfile=CoreProfile::fromJson(QJsonDocument::fromJson(receipt.readAll()).object());
 receipt.close();
 if(configuredProfile.chain!=expectedChain_){
  failStart(QString("This wallet is set to %1, but the saved Core profile is for %2. Open wallet setup to use a matching profile.")
      .arg(expectedChain_=="test"?QString("testnet"):QString("mainnet"),configuredProfile.chain=="test"?QString("testnet"):QString("mainnet")));return;
 }
  if(automatic){
   const auto current=boundedProfileBytes(runtimeDirectory_);
   if(!recoveryBindingValid_||current.isEmpty()||QCryptographicHash::hash(current,QCryptographicHash::Sha256)!=recoveryProfileDigest_||
      !recoveryBindingMatches(readRecoveryStorage(configuredProfile,runtimeDirectory_))||!departedOwnerAbsent()){
    recovery_.attention();recoveryClock_.stop();report("Core recovery stopped: the approved owner, profile or storage identity changed");return;
   }
  }
  profile_=configuredProfile;
 if(!QDir().mkpath(runtimeDirectory_)){failStart("Wallet ownership directory unavailable");return;}
 ownershipLock_=std::make_unique<QLockFile>(runtimeDirectory_+"/wallet-owned.lock");
 if(!ownershipLock_->tryLock(0)){failStart("Another PGN GUI owns wallet lifecycle; no duplicate client launched");return;}
 allowNew_=allowNew;
  preparationGeneration_=recovery_.generation();preparationAutomatic_=automatic;automaticRun_=automatic;
  launching_=true;report("Checking pinned Core executable once, outside the GUI/mining loop");
 const auto root=managedCoreBundleRoot(root_,QCoreApplication::applicationDirPath()),runtime=runtimeDirectory_,chain=expectedChain_;
  const auto pinned=recoveryProfileDigest_;const auto fixture=preparationForTest_;
  verifier_.setFuture(QtConcurrent::run([root,runtime,chain,allowNew,automatic,pinned,fixture]{
   if(fixture)return fixture();
   return automatic?prepareRecoveryProfile(root,runtime,chain,pinned):
    prepareManagedCoreProfile(root,runtime,chain,allowNew,coreOwnerPresence);
  }));
}
void CoreRuntime::rpc(const QString& method,const QJsonArray& params,std::function<void(QJsonValue,QString)> consume,bool wallet,int timeoutMs,const QString& walletName){
 rpcWire(method,QJsonDocument(params).toJson(QJsonDocument::Compact),[consume=std::move(consume)](CoreRpcResult result,QByteArray){consume(result.value,result.error);},wallet,timeoutMs,walletName);
}
void CoreRuntime::consoleRequest(const QString& method,const QByteArray& params,bool displayedWallet,std::function<void(QByteArray,QString)> consume){
  const auto validation=validateConsoleCommand(method,params);if(!validation.isEmpty()){consume({},validation);return;}
  if(method=="stop")suspendRecoveryForMaintenance();
 if(stopping_||!connected_||!processAlive()||(displayedWallet&&!displayWalletReady())){consume({},"Verified Core connection required");return;}
 const auto owner=epoch();const auto display=walletManager_->displayGeneration();const auto name=walletManager_->displayWallet();
 if(displayedWallet&&!walletManager_->holdDisplayOperation()){consume({},"Wait for the current wallet operation to finish");return;}
 rpcWire(method,params,[this,owner,display,displayedWallet,consume=std::move(consume)](CoreRpcResult result,QByteArray raw){
  if(displayedWallet)walletManager_->finishDisplayOperation(display);
  if(stopping_||!owner.matches(epoch())||!connected_||!processAlive()||(displayedWallet&&display!=walletManager_->displayGeneration())){consume({},"Core or displayed wallet changed; check the command outcome before retrying");return;}
  consume(raw,result.error);
 },displayedWallet,30000,name.isNull()?QString(""):name);
}
void CoreRuntime::rpcWire(const QString& method,const QByteArray& params,std::function<void(CoreRpcResult,QByteArray)> consume,bool wallet,int timeoutMs,const QString& walletName){
 QFile cookie(profile_.networkDataDirectory()+"/.cookie");
 if(!cookie.open(QIODevice::ReadOnly)||cookie.size()>4096){consume({{},"Core RPC cookie is not ready"},{});return;}
 const QString endpoint=wallet?walletRpcPath(walletName.isNull()?profile_.walletName:walletName):QString("/");
 QNetworkRequest request(QUrl::fromEncoded(("http://127.0.0.1:"+QString::number(profile_.rpcPort)+endpoint).toUtf8()));
 request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
 request.setRawHeader("Authorization","Basic "+cookie.readAll().trimmed().toBase64());
 request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
 request.setTransferTimeout(std::clamp(timeoutMs,1000,120000));
 const QString requestId=QUuid::createUuid().toString(QUuid::WithoutBraces);
 auto* reply=network_.post(request,consoleRpcEnvelope(requestId,method,params));
 readBoundedCoreRpcReply(reply,this,coreRpcResponseLimit(method),[reply,requestId,method,consume=std::move(consume)](QByteArray bytes,bool oversized){
  const auto error=reply->error();const auto http=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const auto detail=reply->errorString().left(160);reply->deleteLater();
  if(oversized){consume({{},QString("Core RPC response exceeds %1 byte bound").arg(coreRpcResponseLimit(method))},{});return;}
  const auto decoded=decodeCoreRpcResponse(bytes,requestId,method);
  if(error!=QNetworkReply::NoError&&!verifiedRpcHttpError(decoded,http)){consume({{},QString("Core RPC read failed (%1): %2").arg(int(error)).arg(detail)},{});return;}
  consume(decoded,decoded.error.isEmpty()?bytes:QByteArray{});
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
void CoreRuntime::readSocialIdentity(std::function<void(QJsonObject,QString)> consume){
 const auto owner=epoch();const auto lifecycle=recovery_.generation();const auto binding=walletBinding();const auto genesis=profile_.genesisHash;
 const auto current=[this,owner,lifecycle,binding,genesis]{return owner.matches(epoch())&&lifecycle==recovery_.generation()&&binding==walletBinding()&&genesis==profile_.genesisHash&&connected_&&walletReady_&&!profile_.walletName.isEmpty()&&!stopping_&&!managedUpgradePending_&&recovery_.state()!=CoreRecoveryPolicy::State::Suppressed&&processAlive();};
 if(!current()){consume({},"Verified initialized wallet required for Social identity");return;}
 if(!socialRpcQueue_)socialRpcQueue_=new social::RpcQueue(this);
 const auto queued=socialRpcQueue_->enqueue("identity",[this,current,binding,genesis,consume](social::RpcQueue::Done done){
  const auto finished=std::make_shared<bool>(false);
  const auto finish=[consume,done,finished](QJsonObject value,QString error){if(*finished)return;*finished=true;consume(value,error);done();};
  if(!current()){finish({},"Social wallet context changed");return;}
  const auto inspect=[this,current,binding,genesis,finish](const QString& address){
   if(!current()||address.isEmpty()||address.size()>100){finish({},"Social identity address unavailable");return;}
   rpc("getaddressinfo",{address},[current,binding,genesis,address,finish](QJsonValue value,QString error){
    if(!current()){finish({},"Social wallet context changed");return;}
    const auto info=value.toObject();const auto script=info.value("scriptPubKey").toString();
    if(!error.isEmpty()||!info.value("ismine").toBool()||!QRegularExpression("^0014[0-9a-f]{40}$").match(script).hasMatch()){finish({},"Social identity is not owned by this verified wallet");return;}
    finish({{"genesis",genesis},{"address",address},{"script",script},{"binding",binding}},{});
   },true);
  };
  // A dedicated service-wallet label makes Social independent of the miner,
  // remote workers and their private keys. Lost creation replies are recovered
  // from the wallet label before another address can be created.
  rpcWire("getaddressesbylabel",QJsonDocument(QJsonArray{"Horizon Social"}).toJson(QJsonDocument::Compact),[this,current,inspect,finish](CoreRpcResult result,QByteArray){
   if(!current()){finish({},"Social wallet context changed");return;}
   if(!result.error.isEmpty()){
    if(!result.applicationError||result.applicationCode!=-11){finish({},result.error);return;}
    rpc("getnewaddress",{"Horizon Social","bech32"},[current,inspect,finish](QJsonValue value,QString error){if(!current()){finish({},"Social wallet changed during identity creation; retry reads the retained address");return;}if(!error.isEmpty()||!value.isString()){finish({},"Social identity creation could not be confirmed; retry preserves the wallet");return;}inspect(value.toString());},true);
    return;
   }
   if(!result.value.isObject()||result.value.toObject().isEmpty()){finish({},"Social wallet label has no usable address");return;}
   const auto addresses=result.value.toObject();inspect(addresses.constBegin().key());
  },true,3000,{});
 });
 if(!queued)consume({},"Social identity queue is busy");
}
void CoreRuntime::signSocialEnvelope(const QString& address,const QString& genesis,const QString& signingScript,const QByteArray& canonicalMessage,std::function<void(QJsonValue,QString)> consume){
 const social::SigningContext context{genesis,signingScript,address};
 const auto invalid=social::signingRequestError(context,canonicalMessage);
 const auto usable=[this]{return connected_&&walletReady_&&!stopping_&&!managedUpgradePending_&&recovery_.state()!=CoreRecoveryPolicy::State::Suppressed&&processAlive();};
 if(!invalid.isEmpty()||genesis!=profile_.genesisHash||!usable()){consume({},invalid.isEmpty()?QString("Verified native Social wallet context required"):invalid);return;}
 const auto owner=epoch();const auto lifecycle=recovery_.generation();const auto binding=walletBinding();
 const auto current=[this,owner,lifecycle,binding,genesis]{return owner.matches(epoch())&&lifecycle==recovery_.generation()&&binding==walletBinding()&&genesis==profile_.genesisHash&&connected_&&walletReady_&&!stopping_&&!managedUpgradePending_&&recovery_.state()!=CoreRecoveryPolicy::State::Suppressed&&processAlive();};
 if(!socialRpcQueue_)socialRpcQueue_=new social::RpcQueue(this);
 const auto queued=socialRpcQueue_->enqueue(signingScript,[this,address,signingScript,canonicalMessage,context,current,consume](social::RpcQueue::Done done){
  const auto completed=std::make_shared<bool>(false);
  const auto finish=[consume,done,completed](QJsonValue value,QString reason){if(*completed)return;*completed=true;consume(value,reason);done();};
  if(!current()||!social::signingRequestError(context,canonicalMessage).isEmpty()){finish({},"Social context changed while queued");return;}
  rpc("getaddressinfo",{address},[this,address,signingScript,canonicalMessage,context,current,finish](QJsonValue value,QString error){
   if(!current()){finish({},"Social wallet epoch or lifecycle changed; signature refused");return;}
   const auto identity=value.toObject();
   if(!error.isEmpty()||!identity.value("ismine").toBool()||identity.value("scriptPubKey").toString()!=signingScript){finish({},"Social signing address is not owned by the verified service wallet");return;}
   rpc("signmessage",{address,QString::fromUtf8(canonicalMessage)},[context,canonicalMessage,current,finish](QJsonValue signature,QString failure){
    if(!current()){finish({},"Social wallet epoch or lifecycle changed; signature discarded");return;}
    if(!social::signingRequestError(context,canonicalMessage).isEmpty()){finish({},"Social signing context expired before completion");return;}
    if(!failure.isEmpty()||!signature.isString()||signature.toString().isEmpty()||signature.toString().size()>512){finish({},"Native Social BIP322 signature unavailable");return;}
    finish(signature,{});
   },true);
  },true);
 });
 if(!queued)consume({},"Social RPC queue full; mining/wallet RPCs are not throttled");
}
void CoreRuntime::verifySocialEnvelope(const QString& genesis,const QString& signingScript,const QByteArray& canonicalMessage,const QString& signature,std::function<void(bool,QString)> consume){
 // Bound before JSON parsing, descriptor derivation or native signature work.
 if(canonicalMessage.size()>24576||canonicalMessage.size()<64||signature.size()>512||signature.isEmpty()){consume(false,"Bounded Social proof required");return;}
 const int separator=canonicalMessage.indexOf('\n');
 const auto object=QJsonDocument::fromJson(canonicalMessage.mid(separator+1)).object();
 const auto claimedAddress=object.value("signing_address").toString();
 const social::SigningContext context{genesis,signingScript,claimedAddress};
 const auto invalid=social::signingRequestError(context,canonicalMessage,QDateTime::currentMSecsSinceEpoch(),social::EnvelopeUse::StoredVerification);
 const auto raw=QByteArray::fromBase64(signature.toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
 if(!invalid.isEmpty()||raw.toBase64()!=signature.toLatin1()||raw.size()<100||raw.size()>140||raw.at(0)!=2){consume(false,invalid.isEmpty()?QString("Native simple P2WPKH proof required"):invalid);return;}
 const auto usable=[this]{return connected_&&!stopping_&&!managedUpgradePending_&&recovery_.state()!=CoreRecoveryPolicy::State::Suppressed&&processAlive();};
 if(genesis!=profile_.genesisHash||!usable()){consume(false,"Verified native Social network context required");return;}
 const auto owner=epoch();const auto lifecycle=recovery_.generation();const auto binding=walletBinding();
 const auto current=[this,owner,lifecycle,binding,genesis]{return owner.matches(epoch())&&lifecycle==recovery_.generation()&&binding==walletBinding()&&genesis==profile_.genesisHash&&connected_&&!stopping_&&!managedUpgradePending_&&recovery_.state()!=CoreRecoveryPolicy::State::Suppressed&&processAlive();};
 if(!socialRpcQueue_)socialRpcQueue_=new social::RpcQueue(this);
 const auto queued=socialRpcQueue_->enqueue(signingScript,[this,context,canonicalMessage,signature,current,consume](social::RpcQueue::Done done){
  const auto completed=std::make_shared<bool>(false);
  const auto finish=[consume,done,completed](bool valid,QString reason){if(*completed)return;*completed=true;consume(valid,reason);done();};
  const auto canonicalStillValid=[context,canonicalMessage]{return social::signingRequestError(context,canonicalMessage,QDateTime::currentMSecsSinceEpoch(),social::EnvelopeUse::StoredVerification).isEmpty();};
  if(!current()||!canonicalStillValid()){finish(false,"Social verification context changed while queued");return;}
  // Derive the chain's native address from the public script, never a username
  // or a caller-selected network prefix. These are public read-only RPCs.
  rpc("getdescriptorinfo",{"raw("+context.script+")"},[this,context,canonicalMessage,signature,current,canonicalStillValid,finish](QJsonValue value,QString error){
   if(!current()){finish(false,"Social verification epoch changed");return;}
   const auto descriptor=value.toObject().value("descriptor").toString();
   if(!error.isEmpty()||descriptor.isEmpty()||descriptor.size()>512){finish(false,"Native public script descriptor unavailable");return;}
   rpc("deriveaddresses",{descriptor},[this,context,canonicalMessage,signature,current,canonicalStillValid,finish](QJsonValue result,QString reason){
    if(!current()){finish(false,"Social verification epoch changed");return;}
    const auto addresses=result.toArray();
    if(!reason.isEmpty()||addresses.size()!=1||!addresses[0].isString()||addresses[0].toString()!=context.address){finish(false,"Social address prefix or signing script mismatch");return;}
    rpc("verifymessage",{context.address,signature,QString::fromUtf8(canonicalMessage)},[current,canonicalStillValid,finish](QJsonValue verified,QString failure){
     if(!current()||!canonicalStillValid()){finish(false,"Social proof context changed before completion");return;}
     if(!failure.isEmpty()||!verified.isBool()||!verified.toBool()){finish(false,"Native Social BIP322 proof refused");return;}
     finish(true,{});
    });
   });
  });
 });
 if(!queued)consume(false,"Social RPC queue full; mining/wallet RPCs are not throttled");
}

void CoreRuntime::socialRequest(const QString& method,const QJsonArray& params,std::function<void(QJsonValue,QString)> consume){
 bool valid=false;
 if(method=="submitsocialevent")valid=params.size()==2&&params[0].isString()&&params[1].isString()&&params[0].toString().toUtf8().size()<=24576&&params[0].toString().size()>=64&&params[1].toString().size()<=512&&!params[1].toString().isEmpty();
 else if(method=="getsocialevents"&&params.size()==3){const auto generation=params[0].toString();const auto cursor=params[1].toInteger(-1),limit=params[2].toInteger(-1);valid=params[0].isString()&&(generation.isEmpty()||social::hexField(generation,64))&&params[1].isDouble()&&cursor>=0&&params[1].toDouble()==double(cursor)&&params[2].isDouble()&&limit>=1&&limit<=50&&params[2].toDouble()==double(limit);}
 else if(method=="getsocialpeers")valid=params.isEmpty();
 else if(method=="getpgnleaderboard")valid=social::validLeaderboardRpcArguments(params);
 if(!valid||!connected_||stopping_||managedUpgradePending_||recovery_.state()==CoreRecoveryPolicy::State::Suppressed||!processAlive()){consume({},"Verified Social connection and bounded request required");return;}
 const auto owner=epoch();const auto lifecycle=recovery_.generation();const auto binding=walletBinding();const auto genesis=profile_.genesisHash;
 const auto current=[this,owner,lifecycle,binding,genesis]{return owner.matches(epoch())&&lifecycle==recovery_.generation()&&binding==walletBinding()&&genesis==profile_.genesisHash&&connected_&&!stopping_&&!managedUpgradePending_&&recovery_.state()!=CoreRecoveryPolicy::State::Suppressed&&processAlive();};
 if(!socialRpcQueue_)socialRpcQueue_=new social::RpcQueue(this);
 const auto queued=socialRpcQueue_->enqueue("rpc/"+method,[this,method,params,current,genesis,consume](social::RpcQueue::Done done){
  if(!current()){consume({},"Social connection changed while queued");done();return;}
  rpc(method,params,[current,genesis,consume,done](QJsonValue value,QString error){
   if(!current())consume({},"Social connection changed; response discarded");
   else if(!error.isEmpty())consume({},error);
   else if(!value.isObject()||value.toObject().value("genesis").toString()!=genesis)consume({},"Social response belongs to another network");
   else consume(value,{});done();
  });
 });
 if(!queued)consume({},"Social is busy; mining and wallet operations remain available");
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
   if(!exists&&(automaticRun_||!(profile_.userSelectedDirectory&&profile_.createWallet))){walletChecking_=false;report("Selected wallet is unavailable; setup or restore it explicitly");return;}
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
 if(profile_.userSelectedDirectory&&profile_.createWallet&&!automaticRun_){
  QFile input(runtimeDirectory_+"/wallet-core.json");
  if(!input.open(QIODevice::ReadOnly)||input.size()>32768){report("Wallet is retained; setup confirmation could not be saved");return;}
  auto json=QJsonDocument::fromJson(input.readAll()).object();input.close();
  if(coreOwnershipBinding(CoreProfile::fromJson(json),pid_,processBirth_)!=coreOwnershipBinding(profile_,pid_,processBirth_)){report("Wallet setup configuration changed; no receipt overwritten");return;}
  json.insert("create_wallet",false);const auto bytes=QJsonDocument(json).toJson(QJsonDocument::Compact);QSaveFile output(runtimeDirectory_+"/wallet-core.json");
  if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit()){report("Wallet is retained; setup confirmation could not be committed");return;}
  profile_.createWallet=false;
 }
 if(!walletReady_){captureRecoveryBinding();recoveryClock_.start();walletReady_=true;if(!walletManager_->ready())walletManager_->activate(profile_.genesisHash,profile_.chain,profile_.dataDirectory,profile_.walletName,runtimeDirectory_+"/wallet-view.json");if(onReady)onReady(pid_);}
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
    if(!projected.observation){checking_=false;connected_=false;clock_.stop();recovery_.attention();recoveryClock_.stop();setLinkState(CoreLinkState::Rejected,projected.error);if(onObservation)onObservation(std::nullopt);
    report(projected.error+" · refusing wallet integration/RPC mutations; client left intact");return;}
   if(!connected_){connected_=true;if(!recoveryBindingValid_&&!automaticRun_)captureRecoveryBinding();}
   if(managedUpgradePending_){
    // Adopt a manually rebuilt portable Core through the same verified RPC
    // shutdown path. No wallet/worker readiness is published for the old image.
    checking_=false;restartAfterManagedUpgrade_=true;recoveryClock_.stop();stopOwned();return;
   }
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
void CoreRuntime::suspendRecoveryForMaintenance(){
 recovery_.suppress();recoveryClock_.stop();restartAfterManagedUpgrade_=false;managedUpgradePending_=false;
}
void CoreRuntime::stop(){
 suspendRecoveryForMaintenance();stopOwned();
}
void CoreRuntime::stopOwned(){
 if(!connected_||!processAlive()||stopping_)return;stopping_=true;clock_.start();
 const auto ownerEpoch=epoch();
 rpc("stop",{},[this,ownerEpoch](QJsonValue,QString error){
  if(!ownerEpoch.matches(epoch())||!processAlive())return;
  if(!error.isEmpty()){
   stopping_=false;managedUpgradePending_=false;restartAfterManagedUpgrade_=false;
   report("Graceful Core stop unavailable; client left intact");
  }
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

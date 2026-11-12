// Public inert fixtures only. No Core, wallet keys, GPU, or disk exhaustion.
#include "wallet/core_runtime.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <iostream>
#include <stdexcept>
namespace {
unsigned assertions{};
void need(bool value,const char* message){++assertions;if(!value)throw std::runtime_error(message);}
void write(const QString& path,const QByteArray& bytes,bool append=false){
 QDir().mkpath(QFileInfo(path).absolutePath());QFile file(path);
 need(file.open(append?QIODevice::WriteOnly|QIODevice::Append:QIODevice::WriteOnly|QIODevice::Truncate)&&file.write(bytes)==bytes.size(),"fixture write failed");
}
QByteArray read(const QString& path){QFile file(path);need(file.open(QIODevice::ReadOnly),"fixture read failed");return file.readAll();}
void settle(int milliseconds){QEventLoop loop;QTimer::singleShot(milliseconds,&loop,&QEventLoop::quit);loop.exec();}
using Policy=pgn::wallet::CoreRecoveryPolicy;
void policyCases(){
 static_assert(Policy::NativeFloor==50*1024*1024);
 static_assert(Policy::NativePeriodicFloor==100*1024*1024);
 static_assert(Policy::ResumeWater>Policy::LowWater);
 Policy p;const auto generation=p.explicitStart();
 need(p.diskExit(true,true,0),"known owned disk failure did not arm");
 need(!p.probe(5000,true,true,true,Policy::ResumeWater),"one high sample started Core");
 need(!p.probe(5000,true,true,true,Policy::ResumeWater),"duplicate sample counted");
 need(!p.probe(10000,false,true,true,Policy::ResumeWater),"unknown probe started Core");
 need(!p.probe(15000,true,true,true,-1),"unknown byte count started Core");
 need(!p.probe(20000,true,true,true,Policy::ResumeWater),"lost sample failed to reset high streak");
 need(!p.probe(25000,true,true,true,Policy::LowWater+1),"middle band started Core");
 need(!p.probe(30000,true,true,false,Policy::ResumeWater),"live/unknown owner started Core");
 need(!p.probe(35000,true,true,true,Policy::ResumeWater),"first stable sample started Core");
 need(!p.probe(40000,true,true,true,Policy::ResumeWater),"second stable sample started Core");
 need(p.probe(45000,true,true,true,Policy::ResumeWater)&&p.starts()==1,"third stable high sample did not authorize exactly one attempt");
 need(!p.probe(50000,true,true,true,Policy::ResumeWater),"launching state issued another attempt");
 p.suppress();need(!p.permits(generation)&&!p.diskExit(true,true,50000),"STOP did not revoke generation");
 need(!p.probe(60000,true,true,true,Policy::ResumeWater),"STOP was automatically reversed");
 for(const bool disk:{false,true}){
  Policy rejected;rejected.explicitStart();
  need(!rejected.diskExit(disk,false,0),"changed context armed recovery");
 }
 Policy unknown;unknown.explicitStart();need(!unknown.diskExit(false,true,0),"unknown crash armed recovery");
 Policy mount;mount.explicitStart();mount.diskExit(true,true,0);
 need(!mount.probe(5000,true,false,true,Policy::ResumeWater)&&mount.state()==Policy::State::Attention,"changed mount was not rejected");
 Policy budget;budget.explicitStart();qint64 now=0;
 for(unsigned attempt=0;attempt<Policy::MaximumStarts;++attempt){
  need(budget.diskExit(true,true,now),"known subsequent disk failure did not arm within budget");
  bool started=false;
  for(int n=0;n<15&&!started;++n){now+=5000;started=budget.probe(now,true,true,true,Policy::ResumeWater);}
  need(started&&budget.starts()==attempt+1,"bounded backoff did not make one attempt");
  budget.ready(now);now+=1000;
 }
 need(!budget.diskExit(true,true,now)&&budget.state()==Policy::State::Attention,"start budget did not stop a disk-full flap");
 Policy stable;stable.explicitStart();stable.diskExit(true,true,0);
 for(int n=1;n<=3;++n)stable.probe(n*5000,true,true,true,Policy::ResumeWater);
 stable.ready(20000);stable.ready(20000+Policy::StableReadyResetMs);
 need(stable.starts()==0,"stable healthy Core did not reset incident budget");
}
void evidenceCases(){
 using Evidence=pgn::wallet::CoreFaultEvidence;
 const std::string fatal="2026-11-11T20:25:14Z [error] Shutting down due to lack of disk space!\n";
 const std::string done="2026-11-11T20:25:16Z Shutdown done\n";
 Evidence split;split.feed(fatal.substr(0,31));split.feed(fatal.substr(31));
 need(!split.knownDiskFull(),"disk marker without clean shutdown accepted");split.feed(done,true);
 need(split.knownDiskFull(),"exact current Core disk/full shutdown witness rejected");
 Evidence quoted;quoted.feed("peer said: "+fatal+done,true);need(!quoted.knownDiskFull(),"quoted external log text became a cause");
 Evidence crashed;crashed.feed(fatal+"2026-11-11T20:25:15Z [error] A fatal internal error occurred, see debug.log for details: Corrupt database\n"+done,true);
 need(!crashed.knownDiskFull(),"structural fatal was hidden by an earlier disk marker");
 Evidence invalid;invalid.feed(fatal+done);invalid.invalidate();need(!invalid.knownDiskFull(),"rotated/truncated evidence remained usable");
 Evidence wrongOrder;wrongOrder.feed(done+fatal,true);need(!wrongOrder.knownDiskFull(),"old shutdown text completed a later cause");
 Evidence nativeFatal;nativeFatal.feed("[error] A fatal internal error occurred, see debug.log for details: Disk space is too low!\n"+done,true);
 need(nativeFatal.knownDiskFull(),"native validation DiskFull fatal witness rejected");
}
}

namespace pgn::wallet {
struct CoreRuntimeRecoveryTest {
 struct Fixture {
  QTemporaryDir temporary;QString root,runtime,data,debug;CoreProfile profile;QByteArray receipt;
  Fixture(){
   need(temporary.isValid(),"temporary fixture unavailable");root=temporary.path();runtime=root+"/data/testnet/ui-runtime";data=root+"/data/testnet/QA";
   QDir().mkpath(runtime);QDir().mkpath(data+"/testnet/blocks");debug=data+"/testnet/debug.log";write(debug,{});
   QFile executable(QCoreApplication::applicationFilePath());need(executable.open(QIODevice::ReadOnly),"cannot read inert test executable");
   QCryptographicHash hash(QCryptographicHash::Sha256);need(hash.addData(&executable),"cannot hash inert executable");
   const QJsonObject object{{"schema","pgn-core-deployment-2"},{"product","PGN Core"},{"chain","test"},{"frontend","daemon"},{"wallet","QA"},
    {"executable",QCoreApplication::applicationFilePath()},{"executable_sha256",QString::fromLatin1(hash.result().toHex())},
    {"genesis_hash",QString(64,'b')},{"data_directory",data},{"rpc_port",30101},{"peer_port",30102}};
   profile=CoreProfile::fromJson(object);need(profile.validate(root).isEmpty(),"public fixture profile invalid");
   receipt=QJsonDocument(object).toJson(QJsonDocument::Compact);write(runtime+"/wallet-core.json",receipt);
   write(data+"/public-chain-marker","PUBLIC INERT CHAIN MARKER");
  }
 };
 static void logAndStorage(){
  Fixture f;CoreRuntime core(f.root,nullptr,true,f.runtime);core.profile_=f.profile;
  core.captureRecoveryBinding();need(core.recoveryBindingValid_,"standard local blocks/data layout not bound");
  auto sample=CoreRuntime::readRecoveryStorage(f.profile,f.runtime);
  need(sample.ready&&sample.layoutKnown&&core.recoveryBindingMatches(sample),"fresh local volume sample invalid");
  auto changed=sample;changed.blocksDevice+="other";need(!core.recoveryBindingMatches(changed),"wrong blocks volume accepted");
  changed=sample;changed.dataId+="replaced";need(!core.recoveryBindingMatches(changed),"changed data mount accepted");
  write(f.debug,"2026-11-11T20:25:14Z [error] Shutting down due to lack of disk space!\n2026-11-11T20:25:16Z Shutdown done\n",true);
  core.readRecoveryLog(true);need(core.faultEvidence_.knownDiskFull(),"fresh owned file witness not consumed");
  core.captureRecoveryBinding();core.readRecoveryLog(true);need(!core.faultEvidence_.knownDiskFull(),"old log marker reused after reattach baseline");
  write(f.debug,{});core.readRecoveryLog(true);need(!core.faultEvidence_.usable(),"truncated owned log not invalidated");
  core.captureRecoveryBinding();const auto replacement=f.debug+".new";write(replacement,"different file identity\n");
  need(QFile::rename(f.debug,f.debug+".old")&&QFile::rename(replacement,f.debug),"private log rotation failed");
  core.readRecoveryLog(true);need(!core.faultEvidence_.usable(),"rotated owned log not invalidated");
  write(f.data+"/pgn.conf","blocksdir=D:/unverified-custom-blocks\n");
  need(!CoreRuntime::readRecoveryStorage(f.profile,f.runtime).layoutKnown,"custom blocks volume was guessed");
  need(QFile::remove(f.data+"/pgn.conf"),"cannot remove private config fixture");
  write(f.data+"/pgn.conf","includeconf=unexamined.conf\n");
  need(!CoreRuntime::readRecoveryStorage(f.profile,f.runtime).layoutKnown,"indirect config was guessed");
  need(QFile::remove(f.data+"/pgn.conf"),"cannot remove private config fixture");
  write(f.data+"/testnet/settings.json","{\"blocksdir\":\"D:/unverified\"}");
  need(!CoreRuntime::readRecoveryStorage(f.profile,f.runtime).layoutKnown,"settings override was ignored");
  need(read(f.data+"/public-chain-marker")=="PUBLIC INERT CHAIN MARKER","recovery probe changed synthetic chain data");
 }
 static void firstOwnedLog(){
  Fixture f;CoreRuntime core(f.root,nullptr,true,f.runtime);core.profile_=f.profile;
  need(QFile::remove(f.debug),"cannot remove private log fixture");
  core.launching_=true;core.captureRecoveryBinding();
  write(f.debug,"2026-11-11T20:25:14Z [error] Shutting down due to lack of disk space!\n2026-11-11T20:25:16Z Shutdown done\n");
  core.readRecoveryLog(true);need(core.faultEvidence_.knownDiskFull(),"first owned run log was not bound");
  core.launching_=false;need(QFile::remove(f.debug),"cannot reset private log fixture");core.captureRecoveryBinding();
  write(f.debug,"2026-11-11T20:25:14Z [error] Shutting down due to lack of disk space!\n2026-11-11T20:25:16Z Shutdown done\n");
  core.readRecoveryLog(true);need(!core.faultEvidence_.knownDiskFull(),"unowned missing log was adopted");
 }
 static void cancelledHash(bool maintenance,bool automatic){
  Fixture f;CoreRuntime core(f.root,nullptr,true,f.runtime);QSemaphore entered,release;std::atomic<unsigned> calls{};
  CoreProfilePreparation prepared;prepared.profile=f.profile;prepared.digest=f.profile.executableSha256;
  core.preparationForTest_=[&]{++calls;entered.release();release.acquire();return prepared;};
  QByteArray beforeOwner;
  if(automatic){
   core.profile_=f.profile;core.captureRecoveryBinding();
   core.departedOwner_=coreOwnershipBinding(f.profile,999999999,"PUBLIC_ABSENT_OWNER");
   beforeOwner=QJsonDocument(core.departedOwner_).toJson(QJsonDocument::Compact);write(f.runtime+"/wallet-owned.json",beforeOwner);
   core.recovery_.explicitStart();core.recovery_.diskExit(true,true,0);
   for(int n=1;n<=3;++n)core.recovery_.probe(n*5000,true,true,true,CoreRecoveryPolicy::ResumeWater);
   core.begin(true,true);
  }else core.launch();
  need(entered.tryAcquire(1,3000),"private hash task did not enter");
  if(maintenance)core.suspendRecoveryForMaintenance();else core.stop();
  const auto token=core.recovery_.generation();core.launch();
  need(calls==1&&core.recovery_.generation()==token,"overlapping preparation was started");
  release.release();QEventLoop wait;QTimer timeout;timeout.setSingleShot(true);timeout.start(5000);
  QObject::connect(&timeout,&QTimer::timeout,&wait,&QEventLoop::quit);
  QObject::connect(&core.verifier_,&QFutureWatcher<CoreProfilePreparation>::finished,&wait,&QEventLoop::quit);
  if(core.verifier_.isRunning()||core.launching_)wait.exec();settle(200);
  need(!core.verifier_.isRunning()&&!core.launching_&&core.processId()==0&&!core.orphanedStart_,"cancelled preparation reached a child launch");
  need(!QFileInfo::exists(f.runtime+"/wallet-launch-intent.json"),"cancelled hash wrote a launch intent");
  need(!QFileInfo::exists(f.data+"/inert-starts.txt"),"STOP/update did not fence startDetached");
  need(read(f.runtime+"/wallet-core.json")==f.receipt,"cancelled hash rewrote approved profile");
  if(automatic)need(read(f.runtime+"/wallet-owned.json")==beforeOwner,"cancelled recovery wrote ownership");
  else need(!QFileInfo::exists(f.runtime+"/wallet-owned.json"),"cancelled initial hash wrote ownership");
  need(read(f.data+"/public-chain-marker")=="PUBLIC INERT CHAIN MARKER","cancelled preparation changed synthetic chain data");
 }
};
}

int main(int argc,char** argv){
 QCoreApplication application(argc,argv);
 for(int n=1;n<argc;++n){const QString value=QString::fromLocal8Bit(argv[n]);if(value.startsWith("-datadir=")){
  write(value.mid(9)+"/inert-starts.txt","INERT CHILD STARTED\n",true);QTimer::singleShot(500,&application,&QCoreApplication::quit);return application.exec();
 }}
 try{
  policyCases();evidenceCases();pgn::wallet::CoreRuntimeRecoveryTest::logAndStorage();pgn::wallet::CoreRuntimeRecoveryTest::firstOwnedLog();
  for(bool maintenance:{false,true})for(bool automatic:{false,true})pgn::wallet::CoreRuntimeRecoveryTest::cancelledHash(maintenance,automatic);
  std::cout<<"PASS "<<assertions<<" recovery assertions; private clocks/space/owner/log/hash fixtures, no Core/GPU/disk-fill\n";
 }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}
}

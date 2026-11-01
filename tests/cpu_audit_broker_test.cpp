#include "mining/cpu_audit_broker.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSysInfo>
#include <QEventLoop>
#include <QCryptographicHash>
#include <iostream>
using namespace pgn::mining;
struct CoreFixture {
 QString genesis=QString(64,'a'),parent=QString("0123456789abcdef").repeated(4),request=QString(64,'c'),root=QString(64,'d'),ticket=QString(64,'e'),actor="0014"+QString(40,'f'),block;
 QJsonArray ranges;bool available{};int signs{},submits{},assignments{};
 CoreFixture(bool supported):available(supported){const boost::multiprecision::cpp_int start("150000000000000000000");for(int i:{0,9,20,40,99})ranges.append(QJsonObject{{"index",i},{"start",QString::fromStdString((start+boost::multiprecision::cpp_int("2000000000000")*i).convert_to<std::string>())},{"end",QString::fromStdString((start+boost::multiprecision::cpp_int("2000000000000")*(i+1)).convert_to<std::string>())}});}
 pgn::wallet::ChainObservation observation(){return {{42,"closed-fixture-epoch",1},genesis,200,QDateTime::currentDateTimeUtc(),false,block.isEmpty()?parent:block,1};}
 void rpc(QString method,QJsonArray args,pgn::wallet::AuditEventPublisher::Reply reply){
  if(method=="getpgnauditevent"){++assignments;QJsonObject value{{"available",available},{"accepted",false},{"parent",parent},{"request_id",request},{"slot",0},{"attempt",0},{"challenge","PGN/UNAPPROVED/SCRIPT-WORK/1:fixture"},{"ranges",ranges}};if(args.size()==7)value.insert("envelope","6a");reply(value,{});return;}
  if(method=="getpgnbootstraptemplate"){QByteArray header(112,'\0');auto previous=QByteArray::fromHex(parent.toLatin1());std::reverse(previous.begin(),previous.end());header.replace(4,32,previous);reply(QJsonObject{{"schema","PGN_NATIVE112_BOOTSTRAP_TEMPLATE1"},{"genesis",genesis},{"parent",parent},{"work_verified",false},{"nonce_offset",82},{"nonce_bytes",8},{"header",QString::fromLatin1(header.toHex())},{"block",QString::fromLatin1((header+QByteArray("fixture body")).toHex())},{"numeric_target",QString(64,'f')}},{});return;}
  if(method=="submitblock"){++submits;const auto bytes=QByteArray::fromHex(args[0].toString().toLatin1());auto digest=QCryptographicHash::hash(QCryptographicHash::hash(bytes.first(112),QCryptographicHash::Sha256),QCryptographicHash::Sha256);std::reverse(digest.begin(),digest.end());block=QString::fromLatin1(digest.toHex());reply(QJsonValue{},{});return;}
  if(method=="getblock"){reply(QJsonObject{{"hash",block},{"confirmations",1},{"tx",QJsonArray{QJsonObject{{"vout",QJsonArray{QJsonObject{{"scriptPubKey",QJsonObject{{"hex","6a"}}}}}}}}}},{});return;}
  reply({},"Unexpected broker fixture RPC");
 }
 QJsonObject details(){
  const QJsonObject lease{{"request",request},{"report_root",root},{"state","submitted_pending_audits"},{"start","150000000000000000000"},{"end","150000200000000000000"}};
  const QJsonObject report{{"request_id",request},{"ticket",ticket},{"report_root",root}};
  const QJsonObject job{{"slot",0},{"attempt",0},{"actor",actor},{"state","assigned"},{"expired",false},{"issued",199},{"deadline_clock","median_chain_time"},{"issued_time",1000},{"deadline_time",87400},{"ranges",ranges}};
  return {{"genesis",genesis},{"bestblockhash",block},{"leases",QJsonArray{lease}},{"reports",QJsonArray{report}},{"audits",QJsonArray{QJsonObject{{"jobs",QJsonArray{job}}}}}};
 }
};
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 2;QCoreApplication::setOrganizationName("PGN-CPU-TEST");QCoreApplication::setApplicationName("isolated-broker");
 for(int mode=0;mode<3;++mode){
  QTemporaryDir directory;CoreFixture core(mode==1);CpuFleet fleet;QEventLoop loop;bool failed=false,finished=false;int stage=0,attached=0;const auto host=QSysInfo::machineHostName(),device="CPU/"+host.toUpper()+"/0";
  CpuAuditBroker::Services services;
  services.rpc=[&](auto method,auto params,auto reply){core.rpc(method,params,reply);};services.sign=[&](auto,auto,auto reply){++core.signs;reply("fixture-signature",{});};services.identity=[&](auto,auto reply){reply(QJsonObject{{"ismine",true},{"scriptPubKey",core.actor}},{});};services.observation=[&]{return std::optional{core.observation()};};services.details=[&](auto,auto consume){consume(core.details(),{});};
  if(mode==2){core.block=core.parent;const auto snapshot=core.details();const auto selection=cpuAuditWork(core.observation(),core.genesis,snapshot.value("reports").toArray().first().toObject(),snapshot.value("leases").toArray().first().toObject(),snapshot.value("audits").toArray().first().toObject().value("jobs").toArray().first().toObject(),core.actor,QDateTime::currentDateTimeUtc());if(!selection.work)return 5;CpuAuditJournal seed(directory.filePath("audit.json"));int count=0;for(int i=1;i<=20;++i){auto work=*selection.work;work.requestId=QString::number(i,16).rightJustified(64,'0');if(!seed.retainAssignment("CPU/reserved/0","tpgn1fixtureauditor",work))break;++count;}if(count==0||seed.canRequestAssignment())return 6;}
  CpuAuditBroker broker(directory.filePath("audit.json"),core.genesis,"tpgn1fixtureauditor",&fleet,services);
  auto fail=[&](const QString& error){std::cerr<<error.toStdString()<<'\n';failed=true;fleet.requestClose();QTimer::singleShot(1000,&loop,&QEventLoop::quit);};
  fleet.onTelemetry=[&](auto,QJsonObject inventory,QJsonObject status,bool connected){if(!connected)return;if(stage==0&&!inventory.isEmpty()&&status.value("state")=="IDLE"){stage=1;fleet.control(device,"on",true,qRound(100./inventory.value("maximum_workers").toInt()));}if(mode==0&&!status.value("worker_pids").toArray().isEmpty())fail("No-work audit polling started computation");};
  fleet.onControl=[&](auto,QString request,bool applied,QString error){if(!applied){fail(error);return;}if(request=="on")broker.pulse();else if(request=="off"){stage=3;fleet.requestClose();}};
  fleet.onAuditAttached=[&](auto,CpuAuditWork){++attached;stage=2;fleet.control(device,"off",false,qRound(100./fleet.status(device).value("requested_threads").toInt(1)));};
  fleet.onAuditComputation=[&](auto,auto,auto){fail("Bounded broker test unexpectedly closed a full audit");};fleet.onAuditError=[&](auto,auto error){fail(error);};fleet.onCloseReady=[&]{finished=true;loop.quit();};
  broker.onStatus=[&](QString text){if((mode==0&&text.contains("No independent"))||(mode==2&&text.contains("outbox is full"))){stage=2;fleet.control(device,"off",false,12);} };
  fleet.add({QString::fromLocal8Bit(argv[1]),{"--workspace",directory.filePath("worker"),"--piece-positions","100000000"},device,host});QTimer timeout;timeout.setSingleShot(true);QObject::connect(&timeout,&QTimer::timeout,&loop,[&]{fail("Broker test timeout");});timeout.start(15000);loop.exec();
  if(failed||!finished||stage!=3||!fleet.allStopped())return 1;
  if(mode==0&&(attached||core.signs||core.submits))return 3;
  if(mode==2&&(attached||core.assignments||core.signs||core.submits))return 7;
  if(mode==1&&(attached!=1||core.signs!=1||core.submits!=1||!QFileInfo::exists(directory.filePath("audit.json"))))return 4;
 }
 std::cout<<"PASS broker orchestration: no-work stays idle; assignment adopted by actual CPU service then paused; full journal refuses before any allocation/signature; no full scientific audit or network acceptance claim\n";
}

#include "wallet/audit_event_publisher.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QFile>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace pgn::wallet;
struct FakeCore {
 QString genesis=QString(64,'a'),parent=QString("0123456789abcdef").repeated(4),blockHash;
 bool available{true},loseSubmit{},dropSubmit{},knownBlock{},wrongSignatureParent{},wrongEnvelope{};int signs{},submits{};QString firstSubmitted;
 void rpc(QString method,QJsonArray args,AuditEventPublisher::Reply reply){
  if(method=="getpgnauditevent"){
   QJsonObject proposal{{"available",available},{"accepted",false},{"parent",parent},{"request_id",QString(64,'c')},{"slot",0},{"attempt",0},{"challenge","PGN/UNAPPROVED/SCRIPT-WORK/1:fixture"}};
   if(args.size()==7){proposal.insert("envelope","6a");if(wrongSignatureParent)proposal.insert("parent",QString(64,'0'));}reply(proposal,{});return;
  }
  if(method=="getpgnbootstraptemplate"){
   QByteArray header(112,'\0');auto previous=QByteArray::fromHex(parent.toLatin1());std::reverse(previous.begin(),previous.end());header.replace(4,32,previous);
   reply(QJsonObject{{"schema","PGN_NATIVE112_BOOTSTRAP_TEMPLATE1"},{"genesis",genesis},{"parent",parent},{"work_verified",false},{"nonce_offset",82},{"nonce_bytes",8},{"header",QString::fromLatin1(header.toHex())},{"block",QString::fromLatin1((header+QByteArray("fixture body")).toHex())},{"numeric_target",QString(64,'f')}},{});return;
  }
  if(method=="submitblock"){
   ++submits;if(firstSubmitted.isEmpty())firstSubmitted=args[0].toString();else if(firstSubmitted!=args[0].toString())throw std::runtime_error("replacement block submitted");const auto bytes=QByteArray::fromHex(args[0].toString().toLatin1());auto digest=QCryptographicHash::hash(QCryptographicHash::hash(bytes.first(112),QCryptographicHash::Sha256),QCryptographicHash::Sha256);std::reverse(digest.begin(),digest.end());blockHash=QString::fromLatin1(digest.toHex());if(!dropSubmit)knownBlock=true;reply(QJsonValue{},loseSubmit?QString("simulated lost response"):QString{});return;
  }
  if(method=="getblock"){if(!knownBlock){reply({},"simulated block not found");return;}reply(QJsonObject{{"hash",blockHash},{"confirmations",1},{"tx",QJsonArray{QJsonObject{{"vout",QJsonArray{QJsonObject{{"scriptPubKey",QJsonObject{{"hex",wrongEnvelope?"6b":"6a"}}}}}}}}}},{});return;}
  if(method=="getbestblockhash"){reply(parent,{});return;}
  reply({},"Unexpected fixture RPC");
 }
 void sign(QString,QString,AuditEventPublisher::Reply reply){++signs;reply("fixture-signature",{});}
};
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);int checks=0;auto require=[&](bool value){++checks;if(!value)throw std::runtime_error("publisher check "+std::to_string(checks));};
 for(int mode=0;mode<8;++mode){
  QTemporaryDir directory;FakeCore core;core.loseSubmit=mode==1||mode>=5;core.dropSubmit=mode>=5;core.available=mode!=2;core.wrongSignatureParent=mode==3;
  AuditEventPublisher publisher(directory.filePath("publication.json"),core.genesis,[&](auto method,auto args,auto reply){core.rpc(method,args,reply);},[&](auto address,auto challenge,auto reply){core.sign(address,challenge,reply);});
  QJsonObject receipt;QString error;bool done=false;QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);timeout.start(4000);
  publisher.publish({"assign","tpgn1fixtureauditor"},[&](auto value,auto problem){receipt=value;error=problem;done=true;loop.quit();},mode==4?AuditEventPublisher::ProposalGate([](const QJsonObject&){return false;}):AuditEventPublisher::ProposalGate{});if(!done)loop.exec();require(done&&!publisher.busy());
  if(mode==2||mode==4){require(!error.isEmpty()&&!publisher.pending()&&core.submits==0&&core.signs==0);continue;}
  if(mode==3){require(!error.isEmpty()&&!publisher.pending()&&core.submits==0&&core.signs==1);continue;}
  require(publisher.pending()&&core.submits==1&&core.signs==1);
  if(mode==6){core.parent=QString(64,'f');publisher.recover([&](auto value,auto problem){receipt=value;error=problem;});require(!error.isEmpty()&&receipt.isEmpty()&&publisher.pending()&&core.submits==1&&core.signs==1);require(!publisher.acknowledge(core.blockHash));continue;}
  if(mode==7){QFile journal(directory.filePath("publication.json"));require(journal.open(QIODevice::ReadOnly));auto value=QJsonDocument::fromJson(journal.readAll()).object();journal.close();auto block=value.value("block").toString();block[0]=block[0]=='0'?'1':'0';value.insert("block",block);require(journal.open(QIODevice::WriteOnly|QIODevice::Truncate));journal.write(QJsonDocument(value).toJson(QJsonDocument::Compact));journal.close();publisher.recover([&](auto value,auto problem){receipt=value;error=problem;});require(!error.isEmpty()&&receipt.isEmpty()&&publisher.pending()&&core.submits==1);continue;}
  if(mode==1||mode==5){require(!error.isEmpty()&&receipt.isEmpty());core.loseSubmit=false;core.dropSubmit=false;publisher.recover([&](auto value,auto problem){receipt=value;error=problem;});require(error.isEmpty()&&receipt.value("coordination_confirmed")==true);require(core.submits==(mode==5?2:1)&&core.signs==1);}
  else require(error.isEmpty()&&receipt.value("coordination_confirmed")==true);
  require(!publisher.acknowledge(QString(64,'0'))&&publisher.pending());
  bool refused=false;publisher.publish({"assign","tpgn1fixtureauditor"},[&](auto,auto problem){refused=!problem.isEmpty();});require(refused&&core.submits==(mode==5?2:1));
  core.wrongEnvelope=true;publisher.recover([&](auto,auto problem){require(!problem.isEmpty());});require(publisher.pending()&&!publisher.acknowledge(core.blockHash));core.wrongEnvelope=false;
  publisher.recover([&](auto value,auto problem){require(problem.isEmpty()&&value.value("block_hash")==core.blockHash);});
  require(publisher.acknowledge(core.blockHash)&&!publisher.pending());
 }
 std::cout<<"PASS "<<checks<<" publication state/retention checks using a closed transport fixture; no real audit or network acceptance claim\n";
}

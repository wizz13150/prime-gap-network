#undef NDEBUG
#include "social/social_core_service.h"
#include "social/social_signing_contract.h"
#include "social/social_leaderboard.h"
#include "social/social_numbers.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <cassert>
#include <iostream>
using namespace pgn::social;
QString signature(const QByteArray& b){return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toBase64());}
QString digest(const QByteArray& b){return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());}
QJsonObject body(const QByteArray& b){return QJsonDocument::fromJson(b.mid(b.indexOf('\n')+1)).object();}
bool waitFor(std::function<bool()> condition,int ms=3500){QElapsedTimer clock;clock.start();while(!condition()&&clock.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(4);}return condition();}
struct Node {
 QString genesis=QString(64,'a'),generation=QString(64,'c'),submitState{"accepted_local"};QJsonArray rows;int submitted{},signedCount{},verifiedCount{};bool loading{};
 CoreSocialCrypto crypto(){return {
  [this](QByteArray message,auto done){++signedCount;done(signature(message),{});},
  [this](QString,QByteArray message,QString proof,auto done){++verifiedCount;done(proof==signature(message),proof==signature(message)?QString{}:QString("Test signature mismatch"));}
 };}
 CoreSocialRpc rpc(){return [this](CoreSocialCall call,QJsonArray args,auto done){
  if(call==CoreSocialCall::ReadPeers){done(QJsonObject{{"genesis",genesis},{"relay_peers",QJsonArray{}}},{});return;}
  if(call==CoreSocialCall::SubmitEvent){++submitted;const auto message=args[0].toString().toUtf8();const auto proof=args[1].toString();bool found=false;for(const auto& r:rows)if(r.toObject().value("canonical_message")==args[0])found=true;if(!found&&submitState=="accepted_local")add(message,proof);done(QJsonObject{{"genesis",genesis},{"state",submitState},{"reason","Explicit native policy rejection"},{"proof_id",digest(message)}},{});return;}
  if(loading){done(QJsonObject{{"genesis",genesis},{"loading",true}},{});return;}
  int cursor=args[1].toInt();const bool reset=args[0].toString()!=generation&&!args[0].toString().isEmpty();if(reset)cursor=0;QJsonArray result;int next=cursor;for(int i=cursor;i<rows.size()&&result.size()<args[2].toInt();++i){result.append(rows[i]);next=i+1;}
  done(QJsonObject{{"genesis",genesis},{"generation",generation},{"records",result},{"next_cursor",next},{"more",next<rows.size()},{"reset",reset}},{});
 };}
 void add(QByteArray message,QString proof={}){rows.append(QJsonObject{{"seq",rows.size()+1},{"canonical_message",QString::fromUtf8(message)},{"signature",proof.isEmpty()?signature(message):proof}});}
};
QByteArray receiptFor(QByteArray message,QString other,qint64 time=QDateTime::currentMSecsSinceEpoch()){
 const auto old=body(message);QJsonObject r{{"v",1},{"genesis",old.value("genesis")},{"origin",old.value("origin")},{"sender_script",other},{"signing_address","fixture-B"},{"device",QString(32,'b')},{"channel",old.value("channel")},{"created_ms",time},{"nonce",QString(64,'d')},{"event_id",old.value("event_id")},{"recipient_script",old.value("sender_script")}};return "PGN/SOCIAL/RECEIPT/1\n"+canonical(r);
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);QTemporaryDir temp;assert(temp.isValid());Node node;
 const auto own="0014"+QString(40,'a'),other="0014"+QString(40,'b');CoreSocialContext context{{node.genesis,own},"fixture-A",QString(32,'a'),"global","Isolated test"};
 const auto path=temp.filePath("outbox.json");const SendRequest request{context.identity,"global","11111111-1111-4111-8111-111111111111","[b]Exact[/b] message"};int accepted{},posted{},rejected{},uncertain{};
 const auto response=[&](SendResult r){assert(r.identity==request.identity&&r.channel==request.channel&&r.requestId==request.requestId);if(r.outcome==SendOutcome::Accepted)++accepted;else if(r.outcome==SendOutcome::Posted)++posted;else if(r.outcome==SendOutcome::Rejected)++rejected;else ++uncertain;};
 QByteArray original;
 assert(leaderboardNumber("167000000000000000")=="1.670e17");
 assert(leaderboardNumber("9999500000000000")==QString::fromUtf8("≈1.000e16"));
 assert(leaderboardNumber("9007199254740993")==QString::fromUtf8("≈9.007e15"));
 assert(leaderboardNumber("1356")=="1356");
 {
  ReadRequest ranking{Section::Miners,context.identity};QJsonArray args;assert(leaderboardArguments(ranking,args));
  QJsonObject row{{"operator_id",QString(64,'1')},{"tickets_reported","12"},{"tickets_validated","11"},{"audits_finalized_valid","23"},{"validated_search_positions","9007199254740993"},{"last_activity_height",100}};
  QJsonObject page{{"schema","PGN_LEADERBOARD_V1"},{"sort","validated_search_positions_desc_then_operator_id_asc"},{"genesis",node.genesis},{"bestblockhash",QString(64,'b')},{"height",101},{"offset",0},{"next_offset",1},{"total",1},{"more",false},{"rows",QJsonArray{row}}};Snapshot decoded;
  assert(decodeLeaderboard(ranking,page,decoded).isEmpty()&&validate(decoded,ranking).isEmpty()&&decoded.miners[0].validatedSearchPositions.value=="9007199254740993");
  // Main decodes each RPC response into a fresh Snapshot.
  decoded={};row.insert("display_name","Wizz");page.insert("rows",QJsonArray{row});assert(decodeLeaderboard(ranking,page,decoded).isEmpty()&&decoded.miners[0].name.startsWith("Wizz · ")&&decoded.miners[0].id==QString(64,'1'));
  decoded={};row.insert("display_name",QString("Wizz")+QChar(0x202e)+"spoof");page.insert("rows",QJsonArray{row});assert(decodeLeaderboard(ranking,page,decoded).isEmpty()&&decoded.miners[0].name.startsWith("Miner "));
  row.remove("display_name");page.insert("rows",QJsonArray{row});
  auto malformedSort=page;malformedSort.insert("sort","unverified_order");assert(!decodeLeaderboard(ranking,malformedSort,decoded).isEmpty());
  auto wrong=page;wrong.insert("genesis",QString(64,'f'));assert(!decodeLeaderboard(ranking,wrong,decoded).isEmpty());
  ranking.cursor=QString(64,'c')+":0";assert(!decodeLeaderboard(ranking,page,decoded).isEmpty());ranking.cursor.clear();
  row.insert("validated_search_positions",9007199254740993.);page.insert("rows",QJsonArray{row});assert(!decodeLeaderboard(ranking,page,decoded).isEmpty());
 }
 {
  Node denied;denied.submitState="rejected";CoreSocialService service(context,denied.crypto(),denied.rpc(),temp.filePath("rejected-outbox.json"));service.start();int rejected=0;
  service.services().sendChat(request,[&](SendResult result){if(result.outcome==SendOutcome::Rejected)++rejected;});assert(rejected==1&&denied.submitted==1);
  waitFor([]{return false;},2200);assert(denied.submitted==1);
  denied.submitState="accepted_local";service.services().sendChat(request,[](SendResult){});assert(denied.submitted==2&&denied.signedCount==1);service.stop();
 }
 {
  node.loading=true;CoreSocialService service(context,node.crypto(),node.rpc(),temp.filePath("loading-outbox.json"));QString reason;service.start();
  service.services().read({Section::Chat,context.identity},[&](Snapshot,QString error){reason=error;});assert(reason.contains("Loading verified Social history"));
  node.loading=false;assert(waitFor([&]{bool ready=false;service.services().read({Section::Chat,context.identity},[&](Snapshot value,QString error){ready=error.isEmpty()&&value.authenticated;});return ready;}));service.stop();
 }
 {
  CoreSocialService service(context,node.crypto(),node.rpc(),path);service.start();bool ready=false;service.services().read({Section::Chat,context.identity},[&](Snapshot s,QString e){ready=e.isEmpty()&&s.authenticated;});assert(ready);
  service.services().sendChat(request,response);assert(node.submitted==1&&node.signedCount==1&&accepted==0&&posted==1);original=node.rows[0].toObject().value("canonical_message").toString().toUtf8();assert(QFile::exists(path));
  auto changed=request;changed.bbcode="Different bytes";service.services().sendChat(changed,response);assert(rejected==1&&node.submitted==1);
  auto wrong=request;wrong.identity.genesis=QString(64,'e');bool wrongRejected=false;service.services().sendChat(wrong,[&](SendResult r){wrongRejected=r.outcome==SendOutcome::Rejected;});assert(wrongRejected);
  const auto oldReceipt=receiptFor(original,other,QDateTime::currentMSecsSinceEpoch()-366LL*86400000);
  const SigningContext signing{context.identity.genesis,other,"fixture-B"};assert(signingRequestError(signing,oldReceipt,QDateTime::currentMSecsSinceEpoch(),EnvelopeUse::StoredVerification).isEmpty());assert(!signingRequestError(signing,oldReceipt).isEmpty());
  node.add(oldReceipt);assert(waitFor([&]{bool delivered=false;service.services().sendChat(request,[&](SendResult r){delivered=r.outcome==SendOutcome::Accepted;});return delivered;}));assert(node.verifiedCount>=2);const int submissions=node.submitted;service.services().sendChat(request,response);assert(accepted==1&&node.submitted==submissions);service.stop();
 }
 const int signedBefore=node.signedCount,submitBefore=node.submitted;
 {
  CoreSocialService service(context,node.crypto(),node.rpc(),path);service.start();service.services().sendChat(request,response);assert(accepted==2&&node.signedCount==signedBefore&&node.submitted==submitBefore);service.stop();
 }
 // A local boolean never grants proof of remote delivery.
 QFile saved(path);assert(saved.open(QIODevice::ReadOnly));auto root=QJsonDocument::fromJson(saved.readAll()).object();saved.close();auto ops=root.value("operations").toObject();auto op=ops.value(request.requestId).toObject();op.insert("confirmed",true);op.remove("receipt");op.remove("receipt_signature");ops.insert(request.requestId,op);root.insert("operations",ops);assert(saved.open(QIODevice::WriteOnly|QIODevice::Truncate));saved.write(canonical(root));saved.close();
 node.rows={};node.generation=QString(64,'f');
 {
  CoreSocialService service(context,node.crypto(),node.rpc(),path);service.start();service.services().sendChat(request,response);assert(accepted==2&&posted==2);assert(node.rows.size()==1&&node.rows[0].toObject().value("canonical_message").toString().toUtf8()==original);service.stop();assert(uncertain==0);
 }
 // A forged on-disk local/confirmed flag cannot claim either publication or delivery.
 {
  QFile f(path);assert(f.open(QIODevice::ReadOnly));auto data=QJsonDocument::fromJson(f.readAll()).object();f.close();auto all=data.value("operations").toObject();auto op=all.value(request.requestId).toObject();op.insert("local",true);op.insert("confirmed",true);op.remove("receipt");op.remove("receipt_signature");all.insert(request.requestId,op);data.insert("operations",all);assert(f.open(QIODevice::WriteOnly|QIODevice::Truncate));f.write(canonical(data));f.close();
  Node missing;missing.submitState="queued";CoreSocialService service(context,missing.crypto(),missing.rpc(),path);service.start();bool falseSuccess=false,unknown=false;
  service.services().sendChat(request,[&](SendResult r){falseSuccess=r.outcome==SendOutcome::Posted||r.outcome==SendOutcome::Accepted;unknown=r.outcome==SendOutcome::Uncertain;});assert(!falseSuccess);service.stop();assert(unknown&&!falseSuccess);
 }
 // One-user channels keep working after the retained operation window fills;
 // each evicted operation was read back with its exact verified native proof.
 {
  Node alone;CoreSocialService service(context,alone.crypto(),alone.rpc(),temp.filePath("alone.json"));service.start();int count=0,fail=0;
  for(int i=0;i<140;++i){auto r=request;r.requestId=QString("%1-1111-4111-8111-111111111111").arg(i,8,16,QChar('0'));r.bbcode=QString("Solo message %1").arg(i);service.services().sendChat(r,[&](SendResult result){if(result.outcome==SendOutcome::Posted)++count;else ++fail;});}
  assert(count==140&&fail==0&&alone.rows.size()==140);service.stop();QFile f(temp.filePath("alone.json"));assert(f.open(QIODevice::ReadOnly));assert(QJsonDocument::fromJson(f.readAll()).object().value("operations").toObject().size()==128);
 }
 std::cout<<"{\"PASS\":true,\"scope\":\"Adapter protocol fixtures including native loading, definitive rejection and exact explicit retry; cryptography is separately tested, not simulated as a product proof\"}\n";
}

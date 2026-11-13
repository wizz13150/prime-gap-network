#include "social/social_core_service.h"
#include "social/social_signing_contract.h"
#include "social/social_public_names.h"
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTimer>
#include <algorithm>
#include <cstring>

namespace pgn::social {
namespace {
constexpr int OutboxBytes=2*1024*1024,RetainedOperations=128,VisibleRecords=1000;
QString hash(const QByteArray& bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
QString nonce(){QByteArray b(32,Qt::Uninitialized);for(int i=0;i<32;i+=4){auto n=QRandomGenerator::system()->generate();memcpy(b.data()+i,&n,4);}return QString::fromLatin1(b.toHex());}
QByteArray envelope(const QByteArray& domain,const QJsonObject& object){return domain+'\n'+canonical(object);}
QJsonObject fields(const QByteArray& message){return QJsonDocument::fromJson(message.mid(message.indexOf('\n')+1)).object();}
bool requestId(const QString& s){return QRegularExpression("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$").match(s).hasMatch();}
}
struct CoreSocialService::Impl {
 CoreSocialService* owner;CoreSocialContext context;CoreSocialCrypto crypto;CoreSocialRpc rpc;QString path;
 QTimer clock;bool running{},reading{},submitting{},ready{},outboxBad{};quint64 epoch{};
 QString generation,error,historyNotice; qint64 cursor{};int peers{};
  PublicNames names;QElapsedTimer nameClock;bool nameReading{};quint64 nameLookup{};qint64 nextNames{};QString nameError;
 struct Record{QByteArray message;QString signature;QJsonObject object;};
 struct Operation{QString digest;QByteArray message;QString signature;bool local{},confirmed{};QByteArray receipt;QString receiptSignature;bool rejected{},posted{};};
 struct Waiting{SendRequest request;std::function<void(SendResult)> done;QPointer<QTimer> timer;};
 QMap<QString,Record> records;QMap<QString,Operation> operations;QMap<QString,QVector<Waiting>> waiting;
 QSet<QString> receipting;
 Impl(CoreSocialService* o,CoreSocialContext c,CoreSocialCrypto k,CoreSocialRpc r,QString p):owner(o),context(std::move(c)),crypto(std::move(k)),rpc(std::move(r)),path(std::move(p)){
  clock.setInterval(2000);QObject::connect(&clock,&QTimer::timeout,owner,[this]{poll();retry();pollNames();});
 }
 void changed(){if(owner->changed)owner->changed();}
 bool namesFailed(QString reason){const auto previous=names.revision();names.clear();nameReading=false;nextNames=nameClock.elapsed()+5000;
  if(nameError!=reason){nameError=std::move(reason);if(owner->diagnostic)owner->diagnostic("Public names: "+nameError);}return previous!=names.revision();}
 void namePage(QString cursor,quint64 lookup){
  if(!running||!nameReading||lookup!=nameLookup||!owner->leaderboard)return;
  if(names.remaining()<=0){if(namesFailed("Registry exceeds the bounded name lookup; cryptographic IDs retained."))changed();return;}
  const ReadRequest request{Section::Miners,context.identity,Window::AllTime,std::move(cursor),qMin(100,names.remaining())};
  const QPointer<CoreSocialService> weak(owner);const auto e=epoch;const auto completed=std::make_shared<bool>(false);
  owner->leaderboard(request,[weak,e,lookup,request,completed](Snapshot snapshot,QString failure){
   if(!weak||!weak->impl_->running||weak->impl_->epoch!=e||!weak->impl_->nameReading||weak->impl_->nameLookup!=lookup||*completed)return;*completed=true;
   auto& p=*weak->impl_;const auto before=p.names.revision();
   if(failure.isEmpty())failure=p.names.append(request,snapshot);
   if(!failure.isEmpty()){p.namesFailed(std::move(failure));if(before!=p.names.revision())p.changed();return;}
   if(p.names.collecting()){const auto next=p.names.nextCursor();QTimer::singleShot(0,weak,[weak,e,lookup,next]{if(weak&&weak->impl_->running&&weak->impl_->epoch==e)weak->impl_->namePage(next,lookup);});}
   else{p.nameReading=false;p.nextNames=p.nameClock.elapsed()+60000;p.nameError.clear();if(before!=p.names.revision())p.changed();}
  });
 }
 void pollNames(){
  if(!running||!ready||nameReading||!owner->leaderboard||!nameClock.isValid()||nameClock.elapsed()<nextNames)return;
  nameReading=true;names.begin(context.identity);namePage({},++nameLookup);
 }
 QString displayName(const QString& script)const{const auto resolved=names.display(context.identity,script);return resolved.isEmpty()?script:resolved;}
 void report(QString e){if(error==e)return;error=std::move(e);if(owner->diagnostic)owner->diagnostic(error);changed();}
 QJsonObject common()const{return {{"v",1},{"genesis",context.identity.genesis},{"origin","pgn-social://"+context.identity.genesis},{"sender_script",context.identity.subject},{"signing_address",context.address},{"device",context.device},{"channel",context.channel},{"created_ms",QDateTime::currentMSecsSinceEpoch()},{"nonce",nonce()}};}
 bool validContext()const{return context.identity.valid()&&hexField(context.identity.subject,44)&&context.identity.subject.startsWith("0014")&&hexField(context.device,32)&&!context.address.isEmpty()&&bool(crypto.sign)&&bool(crypto.verify)&&bool(rpc);}
 QString save(){
  if(outboxBad)return "Pending messages could not be read; the existing file is preserved.";
  while(operations.size()>RetainedOperations){auto victim=operations.end();for(auto it=operations.begin();it!=operations.end();++it)if(it->confirmed||it->posted){victim=it;break;}if(victim==operations.end())break;operations.erase(victim);}
  QJsonObject ops;for(auto it=operations.begin();it!=operations.end();++it)ops.insert(it.key(),QJsonObject{{"digest",it->digest},{"message",QString::fromUtf8(it->message)},{"signature",it->signature},{"local",it->local},{"confirmed",it->confirmed},{"receipt",QString::fromUtf8(it->receipt)},{"receipt_signature",it->receiptSignature}});
  const auto bytes=canonical({{"schema",1},{"genesis",context.identity.genesis},{"subject",context.identity.subject},{"operations",ops}});
  if(bytes.size()>OutboxBytes)return "Too many pending messages; earlier messages and drafts are preserved.";
  if(!QDir(QFileInfo(path).absolutePath()).mkpath("."))return "Pending-message folder is unavailable.";
  QSaveFile f(path);if(!f.open(QIODevice::WriteOnly))return "Pending messages could not be saved.";
  f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
  if(f.write(bytes)!=bytes.size()||!f.commit())return "Pending-message write did not commit.";return {};
 }
 void load(){
  QFile f(path);if(!f.exists())return;
  if(!f.open(QIODevice::ReadOnly)||f.size()>OutboxBytes){outboxBad=true;report("Pending-message file is unreadable; it was preserved.");return;}
  QJsonParseError parse;const auto doc=QJsonDocument::fromJson(f.readAll(),&parse);const auto root=doc.object();
  if(parse.error!=QJsonParseError::NoError||root.value("schema").toInt()!=1||root.value("genesis").toString()!=context.identity.genesis||root.value("subject").toString()!=context.identity.subject||!root.value("operations").isObject()||root.value("operations").toObject().size()>RetainedOperations){outboxBad=true;report("Pending-message identity or format mismatch; file preserved.");return;}
  const auto ops=root.value("operations").toObject();for(auto it=ops.begin();it!=ops.end();++it){const auto v=it.value().toObject();Operation op{v.value("digest").toString(),v.value("message").toString().toUtf8(),v.value("signature").toString(),false,false,v.value("receipt").toString().toUtf8(),v.value("receipt_signature").toString()};const auto body=fields(op.message);
   if(!requestId(it.key())||!hexField(op.digest,64)||body.value("sender_script").toString()!=context.identity.subject||!signingRequestError({context.identity.genesis,context.identity.subject,context.address},op.message,QDateTime::currentMSecsSinceEpoch(),EnvelopeUse::StoredVerification).isEmpty()||hash(QByteArray::fromBase64(body.value("bbcode_b64").toString().toLatin1()))!=op.digest||op.signature.size()>512){outboxBad=true;operations.clear();report("Pending-message record is invalid; file preserved.");return;}operations.insert(it.key(),std::move(op));
  }
 }
 void checkSaved(QStringList ids,int index,std::function<void()> done){
  if(index>=ids.size()){done();return;}const auto id=ids[index];auto& op=operations[id];const auto receipt=op.receipt;const auto signature=op.receiptSignature;
  if(receipt.isEmpty()||signature.isEmpty()){checkSaved(ids,index+1,std::move(done));return;}
  const auto body=fields(receipt),original=fields(op.message);const auto script=body.value("sender_script").toString();
  if(!receipt.startsWith("PGN/SOCIAL/RECEIPT/1\n")||body.value("recipient_script").toString()!=context.identity.subject||script==context.identity.subject||body.value("event_id")!=original.value("event_id")||body.value("channel")!=original.value("channel")||!signingRequestError({context.identity.genesis,script,body.value("signing_address").toString()},receipt,QDateTime::currentMSecsSinceEpoch(),EnvelopeUse::StoredVerification).isEmpty()){op.receipt.clear();op.receiptSignature.clear();checkSaved(ids,index+1,std::move(done));return;}
  const QPointer<CoreSocialService> weak(owner);const auto e=epoch;
  crypto.verify(script,receipt,signature,[weak,e,ids,index,id,done=std::move(done)](bool valid,QString)mutable{if(!weak||!weak->impl_->running||weak->impl_->epoch!=e)return;auto& p=*weak->impl_;p.operations[id].confirmed=valid;if(!valid){p.operations[id].receipt.clear();p.operations[id].receiptSignature.clear();}p.checkSaved(ids,index+1,std::move(done));});
 }
 void finish(const QString& id,SendOutcome outcome,QString reason){
  const auto list=waiting.take(id);const auto event=fields(operations.value(id).message).value("event_id").toString();
  for(const auto& w:list){if(w.timer){w.timer->stop();w.timer->deleteLater();}w.done({w.request.identity,w.request.channel,w.request.requestId,outcome,event,reason});}
 }
 void submitted(const QString& id){
  if(!operations.contains(id)||operations[id].confirmed||submitting||!running)return;
  auto& op=operations[id];if(op.signature.isEmpty()){
   submitting=true;const QPointer<CoreSocialService> weak(owner);const auto e=epoch;
   crypto.sign(op.message,[weak,e,id](QString signature,QString failure){if(!weak||!weak->impl_->running||weak->impl_->epoch!=e)return;auto& p=*weak->impl_;p.submitting=false;if(!p.operations.contains(id))return;
    if(!failure.isEmpty()||signature.isEmpty()||signature.size()>512){p.finish(id,SendOutcome::Rejected,failure.isEmpty()?QString("Wallet signature unavailable."):failure);return;}
    p.operations[id].signature=signature;const auto error=p.save();if(!error.isEmpty()){p.finish(id,SendOutcome::Uncertain,error);return;}p.submitted(id);
   });return;
  }
  submitting=true;const QPointer<CoreSocialService> weak(owner);const auto e=epoch;
  rpc(CoreSocialCall::SubmitEvent,{QString::fromUtf8(op.message),op.signature},[weak,e,id](QJsonValue value,QString failure){if(!weak||!weak->impl_->running||weak->impl_->epoch!=e)return;auto& p=*weak->impl_;p.submitting=false;const auto v=value.toObject();
   if(!p.operations.contains(id))return;
   if(!failure.isEmpty()||v.value("genesis").toString()!=p.context.identity.genesis){p.report(failure.isEmpty()?QString("Social submission network mismatch."):failure);return;}
   const auto state=v.value("state").toString();if(state=="accepted_local"){p.operations[id].local=true;const auto saved=p.save();if(!saved.isEmpty())p.report(saved);p.poll();}
   else if(state=="rejected"){p.operations[id].rejected=true;p.finish(id,SendOutcome::Rejected,v.value("reason").toString());}
   else if(state!="queued"&&state!="uncertain")p.finish(id,SendOutcome::Uncertain,"Unrecognized Social submission state; draft retained.");
  });
 }
 void retry(){if(!running||submitting||outboxBad)return;for(auto it=operations.begin();it!=operations.end();++it)if(!it->confirmed&&!it->local&&!it->rejected){submitted(it.key());return;}}
 void send(SendRequest request,std::function<void(SendResult)> done){
  const auto reject=[&](QString reason){done({request.identity,request.channel,request.requestId,SendOutcome::Rejected,{},reason});};
  if(!running||!validContext()||outboxBad||request.identity!=context.identity||request.channel!=context.channel||!requestId(request.requestId)||request.bbcode.trimmed().isEmpty()||request.bbcode.size()>4000||QString::fromUtf8(request.bbcode.toUtf8())!=request.bbcode){reject("Verified Social identity, channel and message required.");return;}
  const auto digest=hash(request.bbcode.toUtf8());auto existing=operations.find(request.requestId);
  if(existing!=operations.end()&&existing->digest!=digest){reject("Retry content differs from the saved message.");return;}
  if(existing!=operations.end()&&existing->confirmed){done({request.identity,request.channel,request.requestId,SendOutcome::Accepted,fields(existing->message).value("event_id").toString(),"Delivered · signed peer receipt verified"});return;}
  if(existing!=operations.end()&&existing->posted){done({request.identity,request.channel,request.requestId,SendOutcome::Posted,fields(existing->message).value("event_id").toString(),"Posted on this node · recipient delivery not confirmed"});return;}
  if(existing==operations.end()){
   if(operations.size()>=RetainedOperations){auto victim=operations.end();for(auto it=operations.begin();it!=operations.end();++it)if(it->confirmed||it->posted||it->rejected){victim=it;break;}if(victim==operations.end()){reject("Unpublished messages must settle before adding more messages.");return;}operations.erase(victim);}
   auto body=common();body.insert("kind","chat");body.insert("bbcode_b64",QString::fromLatin1(request.bbcode.toUtf8().toBase64()));body.insert("parents",QJsonArray{});body.insert("event_id",hash("PGN/SOCIAL/EVENT-ID/1\n"+canonical(body)));
   const auto message=envelope("PGN/SOCIAL/EVENT/1",body);const auto invalid=signingRequestError({context.identity.genesis,context.identity.subject,context.address},message);
   if(!invalid.isEmpty()){reject(invalid);return;}operations.insert(request.requestId,{digest,message,{},{},false});const auto saved=save();if(!saved.isEmpty()){operations.remove(request.requestId);reject(saved);return;}
  }
  // A definitive native rejection ends automatic retries. An explicit user
  // retry may recheck it. This flag is deliberately not trusted/persisted in
  // the outbox: restart revalidates the operation against the current Core.
  operations[request.requestId].rejected=false;
  auto* timer=new QTimer(owner);timer->setSingleShot(true);waiting[request.requestId].append({request,std::move(done),timer});
  QObject::connect(timer,&QTimer::timeout,owner,[this,id=request.requestId]{finish(id,SendOutcome::Uncertain,"Local publication not verified yet; retry keeps the same message.");});timer->start(12000);submitted(request.requestId);
 }
 void receiptFor(const Record& record){
  const auto& v=record.object;if(v.value("kind").toString()!="chat"||v.value("sender_script").toString()==context.identity.subject||v.value("channel").toString()!=context.channel)return;
  const auto id=v.value("event_id").toString();if(receipting.contains(id))return;
  for(const auto& old:records)if(old.message.startsWith("PGN/SOCIAL/RECEIPT/1\n")&&old.object.value("sender_script").toString()==context.identity.subject&&old.object.value("recipient_script")==v.value("sender_script")&&old.object.value("event_id")==v.value("event_id"))return;
  receipting.insert(id);auto body=common();body.insert("event_id",id);body.insert("recipient_script",v.value("sender_script"));const auto message=envelope("PGN/SOCIAL/RECEIPT/1",body);const QPointer<CoreSocialService> weak(owner);const auto e=epoch;
  crypto.sign(message,[weak,e,id,message](QString signature,QString failure){if(!weak||!weak->impl_->running||weak->impl_->epoch!=e)return;auto& p=*weak->impl_;if(!failure.isEmpty()||signature.isEmpty()){p.receipting.remove(id);return;}
   p.rpc(CoreSocialCall::SubmitEvent,{QString::fromUtf8(message),signature},[weak,e,id](QJsonValue value,QString error){if(!weak||!weak->impl_->running||weak->impl_->epoch!=e)return;auto& p=*weak->impl_;const auto v=value.toObject();if(!error.isEmpty()||v.value("genesis").toString()!=p.context.identity.genesis||v.value("state").toString()=="rejected")p.receipting.remove(id);});
  });
 }
 // A successful RPC reply is not a delivery receipt. Clear the composer only
 // after the exact signed event is read back from authenticated native history.
 // posted is intentionally not loaded from a disk boolean; restart rechecks it.
 void observePosted(const Record& record){
  if(!record.message.startsWith("PGN/SOCIAL/EVENT/1\n")||record.object.value("sender_script").toString()!=context.identity.subject)return;
  for(auto it=operations.begin();it!=operations.end();++it){
   if(it->message!=record.message||it->signature!=record.signature||it->confirmed||it->posted)continue;
   it->posted=true;it->local=true;finish(it.key(),SendOutcome::Posted,"Posted on this node · recipient delivery not confirmed");break;
  }
 }
 void receive(QJsonArray rows,int index,std::function<void(QString)> done){
  if(index>=rows.size()){done({});return;}
  const auto row=rows[index].toObject();const auto message=row.value("canonical_message").toString().toUtf8();const auto signature=row.value("signature").toString();const auto body=fields(message);const auto script=body.value("sender_script").toString();
  const auto invalid=signingRequestError({context.identity.genesis,script,body.value("signing_address").toString()},message,QDateTime::currentMSecsSinceEpoch(),EnvelopeUse::StoredVerification);
  if(!invalid.isEmpty()||signature.isEmpty()||signature.size()>512){done("Social page contains a malformed proof.");return;}
  const auto key=hash(message);if(records.contains(key)){if(records[key].signature!=signature){done("Conflicting Social proof bytes.");return;}observePosted(records[key]);receive(rows,index+1,std::move(done));return;}
  const QPointer<CoreSocialService> weak(owner);const auto e=epoch;
  crypto.verify(script,message,signature,[weak,e,rows,index,message,signature,body,key,done=std::move(done)](bool valid,QString reason)mutable{
   if(!weak||!weak->impl_->running||weak->impl_->epoch!=e)return;auto& p=*weak->impl_;if(!valid){done(reason.isEmpty()?QString("Social signature refused."):reason);return;}
   p.records.insert(key,{message,signature,body});p.observePosted(p.records[key]);
   if(message.startsWith("PGN/SOCIAL/RECEIPT/1\n")&&body.value("recipient_script").toString()==p.context.identity.subject&&body.value("sender_script").toString()!=p.context.identity.subject){
    for(auto it=p.operations.begin();it!=p.operations.end();++it)if(fields(it->message).value("event_id")==body.value("event_id")&&fields(it->message).value("channel")==body.value("channel")){it->confirmed=true;it->receipt=message;it->receiptSignature=signature;const auto id=it.key();const auto saved=p.save();if(saved.isEmpty())p.finish(id,SendOutcome::Accepted,"Delivered · signed peer receipt verified");else p.report(saved);break;}
   }
   while(p.records.size()>VisibleRecords){auto oldest=p.records.begin();for(auto it=p.records.begin();it!=p.records.end();++it)if(it->object.value("created_ms").toInteger()<oldest->object.value("created_ms").toInteger())oldest=it;p.records.erase(oldest);}p.receive(rows,index+1,std::move(done));
  });
 }
 void poll(){
  if(!running||reading||!rpc)return;reading=true;const QPointer<CoreSocialService> weak(owner);const auto e=epoch;
  rpc(CoreSocialCall::ReadEvents,{generation,cursor,50},[weak,e](QJsonValue value,QString failure){if(!weak||!weak->impl_->running||weak->impl_->epoch!=e)return;auto& p=*weak->impl_;const auto v=value.toObject();
   if(failure.isEmpty()&&v.value("genesis").toString()==p.context.identity.genesis&&v.value("loading").toBool()){
    p.reading=false;p.ready=false;p.report("Loading verified Social history…");return;
   }
   if(!failure.isEmpty()||v.value("genesis").toString()!=p.context.identity.genesis||!v.value("records").isArray()||v.value("records").toArray().size()>50||!hexField(v.value("generation").toString(),64)||v.value("next_cursor").toInteger(-1)<0){p.reading=false;p.ready=false;p.report(failure.isEmpty()?QString("Social history response is invalid."):failure);return;}
   const auto gen=v.value("generation").toString();if(!p.generation.isEmpty()&&p.generation!=gen){p.records.clear();p.receipting.clear();for(auto& op:p.operations)if(!op.confirmed){op.local=false;op.posted=false;}p.historyNotice="History source restarted with a new generation.";}
   if(v.value("reset").toBool())p.historyNotice="Earlier history is outside this relay's retained window.";
   p.receive(v.value("records").toArray(),0,[weak,e,v,gen](QString error){if(!weak||!weak->impl_->running||weak->impl_->epoch!=e)return;auto& p=*weak->impl_;p.reading=false;if(!error.isEmpty()){p.ready=false;p.report(error);return;}
    const auto next=v.value("next_cursor").toInteger();const bool notify=!p.ready||p.generation!=gen||p.cursor!=next||!p.error.isEmpty();p.generation=gen;p.cursor=next;p.ready=true;p.error.clear();if(notify)p.changed();
    if(v.value("more").toBool())QTimer::singleShot(0,weak,[weak]{if(weak)weak->impl_->poll();});
    else{for(const auto& r:p.records){const auto count=p.receipting.size();p.receiptFor(r);if(p.receipting.size()>count)break;}}
   });
  });
  rpc(CoreSocialCall::ReadPeers,{},[weak,e](QJsonValue value,QString failure){
   if(!weak||!weak->impl_->running||weak->impl_->epoch!=e)return;
   auto& p=*weak->impl_;const auto v=value.toObject();
   if(!failure.isEmpty()||v.value("genesis").toString()!=p.context.identity.genesis||!v.value("relay_peers").isArray())return;
   const auto links=v.value("relay_peers").toArray();if(links.size()>1024)return;
   QSet<qint64> peers;for(const auto& entry:links){const auto link=entry.toObject();const auto id=link.value("node_id").toInteger(-1);if(id>=0&&link.value("matching_capability").toBool())peers.insert(id);}
   if(p.peers!=peers.size()){p.peers=peers.size();p.changed();}
  });
 }
 Snapshot snapshot(ReadRequest request)const{
  // Carry the existing authenticated native continuity token to the UI. A
  // refresh returns the latest page, not a deletion of earlier loaded pages.
  Snapshot out;out.identity=context.identity;out.section=request.section;out.authenticated=ready;out.source="PGN native relay · wallet signatures verified";out.observedUtc=QDateTime::currentDateTimeUtc();out.channel=context.channel;out.connection=ConnectionSummary{context.networkLabel,peers};out.channels=QVector<Channel>{{context.channel,"Global","Network conversation"}};out.chatSend=SendAvailability{ready&&!outboxBad,outboxBad?QString("Pending-message file needs attention; existing file preserved."):QString("Wallet-signed publication; recipient delivery is confirmed separately.")};
  out.chatHistoryGeneration=generation;QVector<Record> visible;QMap<QString,QDateTime> authors;
  for(const auto& r:records)if(r.message.startsWith("PGN/SOCIAL/EVENT/1\n")&&r.object.value("channel").toString()==context.channel){visible.append(r);const auto script=r.object.value("sender_script").toString();const auto time=QDateTime::fromMSecsSinceEpoch(r.object.value("created_ms").toInteger(),Qt::UTC);if(!authors.contains(script)||authors[script]<time)authors[script]=time;}
  std::sort(visible.begin(),visible.end(),[](const Record& a,const Record& b){const auto x=a.object.value("created_ms").toInteger(),y=b.object.value("created_ms").toInteger();return x==y?a.object.value("event_id").toString()<b.object.value("event_id").toString():x<y;});
  int end=visible.size();if(!request.cursor.isEmpty()){end=-1;for(int i=0;i<visible.size();++i)if(visible[i].object.value("event_id").toString()==request.cursor){end=i;break;}if(end<0)end=0;}
   const int begin=qMax(0,end-qBound(1,request.limit,100));for(int i=begin;i<end;++i){const auto& v=visible[i].object;Entry entry;entry.id=v.value("event_id").toString();entry.authorId=v.value("sender_script").toString();entry.author=displayName(entry.authorId);entry.createdUtc=QDateTime::fromMSecsSinceEpoch(v.value("created_ms").toInteger(),Qt::UTC);entry.body=QString::fromUtf8(QByteArray::fromBase64(v.value("bbcode_b64").toString().toLatin1()));entry.bbcode=true;out.entries.append(entry);}if(begin>0)out.nextCursor=visible[begin].object.value("event_id").toString();
   QVector<Participant> members{{context.identity.subject,displayName(context.identity.subject),"You",{}}};for(auto it=authors.begin();it!=authors.end()&&members.size()<200;++it)if(it.key()!=context.identity.subject)members.append({it.key(),displayName(it.key()),"Last message "+it.value().toString("HH:mm 'UTC'"),{}});out.participants=members;return out;
 }
};
CoreSocialService::CoreSocialService(CoreSocialContext c,CoreSocialCrypto k,CoreSocialRpc r,QString path,QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(this,std::move(c),std::move(k),std::move(r),std::move(path))){}
CoreSocialService::~CoreSocialService(){stop();}
const Identity& CoreSocialService::identity()const{return impl_->context.identity;}
void CoreSocialService::start(){auto& p=*impl_;if(p.running)return;if(!p.validContext()){p.report("Verified Social wallet context is unavailable.");return;}p.running=true;++p.epoch;p.nameClock.start();p.nextNames=0;p.nameReading=false;p.load();const QPointer<CoreSocialService> weak(this);const auto e=p.epoch;p.checkSaved(p.operations.keys(),0,[weak,e]{if(!weak||!weak->impl_->running||weak->impl_->epoch!=e)return;auto& p=*weak->impl_;p.clock.start();p.poll();p.retry();p.pollNames();});}
void CoreSocialService::stop(){auto& p=*impl_;if(!p.running)return;p.running=false;++p.epoch;p.clock.stop();p.nameReading=false;++p.nameLookup;p.names.clear();const auto ids=p.waiting.keys();for(const auto& id:ids)p.finish(id,SendOutcome::Uncertain,"Social connection changed; pending message preserved.");}
Services CoreSocialService::services(){Services out;const QPointer<CoreSocialService> weak(this);
 out.read=[weak](ReadRequest request,Services::Reply done){if(!weak){done({},"Social connection closed.");return;}auto& p=*weak->impl_;if(request.identity!=p.context.identity||request.limit<1||request.limit>100){done({},"Social identity or page mismatch.");return;}if(request.section==Section::Miners){if(weak->leaderboard)weak->leaderboard(request,std::move(done));else done({},"Authenticated per-miner counters are not available yet.");return;}if(request.section!=Section::Chat){done({},"This section is deferred.");return;}if(!p.ready){done({},p.error.isEmpty()?QString("Connecting to the native Social relay…"):p.error);return;}done(p.snapshot(request),{});};
 out.sendChat=[weak](SendRequest request,std::function<void(SendResult)> done){if(weak)weak->impl_->send(std::move(request),std::move(done));else done({request.identity,request.channel,request.requestId,SendOutcome::Uncertain,{},"Social connection closed; draft preserved."});};return out;
}
} // namespace pgn::social

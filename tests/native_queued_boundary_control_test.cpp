#include "mining/native_queued_boundary_control.h"
#include "mining/staged_successor.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <sqlite3.h>
#include <cstdio>
#include <iostream>
#include <stdexcept>

// CLOSED QtCore SELF executable + owned temporary SQLite only. All process,
// SOURCE, conservation and registry inputs are synthetic TESTONLY. No SSH,
// PowerShell, installed engine, UI, GPU/driver or user database is accessed.
using namespace pgn::mining;using namespace pgn::booking;
namespace {
int checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",Root(32,'a'),Next(32,'b');
const QString First="199900000000000000000",Width="1000000000000000",Birth="2026-10-15T00:00:00.1234567Z";
QString id(int n){return QString("00000000-0000-4000-8000-%1").arg(n,12,16,QChar('0'));}
QString plus(int n){return addUnsigned(First,QString::number(quint64(n)*1000000000000000ULL));}
QByteArray json(const QJsonObject& o){return QJsonDocument(o).toJson(QJsonDocument::Compact);}
void write(const QString& file,const QByteArray& bytes){QFile f(file);need(f.open(QIODevice::WriteOnly),"own fixture write");need(f.write(bytes)==bytes.size(),"own fixture full write");}
class Sql {
public:
 sqlite3* db{};
 explicit Sql(const QString& file){need(sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"own fixture DB open");}
 ~Sql(){if(db)sqlite3_close(db);}
 void exec(const char* sql,const QStringList& values={}){sqlite3_stmt* q{};need(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK,"own fixture prepare");for(int i=0;i<values.size();++i){auto s=values[i].toUtf8();sqlite3_bind_text(q,i+1,s.constData(),s.size(),SQLITE_TRANSIENT);}const int rc=sqlite3_step(q);sqlite3_finalize(q);need(rc==SQLITE_DONE,"own fixture mutation");}
 QString value(const char* sql,const QString& parameter={}){sqlite3_stmt* q{};need(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK,"own fixture query");if(!parameter.isNull()){auto b=parameter.toUtf8();sqlite3_bind_text(q,1,b.constData(),b.size(),SQLITE_TRANSIENT);}QString out;if(sqlite3_step(q)==SQLITE_ROW)out=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(q,0)),sqlite3_column_bytes(q,0));sqlite3_finalize(q);return out;}
 QByteArray dump(){sqlite3_stmt* tables{};need(sqlite3_prepare_v2(db,"SELECT name FROM sqlite_master WHERE type='table' ORDER BY name",-1,&tables,nullptr)==SQLITE_OK,"own fixture tables");QByteArray out;while(sqlite3_step(tables)==SQLITE_ROW){auto name=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(tables,0)));sqlite3_stmt* q{};auto sql=("SELECT * FROM \""+name+"\" ORDER BY rowid").toUtf8();need(sqlite3_prepare_v2(db,sql.constData(),-1,&q,nullptr)==SQLITE_OK,"own fixture rows");out+=name.toUtf8();while(sqlite3_step(q)==SQLITE_ROW){for(int c=0;c<sqlite3_column_count(q);++c){out+=QByteArray::number(sqlite3_column_type(q,c))+':';out+=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(q,c)),sqlite3_column_bytes(q,c)).toHex()+';';}out+='\n';}sqlite3_finalize(q);}sqlite3_finalize(tables);return out;}
};
SourceRegistry registry(){SourceRegistry r;r.url="https://PUBLIC-TESTONLY.invalid/source";r.sha256=QString(64,'1');r.observedUtc=QDateTime::fromString("2026-10-15T00:00:00Z",Qt::ISODate);r.ranges={{1001,First,plus(100),"Open",{}}};return r;}
InitialStartScope scope(){return {First,plus(100),First,plus(100),First,Width};}
QJsonObject manifest(const QString& run,const QString& booking,const QString& a,const QString& z){return {{"schema","pgn-native-process-1"},{"run",run},{"uuid",Device},{"pid",4242},{"birth",Birth},{"start",a},{"end",z},{"queued_intent",booking},{"stdout","C:/PUBLIC-TESTONLY/"+run+".stdout"}};}
SshObservationFrame completed(const QJsonObject& m){
 const auto a=m.value("start").toString(),z=m.value("end").toString();
 QJsonObject b{{"uuid",Device},{"start",a},{"end",z},{"length",subtractUnsigned(z,a)},{"queued_intent",m.value("queued_intent")},{"test_scope",QJsonObject{{"enabled",true},{"order","ascending"},{"start",First},{"end",plus(100)}}}};
 QJsonObject r{{"schema","pgn-source-range-completion-1"},{"run",m.value("run")},{"start",a},{"end",z},{"source_range_complete",true},{"source_positions",subtractUnsigned(z,a)},{"source_tests","2000000000000001"},{"source_session_gaps","17"},{"source_exit_code",0},{"source_exit_utc","2026-10-15T00:00:01Z"},{"source",m.value("stdout")},{"queued_intent",m.value("queued_intent")},{"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"},{"independent_coverage",false}};
 SshObservationFrame f;f.nextOffset=17;f.offset=0;f.pendingBytes=3;f.chunk="INVENTORY-BYTES-MUST-NOT-BE-CONSUMED";
 f.packet={{"schema","pgn-runtime-snapshot-1"},{"manifest",m},{"binding",b},{"owned",false},{"operator_stop",false},{"driver_valid",true},{"has_checkpoint",false},{"state","OFF"},{"observed_utc","2026-10-15T00:00:02.1234567Z"},{"next_offset",17},{"pending_bytes",3},{"last_exit",QJsonObject{{"run",m.value("run")},{"exit_code",0},{"utc","2026-10-15T00:00:01Z"}}},{"completion_candidate",r}};
 // TESTONLY exact peer ABI, not an image or raw-reader attestation.
 f.packet["resume_lineage"]=QJsonObject{{"schema","pgn-source-resume-lineage-1"},{"ready",true},{"error",""},{"terminal","complete"},{"scope","LOCAL_RAW_LINEAGE_COUNTER_DELTAS_NOT_INDEPENDENT_PRIME_COVERAGE"},
  {"run",m.value("run")},{"booking_id",m.value("queued_intent")},{"start",a},{"end",z},{"epochs",1},{"chain_sha256",QString(64,'d')}};return f;
}
QJsonObject conservation(const QString& run){return {{"schema","pgn-source-conservation-1"},{"ready",true},{"run",run},{"source","native/"+run+".stdout"},{"required_end",20},{"committed_offset",20},{"mirror_size",20},{"transport_offset",20},{"scope","LOGICAL_SOURCE_EPOCH_CONSERVATION_NOT_GLOBAL_POWER_CUT_DURABILITY"}};}
void pump(){for(int i=0;i<32;++i)QCoreApplication::processEvents();}
void waitFor(const std::function<bool()>& predicate){QElapsedTimer t;t.start();while(!predicate()&&t.elapsed()<8000){QCoreApplication::processEvents();QThread::msleep(1);}check(predicate(),"bounded closed wait");}
// This exact SELF body reads real native committed SQLite before replying.
// Its reply is still only a synthetic request report, NEVER a worker launch.
int fake(const QStringList& args){try{
 auto key=args.value(args.indexOf("-i")+1);QFile f(key+".fixture.json");need(f.open(QIODevice::ReadOnly),"SELF fixture");auto context=QJsonDocument::fromJson(f.readAll()).object();
 const auto raw=QByteArray::fromBase64(args.last().mid(args.last().lastIndexOf(' ')+1).toLatin1());need(raw.size()%2==0,"SELF UTF16");QString script;for(int i=0;i<raw.size();i+=2)script+=QChar(quint8(raw[i])|(quint16(quint8(raw[i+1]))<<8));
 const auto param=[&](const char* name){auto m=QRegularExpression("-"+QString::fromLatin1(name)+" '([^']*)'").match(script);need(m.hasMatch(),"SELF exact advance param");return m.captured(1);};
 need(script.contains("-Action advance -Offset 0"),"SELF real advance command");const auto old=param("ExpectedRun"),booking=param("QueueId"),a=param("QueueStart"),z=param("QueueEnd");
 Sql db(context.value("database").toString());
 need(db.value("SELECT state FROM dispatch_requests WHERE run=?",old)=="SOURCE_FINISHED","SELF predecessor SOURCE committed before send");
 need(db.value("SELECT state||':'||coalesce(run,'NULL')||':'||predecessor FROM dispatch_requests WHERE id=?",booking)=="PREPARED:NULL:"+old,"SELF next committed PREPARED, ACK not STARTED");
 need(db.value("SELECT state||':'||start||':'||end FROM bookings WHERE id=?",booking)=="DISPATCHING:"+a+':'+z,"SELF exact local tuple");
 QFile calls(key+".calls");need(calls.open(QIODevice::Append),"SELF calls");calls.write("1\n");calls.close();
 const auto mode=args.value(args.size()-2);if(mode=="slow")QThread::msleep(2000);
 auto receipt=context.value("receipt").toObject();need(receipt.value("run")==old,"SELF source epoch");
 QJsonObject reply{{"status",mode=="held"?"SUCCESSOR_HELD":"SUCCESSOR_START_REQUESTED"},{"launch",QJsonObject{{"status",mode=="held"?"OPERATOR_STOP_HELD":"START_REQUESTED"}}},{"completion",receipt},{"predecessor_binding",context.value("binding")},{"binding",QJsonObject{{"uuid",Device},{"start",a},{"end",z},{"queued_intent",mode=="bad"?id(9999):booking}}},{"community_csv",QString::fromLatin1(QByteArray("PUBLIC TESTONLY\n").toBase64())}};
 auto bytes=json(reply);std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);std::fflush(stdout);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"CLOSED SELF failure: %s",e.what());return 9;}}
struct Fixture {
 QTemporaryDir temp;QString file,key,oldBooking;BookingStore store;Sql db;NativeApplicationWriter writer;SshObservationEndpoint endpoint;
 SshObservationFrame frame;QJsonObject conserved;QList<QJsonObject> packets;std::function<void(const QJsonObject&)> hook;
 Fixture():file(temp.filePath("native.sqlite")),key(temp.filePath("PUBLIC-NOT-A-KEY.txt")),store(file),db(file){
  check(temp.isValid()&&store.valid(),"owned temp SAME real store");write(key,"PUBLIC CLOSED TEST ONLY");endpoint={QCoreApplication::applicationFilePath(),key,"ok","fixture",22};
  check(store.queue(registry(),plus(33),plus(34),Device).isEmpty(),"old actual local reservation");auto p=store.prepareInitialStart(id(1),Device,scope(),registry(),{WorkBoundary::Ready,id(2)});check(p.status==InitialStartStatus::Prepared&&p.dispatch.has_value(),"old real native initial preparation");oldBooking=p.dispatch->bookingId;
  db.exec("UPDATE dispatch_requests SET state='STARTED',run=? WHERE id=?",{Root,oldBooking});db.exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{oldBooking});
  check(store.observeRuntime(Root,plus(33),plus(34),Device,false).isEmpty(),"fixture previous real PAUSED runtime");const auto m=manifest(Root,oldBooking,plus(33),plus(34));check(store.saveSourceRunManifest(Device,m).isEmpty(),"fixture actual immutable manifest");frame=completed(m);conserved=conservation(Root);
 }
 std::unique_ptr<NativeQueuedBoundaryControl> controller(int age=30000,NativeControlTimeouts t={5000,10000}){auto c=std::make_unique<NativeQueuedBoundaryControl>(writer,store,endpoint,t,age);c->setDesiredPaused(false);c->onPacket=[this](auto p){packets.append(p);if(hook)hook(p);};return c;}
 bool submit(NativeQueuedBoundaryControl& c,int n=10,const InitialStartScope& s=scope(),const SourceRegistry& r=registry()){
  auto receipt=frame.packet.value("completion_candidate");if(!receipt.isObject())receipt=frame.packet.value("source_completion_observation");
  auto predecessorBinding=frame.packet.value("binding").toObject();
  if(stagedSuccessor(frame.packet)){const auto m=frame.packet.value("manifest").toObject();for(const auto* field:{"start","end","queued_intent"})predecessorBinding[QLatin1String(field)]=m.value(QLatin1String(field));}
  write(key+".fixture.json",json({{"database",file},{"receipt",receipt},{"binding",predecessorBinding}}));return c.submit(id(n),id(n+1),frame,conserved,s,r);
 }
 int calls(){QFile f(key+".calls");return f.open(QIODevice::ReadOnly)?f.readAll().count('\n'):0;}
 int boundaries()const{int n=0;for(const auto& p:packets)if(p.value("type")=="queued_boundary")++n;return n;}
 QJsonObject last()const{for(auto i=packets.crbegin();i!=packets.crend();++i)if(i->value("type")=="queued_boundary")return *i;return {};}
 void settled(int n=1){waitFor([&]{return boundaries()>=n;});}
 QString queue(int a,int z){check(store.queue(registry(),plus(a),plus(z),Device).isEmpty(),"queued manual intent");return db.value("SELECT id FROM bookings WHERE start=? AND state='QUEUED'",plus(a));}
 QString sourceState(){return db.value("SELECT state FROM dispatch_requests WHERE id=?",oldBooking);}
 QString prepared(){return db.value("SELECT id FROM dispatch_requests WHERE state='PREPARED'");}
 void physicalComplete(const QString& booking){
  const auto a=db.value("SELECT start FROM dispatch_requests WHERE id=?",booking),z=db.value("SELECT end FROM dispatch_requests WHERE id=?",booking);auto m=manifest(Next,booking,a,z);auto f=completed(m);auto b=f.packet.value("binding").toObject();b["predecessor"]=QJsonObject{{"run",Root},{"start",plus(33)},{"end",plus(34)}};b["initial_start"]=QJsonObject{{"origin","INITIAL_START"},{"request_id",id(1)}};f.packet["binding"]=b;
  auto actual=f.packet;actual["owned"]=true;actual["state"]="RUNNING";
  check(store.reconcileObservedDispatch(actual).status==ObservedDispatchReconcileStatus::Reconciled,"separate actual-owned reconciles STARTED, not request ACK");frame=f;conserved=conservation(Next);
 }
 void resumed(){
  StoredSourceResume cp;cp.checkpointBytes="PGNCG001";const auto a=plus(33),z=plus(34);
  const quint64 low=subtractUnsigned(a,"184467440737095516160").toULongLong();
  for(const quint64 word:{low,quint64(10),Width.toULongLong(),quint64(123),quint64(2000000000000001ULL),quint64(17),quint64(3)})for(int i=0;i<8;++i)cp.checkpointBytes.append(char((word>>(8*i))&255));
  auto& r=cp.resume;r.run=Root;r.start=a;r.end=z;r.length=Width;r.positions="123";r.nextStart=addUnsigned(a,"123");r.remaining=subtractUnsigned(Width,"123");r.tests="2000000000000001";r.gaps="17";r.batches="3";r.counterTimeNs="777";r.checkpointLabel="C:/PUBLIC-TESTONLY/checkpoint";r.checkpointSha256=QString::fromLatin1(QCryptographicHash::hash(cp.checkpointBytes,QCryptographicHash::Sha256).toHex());
  check(store.saveResumeCheckpoint(Device,r,cp.checkpointBytes).isEmpty(),"real native saved checkpoint tuple");
  check(store.prepareCheckpointResume(id(100),Device,oldBooking,Root,Next,r.checkpointSha256,id(101)).status==CheckpointResumeAdmissionStatus::Prepared,"real native resumed epoch");
  auto m=manifest(Next,oldBooking,a,z);m["resume_previous_run"]=Root;m["checkpoint_input_sha256"]=r.checkpointSha256;auto f=completed(m);auto b=f.packet["binding"].toObject();b["resume_start"]=QJsonObject{{"schema","pgn-checkpoint-resume-marker-1"},{"request_id",id(100)},{"observation_id",id(101)},{"run",Next},{"previous_run",Root},{"checkpoint_sha256",r.checkpointSha256}};f.packet["binding"]=b;
  auto actual=f.packet;actual["owned"]=true;actual["state"]="RUNNING";const auto reconciled=store.reconcileObservedDispatch(actual);if(reconciled.status!=ObservedDispatchReconcileStatus::Reconciled)std::cerr<<"TESTONLY resumed setup: "<<reconciled.error.toStdString()<<'\n';check(reconciled.status==ObservedDispatchReconcileStatus::Reconciled,"actual resumed identity establishes physical SQLite tuple separately");
  // Synthetic peer-verifier ABI only. Real RAW-byte producer verification is
  // the acquisition caller's obligation and is NOT paid by this fixture DTO.
  f.packet["resume_lineage"]=QJsonObject{{"schema","pgn-source-resume-lineage-1"},{"ready",true},{"error",""},{"terminal","complete"},{"scope","LOCAL_RAW_LINEAGE_COUNTER_DELTAS_NOT_INDEPENDENT_PRIME_COVERAGE"},{"run",Next},{"booking_id",oldBooking},{"start",a},{"end",z},{"epochs",2},{"chain_sha256",QString(64,'a')}};
  frame=f;conserved=conservation(Next);
 }
};
void primary(){
 Fixture f;check(f.store.setAutomaticAfterManual(Device,true).isEmpty(),"explicit preference ON");auto c=f.controller();const auto original=json(f.frame.packet);const auto chunk=f.frame.chunk;
 check(f.submit(*c),"fresh final callback submits source+next");f.settled();auto p=f.last();check(p.value("accepted")==true&&p.value("applied")==false&&p.value("status")=="RequestReported"&&f.calls()==1,"SELF request report only");
 const auto selected=f.prepared();check(!selected.isEmpty()&&f.db.value("SELECT start FROM dispatch_requests WHERE id=?",selected)==First,"JIT first entire free block, not old B33 binding");
 check(f.db.value("SELECT coalesce(run,'NULL') FROM dispatch_requests WHERE id=?",selected)=="NULL"&&f.sourceState()=="SOURCE_FINISHED","ACK leaves next PREPARED and predecessor completed");
 check(f.store.miningSessionMetadata(Device)->mode==MiningSessionMode::ManualReservations,"automatic selection preserves Manual provenance");check(json(f.frame.packet)==original&&f.frame.chunk==chunk,"inventory snapshot/chunk unchanged");
 check(!f.submit(*c),"request and nonce one-use");check(!c->submit(id(12),id(11),f.frame,f.conserved,scope(),registry()),"nonce cannot be rebound");check(f.submit(*c,14),"fresh callback may reconcile old boundary but cannot retry");f.settled(4);check(f.calls()==1&&f.last().value("accepted")==false&&f.prepared()==selected,"same predecessor cannot relaunch or allocate");
 f.physicalComplete(selected);check(f.store.setAutomaticAfterManual(Device,false).isEmpty(),"preference OFF after auto selection");check(f.submit(*c,20),"next actual completed run admitted");f.settled(5);
 check(f.last().value("status")=="EndSession"&&f.last().value("accepted")==true&&f.last().value("applied")==false&&f.calls()==1&&f.prepared().isEmpty(),"Manual ON -> A -> OFF -> EndSession without new claim/send");check(f.store.miningSessionMetadata(Device)->mode==MiningSessionMode::ManualReservations,"drain never resets mode to Auto");
}
void orderedAndPending(){
 Fixture f;const auto later=f.queue(8,9),first=f.queue(5,6);auto c=f.controller();check(f.submit(*c),"manual submitted");f.settled();check(f.prepared()==first&&f.db.value("SELECT state FROM bookings WHERE id=?",later)=="QUEUED","numeric first manual, no lookahead");
 c.reset();auto next=f.controller();const auto before=f.db.dump();check(f.submit(*next,30),"restart can inspect same PREPARED");f.settled(2);check(f.last().value("error").toString().contains("reconciliation")&&f.calls()==1&&f.db.dump()==before,"ReconcilePrepared is Held, no mutation or automatic retry");
}
void stagedRecovery(){
 for(int poison=0;poison<7;++poison){
  Fixture f;f.queue(8,9);auto first=f.controller();check(f.submit(*first),"prepare before staged worker failure");f.settled();const auto selected=f.prepared();first.reset();
  auto b=f.frame.packet.value("binding").toObject();const auto m=f.frame.packet.value("manifest").toObject();
  b["predecessor"]=QJsonObject{{"run",m.value("run")},{"start",m.value("start")},{"end",m.value("end")}};
  b["start"]=f.db.value("SELECT start FROM dispatch_requests WHERE id=?",selected);b["end"]=f.db.value("SELECT end FROM dispatch_requests WHERE id=?",selected);b["queued_intent"]=selected;
  if(poison==1)b["queued_intent"]=id(9999);
  if(poison==2)b["end"]=plus(10);
  if(poison==3){auto p=b["predecessor"].toObject();p["run"]=Next;b["predecessor"]=p;}
  if(poison==4)f.frame.packet["owned"]=true;
  if(poison==5)f.frame.packet["operator_stop"]=true;
  if(poison==6)f.db.exec("UPDATE dispatch_requests SET state='STARTED',run=? WHERE id=?",{Next,selected});
  f.frame.packet["binding"]=b;const auto before=f.db.dump();auto recovery=f.controller();const bool submitted=f.submit(*recovery,200);
  if(submitted)f.settled(2);
  if(!poison){if(f.last().value("accepted")!=true||f.db.dump()!=before)std::cerr<<json(f.last()).constData()<<" calls="<<f.calls()<<" same="<<(f.db.dump()==before)<<'\n';check(f.last().value("accepted")==true&&f.calls()==2&&f.prepared()==selected&&f.db.dump()==before,"exact staged existing dispatch reconciles once; no new reservation or ACK-as-owner");}
  else check(f.last().value("accepted")==false&&f.calls()==1&&f.db.dump()==before,"staged wrong id/range/predecessor/ownership/STOP/started dispatch cannot launch or allocate");
 }
}
void latches(){
 for(int mode=0;mode<5;++mode){Fixture f;f.queue(8,9);auto c=f.controller();
  if(mode==0)c->setDesiredPaused(true);
  if(mode==1)check(f.writer.prepareClose(id(100),[](auto){}),"synchronous close freeze");
  if(mode==2)f.hook=[&](const auto& p){if(p.value("type")=="source_completion")c->setDesiredPaused(true);};
  if(mode==3)f.hook=[&](const auto& p){if(p.value("type")=="source_completion")check(f.writer.prepareClose(id(100),[](auto){}),"close between source and next");};
  if(mode==4){f.frame.packet["operator_stop"]=true;auto r=f.frame.packet.take("completion_candidate").toObject();r["evidence_scope"]="VERIFIED_SNAPSHOT_SOURCE_COMPLETION";r["source_observed_operator_stop"]=true;r["source_observed_stop_file"]=true;f.frame.packet["source_completion_observation"]=r;}
  check(f.submit(*c),"fact remains committable under pause/freeze/STOP");f.settled();check(f.sourceState()=="SOURCE_FINISHED"&&f.prepared().isEmpty()&&f.calls()==0&&f.last().value("accepted")==false,"latch preserves SOURCE and refuses all future launch/claim");
 }
 {Fixture f;f.queue(8,9);auto c=f.controller();NativeApplicationWriter::Done release;check(f.writer.enqueue(NativeApplicationOperation::Read,[&](auto done){release=done;},[](auto){}),"own writer gate");pump();check(bool(release),"gate entered");check(f.submit(*c),"boundary queued behind actual writer");c->setDesiredPaused(true);c->setDesiredPaused(false);release({});f.settled();check(f.calls()==0&&f.sourceState()=="SOURCE_FINISHED"&&f.prepared().isEmpty(),"pause generation cannot be cleared into old queued admission");}
 {Fixture f;f.queue(8,9);auto c=f.controller(5);NativeApplicationWriter::Done release;check(f.writer.enqueue(NativeApplicationOperation::Read,[&](auto done){release=done;},[](auto){}),"TTL writer gate");pump();check(f.submit(*c),"TTL queued callback");QThread::msleep(15);release({});f.settled();check(f.calls()==0&&f.sourceState()=="SOURCE_FINISHED"&&f.prepared().isEmpty()&&f.last().value("error").toString().contains("monotonic observation expired"),"preentry TTL preserves SOURCE but refuses next mutation");}
 {Fixture f;f.queue(8,9);auto c=f.controller();f.hook=[&](const auto& p){if(p.value("type")=="source_completion")c.reset();};check(f.submit(*c),"owner lifetime source submit");waitFor([&]{return !c;});pump();check(f.sourceState()=="SOURCE_FINISHED"&&f.prepared().isEmpty()&&f.calls()==0,"deletion in SOURCE callback releases writer and never claims");bool barrier=false;check(f.writer.prepareClose(id(100),[&](auto r){barrier=r.entered&&r.error.isEmpty();}),"close after controller deletion");waitFor([&]{return barrier;});}
 {Fixture f;f.queue(8,9);auto c=f.controller();f.hook=[](const auto& p){if(p.value("type")=="source_completion")throw std::runtime_error("TESTONLY notification failure");};check(f.submit(*c),"throwing presentation callback submit");f.settled();check(f.sourceState()=="SOURCE_FINISHED"&&f.prepared().isEmpty()&&f.calls()==0&&f.last().value("error").toString().contains("notification failed"),"presentation failure cannot strand admission or launch next");}
 {Fixture f;f.queue(8,9);auto c=f.controller();NativeApplicationWriter::Done release;check(f.writer.enqueue(NativeApplicationOperation::Read,[&](auto done){release=done;},[](auto){}),"lifetime writer gate");pump();check(f.submit(*c),"queued owner admission");c.reset();release({});bool barrier=false;check(f.writer.prepareClose(id(100),[&](auto r){barrier=r.entered&&r.error.isEmpty();}),"barrier after queued owner destroyed");waitFor([&]{return barrier;});check(f.sourceState()=="STARTED"&&f.prepared().isEmpty()&&f.calls()==0&&f.packets.isEmpty(),"late queued callback has no dangling owner, mutation or publication");}
}
void validation(){
 using Poison=std::function<void(Fixture&)>;const QList<Poison> poisons={
  [](auto& f){f.frame.packet.remove("owned");},[](auto& f){f.frame.packet["owned"]="false";},[](auto& f){f.frame.packet["owned"]=0;},[](auto& f){f.frame.packet["operator_stop"]=QJsonValue::Null;},[](auto& f){f.frame.packet.remove("has_checkpoint");},[](auto& f){f.frame.packet["has_checkpoint"]=true;},
  [](auto& f){f.conserved["required_end"]=19;},[](auto& f){f.conserved["committed_offset"]=19;},[](auto& f){f.conserved["mirror_size"]=QJsonValue::Null;},[](auto& f){f.conserved["transport_offset"]="20";},[](auto& f){f.conserved["run"]=Next;},[](auto& f){f.frame.pendingBytes.reset();},[](auto& f){f.frame.nextOffset=16;},
  [](auto& f){f.frame.packet["observed_utc"]="2026-10-15T00:00:02Z\n";},[](auto& f){auto m=f.frame.packet["manifest"].toObject();m["pid"]=0;f.frame.packet["manifest"]=m;},
  [](auto& f){auto e=f.frame.packet["last_exit"].toObject();e["run"]=Next;f.frame.packet["last_exit"]=e;},[](auto& f){auto r=f.frame.packet["completion_candidate"].toObject();r["source_positions"]="500";f.frame.packet["completion_candidate"]=r;},
  [](auto& f){auto r=f.frame.packet["completion_candidate"].toObject();r["independent_coverage"]=true;f.frame.packet["completion_candidate"]=r;},[](auto& f){auto r=f.frame.packet["completion_candidate"].toObject();r["queued_intent"]=id(999);f.frame.packet["completion_candidate"]=r;},
  [](auto& f){auto r=f.frame.packet["completion_candidate"].toObject();r["source_tests"]="1";f.frame.packet["completion"]=r;},[](auto& f){f.frame.packet["completion"]=QJsonObject{{"run","bad"}};}
 };
 for(const auto& poison:poisons){Fixture f;auto c=f.controller();const auto before=f.db.dump();poison(f);check(!f.submit(*c),"malformed observation held synchronously");pump();check(f.calls()==0&&f.db.dump()==before,"unknown/corrupt proof makes no local mutation/launch");}
 {Fixture f;auto c=f.controller();auto old=f.frame.packet["completion_candidate"].toObject();old["run"]=Next;old["source_tests"]="0";f.frame.packet["completion"]=old;check(f.submit(*c),"valid foreign old completion ignored unmodified");f.settled();check(f.last().value("status")=="EndSession"&&f.frame.packet["completion"]==old,"historical receipt retained and not relabelled");}
 {Fixture f;auto c=f.controller();auto m=f.frame.packet["manifest"].toObject();m["stdout"]="C:/PUBLIC-TESTONLY/changed.stdout";f.frame.packet["manifest"]=m;auto r=f.frame.packet["completion_candidate"].toObject();r["source"]=m["stdout"];f.frame.packet["completion_candidate"]=r;const auto before=f.db.dump();check(f.submit(*c),"manifest consistency checked inside writer");f.settled();check(f.db.dump()==before&&f.calls()==0&&f.sourceState()=="STARTED","actual immutable manifest mismatch Held");}
}
void failures(){
 {Fixture f;f.queue(8,9);auto c=f.controller();auto invalid=registry();invalid.ranges[0].end=plus(99);check(f.submit(*c,10,scope(),invalid),"registry validation separate from source commit");f.settled();check(f.sourceState()=="SOURCE_FINISHED"&&f.prepared().isEmpty()&&f.calls()==0,"invalid next registry does not discard SOURCE");check(f.submit(*c,20),"new actual proof can reopen nonmutating registry Held");f.settled(2);check(f.calls()==1&&!f.prepared().isEmpty(),"Held did not consume predecessor before real PREPARED");}
 {Fixture f;f.queue(8,9);auto c=f.controller();f.db.exec("CREATE TRIGGER fixture_fail BEFORE INSERT ON dispatch_requests WHEN NEW.state='PREPARED' BEGIN SELECT RAISE(ABORT,'TESTONLY next failure'); END");check(f.submit(*c),"next trigger failure attempt");f.settled();check(f.sourceState()=="SOURCE_FINISHED"&&f.prepared().isEmpty()&&f.calls()==0,"next rollback preserves prior SOURCE transaction");f.db.exec("DROP TRIGGER fixture_fail");check(f.submit(*c,20),"new proof after no PREPARED may reopen");f.settled(2);check(f.calls()==1,"retry after repaired nonmutating next Held only");}
 {Fixture f;auto c=f.controller();f.db.exec("CREATE TRIGGER fixture_fail BEFORE UPDATE OF state ON bookings WHEN NEW.state='SOURCE_FINISHED' BEGIN SELECT RAISE(ABORT,'TESTONLY SOURCE failure'); END");const auto before=f.db.dump();check(f.submit(*c),"source commit failure attempt");f.settled();check(f.db.dump()==before&&f.packets.size()==1&&f.calls()==0&&f.sourceState()=="STARTED","SOURCE failure rolls back all and publishes no completion");}
 {Fixture f;f.queue(8,9);auto c=f.controller(200);f.db.exec("CREATE TRIGGER fixture_sql_cost BEFORE INSERT ON dispatch_requests WHEN NEW.state='PREPARED' BEGIN SELECT sum(x) FROM (WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<2000000) SELECT x FROM n); END");check(f.submit(*c),"real temporary SQL cost attempt");f.settled();if(f.prepared().isEmpty())std::cerr<<json(f.last()).constData()<<'\n';check(f.sourceState()=="SOURCE_FINISHED"&&!f.prepared().isEmpty()&&f.calls()==0&&f.last().value("error").toString().contains("expired during next SQL/readback/encode"),"postSQL TTL check leaves PREPARED unsent; no lifetime renewal");}
 for(const auto& mode:QStringList{"held","bad","slow"}){Fixture f;f.queue(8,9);f.endpoint.host=mode;auto c=f.controller(30000,mode=="slow"?NativeControlTimeouts{1000,1200}:NativeControlTimeouts{5000,10000});check(f.submit(*c),"SELF uncertain/held attempt");f.settled();check(f.last().value("accepted")==false&&!f.prepared().isEmpty()&&f.last().value("applied")==false,"remote uncertainty never erases PREPARED or proves applied");const auto calls=f.calls();check(f.submit(*c,20),"new fresh proof after uncertain enters reconciliation only");f.settled(2);check(f.calls()==calls&&f.last().value("accepted")==false,"no automatic retry on new nonce after uncertain");}
}
void unsentPreparedRecovery(){
 Fixture f;f.queue(8,9);auto c=f.controller(200);
 f.db.exec("CREATE TRIGGER fixture_sql_cost BEFORE INSERT ON dispatch_requests WHEN NEW.state='PREPARED' BEGIN SELECT sum(x) FROM (WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<2000000) SELECT x FROM n); END");
 check(f.submit(*c),"first fresh acquisition pays actual SQL cost");f.settled();
 const auto selected=f.prepared();const auto before=f.db.dump();
 check(!selected.isEmpty()&&f.calls()==0&&f.last().value("reacquire_prepared_unsent")==true&&f.last().value("transport_attempted")==false,"only provably unsent postSQL expiry requests a fresh read");
 check(f.submit(*c,30),"new physical acquisition can reconcile owner-local unsent preparation");f.settled(2);
 check(f.last().value("accepted")==true&&f.calls()==1&&f.prepared()==selected&&f.db.dump()==before,"fresh observation sends SAME saved tuple once without repeating INSERT or allocating");
 check(f.submit(*c,40),"later callback still enters ordinary consumed guard");f.settled(3);
 check(f.calls()==1&&f.last().value("accepted")==false&&f.db.dump()==before,"unsent capability consumed before transport, no automatic resend");
}
void lineage(){
  for(int poison=0;poison<3;++poison){Fixture f;auto c=f.controller();const auto before=f.db.dump();auto proof=f.frame.packet["resume_lineage"].toObject();
   if(poison==0)f.frame.packet.remove("resume_lineage");
   if(poison==1){proof["ready"]=false;proof["error"]="v2 terminal Held";proof["source_boundary_protocol"]="pgn-source-boundary-v2";proof["source_boundary_groups"]="1";f.frame.packet["resume_lineage"]=proof;
    auto binding=f.frame.packet["binding"].toObject();binding["executable"]="C:/PUBLIC-TESTONLY/A73.exe";binding["pins"]=QJsonArray{QJsonObject{{"path","C:/PUBLIC-TESTONLY/A73.exe"},{"sha256","A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43"}}};f.frame.packet["binding"]=binding;}
   if(poison==2){proof["run"]=Next;f.frame.packet["resume_lineage"]=proof;}
   check(!f.submit(*c),"ordinary completion missing/Held/foreign RAW lineage refused before writer");pump();
   check(f.db.dump()==before&&f.calls()==0&&f.sourceState()=="STARTED","nonresumed Held proof cannot finish SOURCE, reserve or launch despite full footer/binding");
  }
 for(int poison=0;poison<4;++poison){Fixture f;f.resumed();auto c=f.controller();
  if(poison==0){check(f.store.setAutomaticAfterManual(Device,true).isEmpty(),"resumed continuation preference ON");check(f.submit(*c),"peer lineage ABI plus actual native resume chain");f.settled();check(f.calls()==1&&f.sourceState()=="SOURCE_FINISHED"&&f.db.value("SELECT state FROM bookings WHERE kind='RUNTIME' AND id=?",Root)=="PAUSED","covered old PAUSED kept historically while descendant completes/continues");}
  else{const auto before=f.db.dump();auto proof=f.frame.packet["resume_lineage"].toObject();if(poison==1)f.frame.packet.remove("resume_lineage");if(poison==2){proof["ready"]="true";f.frame.packet["resume_lineage"]=proof;}if(poison==3){proof["booking_id"]=id(999);f.frame.packet["resume_lineage"]=proof;}check(!f.submit(*c),"missing/mistyped/foreign required resumed lineage held");pump();check(f.db.dump()==before&&f.calls()==0,"metadata alone cannot bypass peer lineage contract");}
 }
}
void legacyScopeAbi(){
 // Exact four-field test_scope shape reported from the real controller67CC.
 // This fixture copies its ABI, not physical ownership or producer evidence.
 const QJsonObject observedScope{{"authority","ROOT379"},{"start","199900000000000000000"},{"end","200000000000000000000"},{"order","ascending"}};
 {Fixture f;const auto manual=f.queue(8,9);auto c=f.controller();auto b=f.frame.packet["binding"].toObject();b["test_scope"]=observedScope;f.frame.packet["binding"]=b;const auto original=json(f.frame.packet);check(f.submit(*c),"actual legacy scope without enabled admitted");f.settled();check(f.calls()==1&&f.prepared()==manual&&f.last().value("accepted")==true&&f.last().value("applied")==false&&json(f.frame.packet)==original,"absent enabled preserves scope ABI without modifying config/snapshot or ACK ownership");}
 for(const auto& enabled:QList<QJsonValue>{false,0,"true",QJsonValue(QJsonValue::Null)}){Fixture f;f.queue(8,9);auto c=f.controller();auto b=f.frame.packet["binding"].toObject();auto s=observedScope;s["enabled"]=enabled;b["test_scope"]=s;f.frame.packet["binding"]=b;check(f.submit(*c),"scope refusal must not erase separately valid SOURCE");f.settled();check(f.calls()==0&&f.prepared().isEmpty()&&f.sourceState()=="SOURCE_FINISHED"&&f.last().value("accepted")==false&&f.last().value("error").toString().contains("explicit ascending test scope unavailable"),"present false/malformed enabled is Held with no next claim/send");}
}
void csvLineIsNotZone(){
 for(int poison=0;poison<4;++poison){Fixture f;const auto manual=f.queue(8,9);auto c=f.controller();
  QByteArray csv="Start *1e15,End *1e15,Status,Owner\n";for(int n=0;n<999;++n)csv+=QByteArray::number(100000+100*n)+','+QByteArray::number(100100+100*n)+",Complete,\n";csv+="199900,200000,Open,\n";
  write(f.temp.filePath("community-source.csv"),csv);write(f.temp.filePath("binding.json"),json({{"community",QJsonObject{{"source","https://docs.google.com/spreadsheets/d/1y0OWkPbf8dvg6cpNqGNawhIykAlAWAtI8vqUA4KPiQ8/export?format=csv"},{"source_sha256",QString::fromLatin1(QCryptographicHash::hash(csv,QCryptographicHash::Sha256).toHex())},{"observed_utc","2026-10-15T00:00:00Z"}}}}));
  auto loaded=loadSourceRegistry(f.temp.path());check(loaded.valid()&&loaded.ranges.size()==1000&&loaded.ranges.last().row==1001&&loaded.ranges.last().start==First&&loaded.ranges.last().end==plus(100),"real CSV parser includes header in row1001 for display Zone1000");
  if(poison==1)loaded.ranges.append(loaded.ranges.last());if(poison==2)loaded.ranges.last().end=plus(99);if(poison==3){loaded.ranges.last().status="Reserved";loaded.ranges.last().owner="TESTONLY owner";}
  check(f.submit(*c,10,scope(),loaded),"loaded current registry boundary submitted");f.settled();
  if(!poison)check(f.calls()==1&&f.prepared()==manual&&f.last().value("accepted")==true,"unique exact Open bounds, not CSV ordinal, authorize existing ABI");
  else check(f.calls()==0&&f.prepared().isEmpty()&&f.sourceState()=="SOURCE_FINISHED"&&f.last().value("accepted")==false,"duplicate/different/notOpen scope range Held with no next claim");
 }
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(app.arguments().contains("-F"))return fake(app.arguments());try{primary();orderedAndPending();stagedRecovery();latches();validation();failures();unsentPreparedRecovery();lineage();legacyScopeAbi();csvLineIsNotZone();std::cout<<"NATIVE_QUEUED_BOUNDARY "<<checks<<" checks / 0 failures; CLOSED SELF+tempSQLite only, not physical or independent coverage\n";return 0;}catch(const std::exception& e){std::cerr<<"NATIVE_QUEUED_BOUNDARY failed check "<<checks<<": "<<e.what()<<'\n';return 1;}}

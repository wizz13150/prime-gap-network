#include "booking/booking_store.h"
#include "booking/next_boundary_admission.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>

// CLOSED: synthetic TESTONLY actual-manifest-shaped observations and owned
// temporary SQLite only. No process, transport, UI, remote or hardware access.
using namespace pgn::booking;
using namespace pgn::mining;
namespace {
int checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
const QString First="199900000000000000000",Width="1000000000000000",Birth="2026-10-15T00:00:00.1234567Z";
const QString Observation="12345678-1234-4567-89ab-123456789abc",Request="11111111-2222-4333-8444-555555555555",Root(32,'a');
QString plus(int i){return addUnsigned(First,QString::number(quint64(i)*1000000000000000ULL));}
QString key(const QString& s){return QString::number(s.size()).rightJustified(3,'0')+s;}
QString id(int i){return QString("00000000-0000-4000-8000-%1").arg(i,12,16,QChar('0'));}
QByteArray compact(const QJsonObject& o){return QJsonDocument(o).toJson(QJsonDocument::Compact);}
SourceRegistry registry(){SourceRegistry r;r.url="https://PUBLIC-TESTONLY.invalid/source";r.sha256=QString(64,'1');r.observedUtc=QDateTime::fromString("2026-10-15T00:00:00Z",Qt::ISODate);r.ranges={{1,First,plus(300),"Open",{}}};return r;}
InitialStartScope scope(){return {First,plus(300),First,plus(300),First,Width};}
class Sql {
public:
 sqlite3* db{};
 explicit Sql(const QString& file){need(sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"own fixture DB open");}
 ~Sql(){if(db)sqlite3_close(db);}
 void exec(const char* sql,const QStringList& values={}){sqlite3_stmt* q{};need(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK,"own fixture SQL prepare");for(int i=0;i<values.size();++i){const auto v=values[i].toUtf8();sqlite3_bind_text(q,i+1,v.constData(),v.size(),SQLITE_TRANSIENT);}const auto rc=sqlite3_step(q);sqlite3_finalize(q);need(rc==SQLITE_DONE,"own fixture SQL transition");}
 void script(const char* sql){need(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)==SQLITE_OK,"own fixture SQL script");}
 QByteArray value(const char* sql,const QString& parameter={}){sqlite3_stmt* q{};need(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK,"own fixture SQL read");if(!parameter.isNull()){const auto v=parameter.toUtf8();sqlite3_bind_text(q,1,v.constData(),v.size(),SQLITE_TRANSIENT);}QByteArray out;if(sqlite3_step(q)==SQLITE_ROW)out=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(q,0)),sqlite3_column_bytes(q,0));sqlite3_finalize(q);return out;}
 QByteArray dump(){sqlite3_stmt* tables{};need(sqlite3_prepare_v2(db,"SELECT name FROM sqlite_master WHERE type='table' ORDER BY name",-1,&tables,nullptr)==SQLITE_OK,"own snapshot tables");QByteArray out;while(sqlite3_step(tables)==SQLITE_ROW){const auto name=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(tables,0)));sqlite3_stmt* q{};const auto sql=("SELECT * FROM \""+name+"\" ORDER BY rowid").toUtf8();need(sqlite3_prepare_v2(db,sql.constData(),-1,&q,nullptr)==SQLITE_OK,"own snapshot rows");out+=name.toUtf8()+':';while(sqlite3_step(q)==SQLITE_ROW){for(int c=0;c<sqlite3_column_count(q);++c){out+=QByteArray::number(sqlite3_column_type(q,c))+':';out+=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(q,c)),sqlite3_column_bytes(q,c)).toHex()+';';}out+='\n';}sqlite3_finalize(q);}sqlite3_finalize(tables);return out;}
};
struct Fixture {
 QTemporaryDir temp;QString file;BookingStore store;Sql db;NextBoundarySource predecessor;QString oldBooking;
 Fixture():file(temp.filePath("native.sqlite")),store(file),db(file){
  check(temp.isValid()&&store.valid(),"real private native SQLite");
  check(store.queue(registry(),plus(33),plus(34),Device).isEmpty(),"old manual reservation");
  const auto old=store.prepareInitialStart(Request,Device,scope(),registry(),{WorkBoundary::Ready,Observation});
  check(old.status==InitialStartStatus::Prepared&&old.dispatch.has_value(),"old real initial admission");oldBooking=old.dispatch->bookingId;
  // Explicit setup, not an ACK test: the original completed physical run is
  // historical fixture state. New successor observations use production API.
  db.exec("UPDATE dispatch_requests SET state='STARTED',run=? WHERE id=?",{Root,oldBooking});db.exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{oldBooking});
  check(store.observeRuntime(Root,plus(33),plus(34),Device,false).isEmpty(),"old real physical PAUSED storage");
  auto m=manifest(Root,oldBooking,plus(33),plus(34));check(store.saveSourceRunManifest(Device,m).isEmpty(),"old immutable actual-shaped manifest");
  check(store.recordQueuedSourceCompletion(Root,plus(33),plus(34),Device,oldBooking).isEmpty(),"SOURCE/runtime/local/dispatch transaction precedes selection");
  predecessor={Root,Birth,plus(33),plus(34),oldBooking};db.exec("INSERT INTO booking_metadata VALUES('fixture/unrelated','KEEP')");
 }
 QJsonObject manifest(const QString& run,const QString& booking,const QString& start,const QString& end)const{return {{"schema","pgn-native-process-1"},{"run",run},{"uuid",Device},{"pid",4242},{"birth",Birth},{"start",start},{"end",end},{"queued_intent",booking},{"stdout","C:/PUBLIC-TESTONLY/"+run+".stdout"}};}
 QString queue(int a,int b){check(store.queue(registry(),plus(a),plus(b),Device).isEmpty(),"real exact manual reservation");return QString::fromUtf8(db.value("SELECT id FROM bookings WHERE start=? AND kind='LOCAL_INTENT' AND state='QUEUED'",plus(a)));}
 NextBoundaryAdmission prepare(){return store.prepareNextAtBoundary(Device,predecessor,scope(),registry(),{WorkBoundary::Ready,Observation});}
 QJsonObject snapshot(const InitialStartDispatch& d,const QString& run)const{
  const auto m=manifest(run,d.bookingId,d.start,d.end);
  // initial_start is genuinely inherited by current PS advance. It must NOT
  // be mistaken for this successor's admission/request marker.
  const QJsonObject binding{{"uuid",Device},{"start",d.start},{"end",d.end},{"length",subtractUnsigned(d.end,d.start)},{"queued_intent",d.bookingId},{"initial_start",QJsonObject{{"origin","INITIAL_START"},{"request_id",Request}}},{"predecessor",QJsonObject{{"run",predecessor.run},{"start",predecessor.start},{"end",predecessor.end}}}};
  return {{"schema","pgn-runtime-snapshot-1"},{"owned",true},{"operator_stop",false},{"driver_valid",true},{"state","RUNNING"},{"observed_utc","2026-10-15T00:00:01Z"},{"manifest",m},{"binding",binding}};
 }
 void finish(const InitialStartDispatch& d,const QString& run){
  const auto packet=snapshot(d,run);const auto r=store.reconcileObservedDispatch(packet);
  check(r.status==ObservedDispatchReconcileStatus::Reconciled&&r.run==run&&r.bookingId==d.bookingId,"owned actual-shaped snapshot atomically reconciles local/dispatch/runtime/manifest");
  const auto before=db.dump();check(store.reconcileObservedDispatch(packet).status==ObservedDispatchReconcileStatus::Unchanged&&db.dump()==before,"exact observed replay performs no writes or date changes");
  check(store.recordQueuedSourceCompletion(run,d.start,d.end,Device,d.bookingId).isEmpty(),"paid successor SOURCE transaction");predecessor={run,Birth,d.start,d.end,d.bookingId};
 }
};
void prepared(const NextBoundaryAdmission& a,bool automatic){check(a.status==NextBoundaryStatus::Prepared&&a.error.isEmpty()&&a.dispatch&&a.dispatch->state=="PREPARED"&&a.dispatch->origin=="QUEUED_ADVANCE"&&a.dispatch->automatic==automatic&&a.dispatch->predecessor&&!a.dispatch->run,"typed exact continuation selection, not initial start");}
void held(Fixture& f){const auto before=f.db.dump();const auto a=f.prepare();check(a.status==NextBoundaryStatus::Held&&!a.error.isEmpty()&&!a.dispatch&&f.db.dump()==before,"Held preparation rolls back ALL mutations, preserves SOURCE and unrelated data");}
void retiredAbortedHistory(){
 Fixture f;const QString retired(32,'f');
 check(f.store.recordSourceCompletion(retired,plus(0),plus(1),Device).isEmpty(),"Own retired physical fixture");
 f.db.exec("UPDATE bookings SET state='ABORTED' WHERE id=?",{retired});
 const auto selected=f.queue(8,9);const auto p=f.prepare();prepared(p,false);
 check(p.dispatch->bookingId==selected,"Old aborted runtime does not block next manual reservation");
 check(f.db.value("SELECT state FROM bookings WHERE id=?",retired)=="ABORTED","Continuation preserves aborted history without crediting coverage");
 Fixture local;const auto malformed=local.queue(8,9);local.db.exec("UPDATE bookings SET state='ABORTED' WHERE id=?",{malformed});held(local);
 Fixture corrupt;check(corrupt.store.recordSourceCompletion(retired,plus(0),plus(1),Device).isEmpty(),"Own poisoned aborted fixture");corrupt.db.exec("UPDATE bookings SET state='ABORTED',id='not-a-run' WHERE id=?",{retired});corrupt.queue(8,9);held(corrupt);
}
void manualOnOff(){
 Fixture f;const auto a=f.queue(8,9),b=f.queue(9,10);auto p=f.prepare();prepared(p,false);check(p.dispatch->bookingId==a&&p.mode==MiningSessionMode::ManualReservations,"numeric manual head first");
 auto before=f.db.dump();const auto pending=f.prepare();check(pending.status==NextBoundaryStatus::ReconcilePrepared&&pending.dispatch->bookingId==a&&f.db.dump()==before,"same predecessor pending idempotent, no second token/claim");
 auto read=f.store.savedPreparedNextAdmission(Device,a,f.predecessor);check(read.status==PreparedNextReadStatus::Present&&read.admission.dispatch->bookingId==a&&f.db.dump()==before,"same read snapshot returns exact prepared dispatch; no mutation/retry authority");
 f.finish(*p.dispatch,QString(32,'b'));p=f.prepare();prepared(p,false);check(p.dispatch->bookingId==b,"all manual reservations, not only one");f.finish(*p.dispatch,QString(32,'c'));
 before=f.db.dump();check(f.prepare().status==NextBoundaryStatus::EndSession&&f.db.dump()==before,"Manual OFF drains successfully without creating work or changing mode");
 check(f.store.setAutomaticAfterManual(Device,true).isEmpty(),"explicit ON preference");p=f.prepare();prepared(p,true);check(p.mode==MiningSessionMode::ManualReservations&&p.dispatch->start==First,"ON JIT automatic selection keeps Manual provenance, first entire free block");
 check(f.store.setAutomaticAfterManual(Device,false).isEmpty(),"explicit OFF after accepted preparation");before=f.db.dump();check(f.prepare().status==NextBoundaryStatus::ReconcilePrepared&&f.db.dump()==before,"OFF cannot rebind an already PREPARED transition");
 read=f.store.savedPreparedNextAdmission(Device,p.dispatch->bookingId,f.predecessor);check(read.status==PreparedNextReadStatus::Present&&read.admission.mode==MiningSessionMode::ManualReservations&&read.admission.dispatch->automatic==true&&f.db.dump()==before,"readback keeps session mode independent of selection kind and current preference");
 f.finish(*p.dispatch,QString(32,'d'));before=f.db.dump();check(f.prepare().status==NextBoundaryStatus::EndSession&&f.db.dump()==before&&f.store.miningSessionMetadata(Device)->mode==MiningSessionMode::ManualReservations,"Manual ON→Auto→OFF→SOURCE→EndSession, no second auto claim");
}
void zonesAndSplit(){
 {Fixture f;const auto low=f.queue(8,9),high=f.queue(108,109),highFirst=f.queue(101,102);check(f.store.reorderQueuedZones({{plus(100),plus(200),plus(100),plus(200)},{First,plus(100),First,plus(100)}},{low,high,highFirst}).isEmpty(),"native configured zone order");auto p=f.prepare();prepared(p,false);check(p.dispatch->bookingId==highFirst,"configured zone rank then numeric block rank");}
 {Fixture f;const auto parent=f.queue(8,11);const auto before=f.db.value("SELECT created_utc FROM bookings WHERE id=?",parent);auto p=f.prepare();prepared(p,false);check(p.dispatch->bookingId!=parent&&p.dispatch->start==plus(8)&&p.dispatch->end==plus(9),"finite capacity split claims exactly first prefix");check(f.db.value("SELECT state FROM bookings WHERE id=?",parent)=="RELEASED"&&f.db.value("SELECT created_utc FROM bookings WHERE id=?",parent)==before&&f.db.value("SELECT count(*) FROM booking_derivations")=="2","legacy reservation retained with unchanged age and two exact derived children");}
}
void legacyLookahead(){
 for(bool adopted:{false,true}){
  Fixture f;const auto legacy=f.queue(8,9);f.db.script("CREATE TABLE automatic_next(current_run TEXT PRIMARY KEY,booking_id TEXT UNIQUE NOT NULL,created_utc TEXT NOT NULL);CREATE TABLE automatic_adoptions(booking_id TEXT PRIMARY KEY,created_utc TEXT NOT NULL)");
  f.db.exec("INSERT INTO automatic_next VALUES(?,?,'TESTONLY-UNCHANGED')",{Root,legacy});f.db.exec("UPDATE bookings SET state='ARMED' WHERE id=?",{legacy});
  f.db.exec("INSERT INTO dispatch_requests VALUES(?,?,?,?,'ARMED',NULL,NULL,'TESTONLY-UNCHANGED')",{legacy,plus(8),plus(9),Device});if(adopted)f.db.exec("INSERT INTO automatic_adoptions VALUES(?,'TESTONLY-UNCHANGED')",{legacy});
  const auto age=f.db.value("SELECT created_utc FROM bookings WHERE id=?",legacy);const auto a=f.prepare();
  if(adopted){prepared(a,false);check(a.dispatch->bookingId==legacy,"human-adopted old automatic intent remains manual work");}
  else check(a.status==NextBoundaryStatus::EndSession&&f.db.value("SELECT state FROM bookings WHERE id=?",legacy)=="RELEASED"&&f.db.value("SELECT state FROM dispatch_requests WHERE id=?",legacy)=="CANCELLED","retire only unclaimed legacy automatic lookahead at boundary; never select future work early");
  check(f.db.value("SELECT created_utc FROM bookings WHERE id=?",legacy)==age&&f.db.value("SELECT created_utc FROM dispatch_requests WHERE id=?",legacy)=="TESTONLY-UNCHANGED","legacy record identities and original ages retained");
 }
 for(int poison=0;poison<5;++poison){Fixture f;const auto legacy=f.queue(8,9);f.db.script("CREATE TABLE automatic_next(current_run TEXT PRIMARY KEY,booking_id TEXT UNIQUE NOT NULL,created_utc TEXT NOT NULL)");f.db.exec("INSERT INTO automatic_next VALUES(?,?,'TESTONLY-UNCHANGED')",{Root,legacy});switch(poison){case 0:f.db.exec("UPDATE bookings SET device='GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265' WHERE id=?",{legacy});break;case 1:f.db.exec("UPDATE bookings SET kind='RUNTIME' WHERE id=?",{legacy});break;case 2:f.db.exec("UPDATE bookings SET start_key='corrupt' WHERE id=?",{legacy});break;case 3:f.db.exec("UPDATE automatic_next SET created_utc=CAST(created_utc AS BLOB)");break;case 4:f.db.exec("UPDATE automatic_next SET booking_id=CAST(booking_id AS BLOB)");break;}held(f);}
}
void readbackAndBoundaries(){
 for(const auto boundary:{WorkBoundary::Unknown,WorkBoundary::Running,WorkBoundary::Interrupted}){Fixture f;f.queue(8,9);const auto before=f.db.dump();const auto a=f.store.prepareNextAtBoundary(Device,f.predecessor,scope(),registry(),{boundary,Observation});check(a.status==NextBoundaryStatus::Held&&!a.dispatch&&f.db.dump()==before,"Unknown/Running/interrupted cannot prepare continuation");}
 {Fixture f;check(f.store.setMiningSessionMode(Device,MiningSessionMode::Automatic).isEmpty(),"explicit mode setup only");auto r=registry();r.ranges[0].status="Reserved";r.ranges[0].owner="TESTONLY_OTHER";const auto before=f.db.dump();const auto a=f.store.prepareNextAtBoundary(Device,f.predecessor,scope(),r,{WorkBoundary::Ready,Observation});check(a.status==NextBoundaryStatus::Exhausted&&!a.dispatch&&f.db.dump()==before,"no whole Open block is explicit exhaustion, no lookahead/claim/mutation");}
 for(int poison=0;poison<9;++poison){Fixture f;f.queue(8,9);const auto a=f.prepare();prepared(a,false);auto base=f.predecessor;auto booking=a.dispatch->bookingId;switch(poison){case 0:base.birth+="\n";break;case 1:base.run=QString(32,'f');break;case 2:booking=id(999);break;case 3:f.db.exec("UPDATE dispatch_requests SET run=? WHERE id=?",{QString(32,'b'),booking});break;case 4:f.db.exec("UPDATE dispatch_requests SET predecessor=? WHERE id=?",{QString(32,'f'),booking});break;case 5:f.db.exec("UPDATE bookings SET state='QUEUED' WHERE id=?",{booking});break;case 6:f.db.exec("UPDATE dispatch_requests SET state='STARTED' WHERE id=?",{booking});break;case 7:f.db.exec("UPDATE bookings SET end_key='bad' WHERE id=?",{booking});break;case 8:f.db.exec("INSERT INTO booking_metadata VALUES(?, 'unknown')",{"mining-auto-after-manual/v1/"+Device.toLower()});break;}const auto before=f.db.dump();const auto read=f.store.savedPreparedNextAdmission(Device,booking,base);check((read.status==PreparedNextReadStatus::Unavailable||poison==2&&read.status==PreparedNextReadStatus::Absent)&&!read.admission.dispatch&&f.db.dump()==before,"typed current prepared read rejects changed tuple/metadata, preserves every row/date");}
 for(int poison=0;poison<3;++poison){Fixture f;check(f.store.setAutomaticAfterManual(Device,true).isEmpty(),"ON for actual automatic claim fixture");const auto a=f.prepare();prepared(a,true);const auto booking=a.dispatch->bookingId;switch(poison){case 0:f.db.exec("UPDATE bookings SET start=?,end=?,start_key=?,end_key=? WHERE id=?",{plus(1),plus(2),key(plus(1)),key(plus(2)),booking});f.db.exec("UPDATE dispatch_requests SET start=?,end=? WHERE id=?",{plus(1),plus(2),booking});break;case 1:f.db.exec("UPDATE automatic_claims SET predecessor=?",{QString(32,'f')});break;case 2:f.db.exec("DELETE FROM automatic_claims WHERE booking_id=?",{booking});break;}const auto before=f.db.dump();const auto read=f.store.savedPreparedNextAdmission(Device,booking,f.predecessor);check(read.status==PreparedNextReadStatus::Unavailable&&!read.admission.dispatch&&f.db.dump()==before,"automatic UUIDv8 binds predecessor/range/device; missing origin or coherent row rebind Held");}
}
void initialAndResumeObservations(){
 for(int poison=-1;poison<5;++poison){
  Fixture f;const auto request=id(700),observation=id(701),oldRun=QString(32,'e'),newRun=QString(32,'f');
  const auto initial=f.store.prepareInitialStart(request,Device,scope(),registry(),{WorkBoundary::Ready,observation});check(initial.status==InitialStartStatus::Prepared&&initial.dispatch,"new real initial admission after prior SOURCE, fixture only");
  auto packet=f.snapshot(*initial.dispatch,oldRun),binding=packet.value("binding").toObject();binding.remove("predecessor");binding["initial_start"]=QJsonObject{{"origin","INITIAL_START"},{"request_id",request}};packet["binding"]=binding;
  const auto first=f.store.reconcileObservedDispatch(packet);check(first.status==ObservedDispatchReconcileStatus::Reconciled,"initial origin uses its exact immutable receipt, not an ACK");
  check(f.store.observeRuntime(oldRun,initial.dispatch->start,initial.dispatch->end,Device,false).isEmpty(),"own interrupted original runtime PAUSED");
  StoredSourceResume cp;cp.checkpointBytes="PGNCG001";for(const quint64 word:{15432559262904483840ULL,10ULL,1000000000000000ULL,123ULL,9007199254740993ULL,7ULL,2ULL})for(int i=0;i<8;++i)cp.checkpointBytes.append(char((word>>(i*8))&255));
  auto& r=cp.resume;r.run=oldRun;r.start=First;r.end=plus(1);r.length=Width;r.positions="123";r.nextStart=addUnsigned(First,"123");r.remaining=subtractUnsigned(Width,"123");r.tests="9007199254740993";r.gaps="7";r.batches="2";r.counterTimeNs="12345";r.checkpointLabel="C:/PUBLIC-TESTONLY/checkpoint";r.checkpointSha256=QString::fromLatin1(QCryptographicHash::hash(cp.checkpointBytes,QCryptographicHash::Sha256).toHex());
  check(f.store.saveResumeCheckpoint(Device,r,cp.checkpointBytes).isEmpty(),"native exact checkpoint storage fixture");
  const auto resumed=f.store.prepareCheckpointResume(id(702),Device,initial.dispatch->bookingId,oldRun,newRun,r.checkpointSha256,id(703));check(resumed.status==CheckpointResumeAdmissionStatus::Prepared,"native epoch reserved before physical resume observation");
  auto m=f.manifest(newRun,initial.dispatch->bookingId,First,plus(1));m["resume_previous_run"]=oldRun;m["checkpoint_input_sha256"]=r.checkpointSha256;
  binding["resume_start"]=QJsonObject{{"schema","pgn-checkpoint-resume-marker-1"},{"request_id",id(702)},{"observation_id",id(703)},{"run",newRun},{"previous_run",oldRun},{"checkpoint_sha256",r.checkpointSha256}};
  packet["state"]="STOPPING";packet["operator_stop"]=true;packet["manifest"]=m;packet["binding"]=binding;
  switch(poison){case 0:{auto marker=binding.value("resume_start").toObject();marker["request_id"]=id(999);binding["resume_start"]=marker;packet["binding"]=binding;break;}case 1:m["checkpoint_input_sha256"]=QString(64,'0');packet["manifest"]=m;break;case 2:packet["owned"]=false;packet["state"]="OFF";packet["has_checkpoint"]=true;break;case 3:f.db.exec("UPDATE dispatch_requests SET run=? WHERE id=?",{oldRun,initial.dispatch->bookingId});break;case 4:f.db.exec("UPDATE bookings SET state='ACTIVE' WHERE id=?",{oldRun});break;}
  const auto before=f.db.dump();const auto result=f.store.reconcileObservedDispatch(packet);
  if(poison>=0){check(result.status==ObservedDispatchReconcileStatus::Held&&!result.error.isEmpty()&&f.db.dump()==before,"resume marker/pointer/old-runtime/first-OFF poison Held with zero partial writes");continue;}
  check(result.status==ObservedDispatchReconcileStatus::Reconciled&&result.run==newRun,"resumed owned STOPPING observation reconciles physical epoch, not new work or Auto permission");
  check(f.db.value("SELECT state FROM bookings WHERE id=?",oldRun)=="PAUSED"&&f.db.value("SELECT state FROM bookings WHERE id=?",initial.dispatch->bookingId)=="DISPATCHED"&&f.db.value("SELECT state FROM dispatch_requests WHERE id=?",initial.dispatch->bookingId)=="STARTED","old PAUSED ancestry retained; sole active pointer/local projection advances");
  check(f.store.recordQueuedSourceCompletion(newRun,First,plus(1),Device,initial.dispatch->bookingId).isEmpty(),"caller-paid resumed SOURCE tuple storage, not RAW/coverage proof");
  f.predecessor={newRun,Birth,First,plus(1),initial.dispatch->bookingId};const auto manual=f.queue(2,3);const auto next=f.prepare();prepared(next,false);check(next.dispatch->bookingId==manual&&f.db.value("SELECT state FROM bookings WHERE id=?",oldRun)=="PAUSED","SOURCE-covered PAUSED ancestor is historical, not false active blocker; never erased/normalized");
  f.db.exec("DELETE FROM booking_metadata WHERE key=?",{"mining-source-manifest/v1/"+oldRun});const auto corrupt=f.db.dump();const auto missing=f.store.savedPreparedNextAdmission(Device,next.dispatch->bookingId,f.predecessor);check(missing.status==PreparedNextReadStatus::Unavailable&&!missing.admission.dispatch&&f.db.dump()==corrupt,"missing actual old lineage manifest Held on next readback, no legacy adoption/partial repair");
 }
}
void poisons(){
 for(int poison=0;poison<12;++poison){Fixture f;f.queue(8,9);switch(poison){case 0:f.predecessor.birth+="\n";break;case 1:f.predecessor.run=QString(32,'f');break;case 2:f.predecessor.queuedIntent=id(900);break;case 3:f.db.exec("UPDATE bookings SET state='PAUSED' WHERE id=?",{Root});break;case 4:f.db.exec("DELETE FROM booking_metadata WHERE key=?",{"mining-source-manifest/v1/"+Root});break;case 5:f.db.exec("UPDATE booking_metadata SET value='corrupt' WHERE key=?",{"mining-session-mode/v1/"+Device.toLower()});break;case 6:f.db.exec("UPDATE bookings SET start_key='bad' WHERE state='QUEUED'");break;case 7:f.db.exec("INSERT INTO booking_metadata VALUES('queue-order-mode','unknown')");break;case 8:f.db.exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{f.oldBooking});break;case 9:f.db.exec("UPDATE booking_metadata SET value=CAST(value AS BLOB) WHERE key=?",{"mining-source-manifest/v1/"+Root});break;case 10:f.db.exec("UPDATE bookings SET state='ABORTED' WHERE id=?",{Root});break;case 11:f.db.exec("UPDATE dispatch_requests SET state='UNKNOWN' WHERE id=?",{f.oldBooking});break;}held(f);}
 for(int poison=0;poison<15;++poison){Fixture f;f.queue(8,9);auto a=f.prepare();prepared(a,false);auto packet=f.snapshot(*a.dispatch,QString(32,'b')),m=packet.value("manifest").toObject(),binding=packet.value("binding").toObject();switch(poison){case 0:packet.remove("owned");break;case 1:packet["owned"]="true";break;case 2:packet["owned"]=false;packet["state"]="OFF";packet["has_checkpoint"]=true;break;case 3:packet["state"]="OFF";break;case 4:m["pid"]=0;break;case 5:m["birth"]=Birth+"\n";break;case 6:m["run"]=Root;break;case 7:m["queued_intent"]=id(999);break;case 8:binding["start"]=plus(7);break;case 9:binding.remove("predecessor");break;case 10:{auto b=binding.value("predecessor").toObject();b["run"]=QString(32,'f');binding["predecessor"]=b;break;}case 11:m["schema"]="unknown";break;case 12:m["start"]=1.999e20;break;case 13:packet.remove("operator_stop");break;case 14:binding["queued_intent"]=id(999);break;}packet["manifest"]=m;packet["binding"]=binding;const auto before=f.db.dump();const auto r=f.store.reconcileObservedDispatch(packet);check(r.status==ObservedDispatchReconcileStatus::Held&&!r.error.isEmpty()&&f.db.dump()==before,"unknown/foreign/malformed physical identity Held atomically; OFF partial is explicitly not owned");}
}
void rollbackAndCapacity(){
 for(const auto& trigger:{QByteArray("CREATE TRIGGER fail_next AFTER INSERT ON dispatch_requests WHEN new.predecessor IS NOT NULL BEGIN SELECT RAISE(FAIL,'after dispatch'); END"),QByteArray("CREATE TRIGGER fail_next AFTER UPDATE ON bookings WHEN new.state='DISPATCHING' BEGIN SELECT RAISE(FAIL,'after local'); END")}){Fixture f;f.queue(8,11);f.db.script(trigger.constData());held(f);}
 {Fixture f;f.queue(8,9);auto p=f.prepare();prepared(p,false);f.db.script("CREATE TRIGGER fail_observe AFTER INSERT ON booking_metadata WHEN new.key LIKE 'mining-source-manifest/v1/%' BEGIN SELECT RAISE(FAIL,'after manifest'); END");const auto before=f.db.dump();const auto r=f.store.reconcileObservedDispatch(f.snapshot(*p.dispatch,QString(32,'b')));check(r.status==ObservedDispatchReconcileStatus::Held&&f.db.dump()==before,"reconcile runtime/local/dispatch/manifest AFTER FAIL rolls back together");}
 for(bool split:{false,true}){Fixture f;check(f.store.setAutomaticAfterManual(Device,true).isEmpty(),"ON for capacity test");if(split)f.queue(8,11);f.db.script("BEGIN");for(int i=0;i<(split?4096:4097);++i){const auto a=addUnsigned("900000000000000000000000000000",QString::number(i*2));f.db.exec("INSERT INTO bookings VALUES(?,?,?,?,?,'QUEUED','LOCAL_INTENT',?,'TESTONLY-UNCHANGED')",{id(i+1),a,addUnsigned(a,"1"),key(a),key(addUnsigned(a,"1")),Device});}f.db.script("COMMIT");held(f);}
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{manualOnOff();retiredAbortedHistory();zonesAndSplit();legacyLookahead();readbackAndBoundaries();initialAndResumeObservations();poisons();rollbackAndCapacity();std::cout<<"Native next-boundary admission: "<<checks<<" checks / 0 failures; real temporary SQLite; synthetic TESTONLY observations; no transport/physical/freshness/independent-coverage claim\n";return 0;}catch(const std::exception& e){std::cerr<<"Next boundary failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}

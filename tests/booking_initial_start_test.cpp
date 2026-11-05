#include <QJsonDocument>
#include "booking/booking_store.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QUuid>
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>
#include <QFileInfo>
#include <QElapsedTimer>

using namespace pgn::booking;using namespace pgn::mining;
namespace {
int checks{};const QString A="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",B="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265";
const QString First="199900000000000000000",Last="200000000000000000000",Width="1000000000000000";
const QString Request="11111111-2222-4333-8444-555555555555",Other="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
void check(bool b,const char* s){++checks;if(!b)throw std::runtime_error(s);}
QString plus(const QString& a,int n){QString x=a;for(int i=0;i<n;++i)x=addUnsigned(x,Width);return x;}
QString sortKey(const QString& v){return QString::number(v.size()).rightJustified(3,'0')+v;}
SourceRegistry registry(){SourceRegistry r;r.url="https://PUBLIC-TEST.invalid/source";r.sha256=QString(64,'1');r.observedUtc=QDateTime::fromString("2026-10-14T00:00:00Z",Qt::ISODate);r.ranges={{999,subtractUnsigned(First,"100000000000000000"),First,"Open",{}},{1000,First,Last,"Open",{}}};r.start="WRONG configured binding start";r.end="WRONG configured binding end";return r;}
InitialStartScope scope(){return {First,Last,First,Last,"0",Width};}
InitialBoundaryEvidence proof(){return {WorkBoundary::Ready,"12345678-1234-4567-89ab-123456789abc"};}
class Database {
public:
 sqlite3* db{};
 explicit Database(const QString& file){if(sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)throw std::runtime_error("own SQLite open");}
 ~Database(){if(db)sqlite3_close(db);}
 void sql(const QByteArray& s){if(sqlite3_exec(db,s.constData(),nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
 QByteArray dump(const char* s){sqlite3_stmt* q{};if(sqlite3_prepare_v2(db,s,-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture dump prepare");QByteArray out;int rc;
  while((rc=sqlite3_step(q))==SQLITE_ROW){for(int c=0;c<sqlite3_column_count(q);++c){out+=QByteArray::number(sqlite3_column_type(q,c))+":";out+=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(q,c)),sqlite3_column_bytes(q,c)).toHex()+";";}out+='\n';}sqlite3_finalize(q);if(rc!=SQLITE_DONE)throw std::runtime_error("fixture dump step");return out;}
 bool dispatchExists(){return !dump("SELECT name FROM sqlite_master WHERE name='dispatch_requests'").isEmpty();}
 QByteArray snapshot(){return dump("SELECT type,name,sql FROM sqlite_master ORDER BY name")+dump("SELECT * FROM bookings ORDER BY id")+dump("SELECT * FROM booking_events ORDER BY sequence")+dump("SELECT * FROM sqlite_sequence ORDER BY name")+dump("SELECT * FROM booking_metadata ORDER BY key")+dump("SELECT * FROM queue_zone_order ORDER BY zone_start")+(dispatchExists()?dump("SELECT * FROM dispatch_requests ORDER BY id"):QByteArray{});}
 void rawMeta(const QString& k,const QByteArray& raw,bool blob=false){sqlite3_stmt* q{};sqlite3_prepare_v2(db,"INSERT INTO booking_metadata VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",-1,&q,nullptr);const auto name=k.toUtf8();sqlite3_bind_text(q,1,name.constData(),name.size(),SQLITE_TRANSIENT);if(blob)sqlite3_bind_blob(q,2,raw.constData(),raw.size(),SQLITE_TRANSIENT);else sqlite3_bind_text(q,2,raw.constData(),raw.size(),SQLITE_TRANSIENT);const auto rc=sqlite3_step(q);sqlite3_finalize(q);if(rc!=SQLITE_DONE)throw std::runtime_error("fixture metadata write");}
};
struct Fixture {QTemporaryDir temp;QString file;BookingStore store;Database db;Fixture():file(temp.filePath("local-bookings.sqlite")),store(file),db(file){check(temp.isValid()&&store.valid(),"own temp real booking store");db.rawMeta("fixture/unrelated","KEEP");}};
void held(Fixture& f,const QString& request=Request,const InitialStartScope& s=scope(),const SourceRegistry& r=registry(),const InitialBoundaryEvidence& p=proof()){
 const auto before=f.db.snapshot();const auto result=f.store.prepareInitialStart(request,A,s,r,p);check(result.status==InitialStartStatus::Held&&!result.error.isEmpty(),"invalid/unknown/conflict explicit Held");check(f.db.snapshot()==before,"Held conserves all booking/event/metadata/schema rows");
}
void checkPrepared(const InitialStartResult& r){check(r.status==InitialStartStatus::Prepared&&r.dispatch&&r.dispatch->automatic.has_value()&&r.dispatch->state=="PREPARED"&&r.dispatch->origin=="INITIAL_START"&&!r.dispatch->predecessor&&!r.dispatch->run,"fresh PREPARED has known mode, INITIAL_START and NULL predecessor/run, not a completed predecessor");}
void manual(){
 Fixture f;const auto r=registry();check(f.store.queue(r,plus(First,10),plus(First,11),A).isEmpty()&&f.store.queue(r,plus(First,7),plus(First,8),A).isEmpty(),"real reservations11 then8");
 auto prepared=f.store.prepareInitialStart(Request,A,scope(),r,proof());checkPrepared(prepared);check(prepared.mode==MiningSessionMode::ManualReservations&&prepared.dispatch->start==plus(First,7),"manual head8 chosen before11 independent of insertion/binding");
 const auto rows=f.store.active();bool next=false;for(const auto& b:rows)if(b.start==plus(First,10)&&b.state=="QUEUED")next=true;check(next,"remaining manual reservation unchanged");
 const auto original=f.db.snapshot();f.db.sql("CREATE TRIGGER no_book_update BEFORE UPDATE ON bookings BEGIN SELECT RAISE(FAIL,'no replay UPDATE'); END;CREATE TRIGGER no_meta_update BEFORE UPDATE ON booking_metadata BEGIN SELECT RAISE(FAIL,'no replay UPDATE'); END;CREATE TRIGGER no_dispatch_update BEFORE UPDATE ON dispatch_requests BEGIN SELECT RAISE(FAIL,'no replay UPDATE'); END;");
 const auto before=f.db.snapshot();auto replay=f.store.prepareInitialStart(Request,A,scope(),r,proof());check(replay.status==InitialStartStatus::ReconcilePrepared&&replay.replayed&&replay.dispatch->bookingId==prepared.dispatch->bookingId,"same request returns exact reconciliation not a new launch token");check(f.db.snapshot()==before,"exact replay makes no UPDATE");
 auto changed=scope();changed.autoStart=plus(First,1);held(f,Request,changed);
 auto changedSource=r;changedSource.sha256=QString(64,'2');held(f,Request,scope(),changedSource);
 auto changedProof=proof();changedProof.observationUUID=Other;held(f,Request,scope(),r,changedProof);
 const auto pending=f.store.prepareInitialStart(Other,A,scope(),r,proof());check(pending.status==InitialStartStatus::ReconcilePrepared&&pending.dispatch->bookingId==prepared.dispatch->bookingId&&!pending.replayed&&f.db.snapshot()==before,"other command sees existing same-device PREPARED, no replacement/receipt/allocation");
 (void)original;
}
void zones(){
 Fixture f;const auto r=registry();const auto previous=r.ranges[0].start;
 check(f.store.queue(r,previous,plus(previous,1),A).isEmpty()&&f.store.queue(r,plus(First,7),plus(First,8),A).isEmpty(),"manual two-zone queue");held(f); // numeric head outside execution: do NOT skip it.
 QStringList ids;for(const auto& b:f.store.active())ids<<b.id;
 const ExactBlockSelection last{First,Last,First,Last},first{previous,First,previous,First};check(f.store.reorderQueuedZones({last,first},ids).isEmpty(),"real explicit zone order");
 const auto result=f.store.prepareInitialStart(Request,A,scope(),r,proof());checkPrepared(result);check(result.dispatch->start==plus(First,7),"zone priority then block order followed");
}
void replayRetainedStore(const QString& source){
 check(QFileInfo(source).isFile(),"Existing booking SQLite required, never create source");
 Fixture f;sqlite3* readOnly{};
 check(sqlite3_open_v2(source.toUtf8().constData(),&readOnly,SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr)==SQLITE_OK,"Actual source opens READONLY");
 auto* backup=sqlite3_backup_init(f.db.db,"main",readOnly,"main");
 if(!backup){sqlite3_close(readOnly);throw std::runtime_error("Private consistent SQLite backup unavailable");}
 QElapsedTimer budget;budget.start();int rc{};
 do{rc=sqlite3_backup_step(backup,64);}while(rc==SQLITE_OK&&budget.elapsed()<10000);
 const auto finished=sqlite3_backup_finish(backup);sqlite3_close(readOnly);
 check(rc==SQLITE_DONE&&finished==SQLITE_OK,"Private backup complete within diagnostic budget");
 const auto retained=f.db.dump("SELECT * FROM bookings WHERE state='ABORTED' ORDER BY id");
 auto s=scope();s.autoStart=First;
 const auto result=f.store.prepareInitialStart(Request,A,s,registry(),proof());checkPrepared(result);
 check(f.db.dump("SELECT * FROM bookings WHERE state='ABORTED' ORDER BY id")==retained,"Retained aborted rows unchanged in private replay");
 const QJsonObject output{{"scope","PRIVATE_READONLY_SOURCE_STATE_REPLAY_WITH_SYNTHETIC_REGISTRY_AND_BOUNDARY_NOT_REAL_ADMISSION"},{"status","PREPARED_IN_TEMP_ONLY"},{"start",result.dispatch->start},{"end",result.dispatch->end},{"automatic",result.dispatch->automatic.value_or(false)}};
 std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).constData()<<std::endl;
}
void retiredAbortedHistory(){
 Fixture f;const QString run(32,'c');
 check(f.store.recordSourceCompletion(run,First,plus(First,1),A).isEmpty(),"own runtime fixture row");
 f.db.sql(("UPDATE bookings SET state='ABORTED' WHERE id='"+run+"';").toUtf8());
 const auto result=f.store.prepareInitialStart(Request,A,scope(),registry(),proof());checkPrepared(result);
 check(result.dispatch->start==First,"Retired aborted runtime gives no completed coverage or active reservation");
 check(f.db.dump("SELECT state FROM bookings WHERE state='ABORTED'").contains("41424f52544544"),"Original aborted historical row is retained, never rewritten as complete");
 Fixture local;check(local.store.queue(registry(),First,plus(First,1),A).isEmpty(),"own local intent");local.db.sql("UPDATE bookings SET state='ABORTED';");held(local);
 Fixture corrupt;check(corrupt.store.recordSourceCompletion(run,First,plus(First,1),A).isEmpty(),"own bad-key runtime");corrupt.db.sql("UPDATE bookings SET state='ABORTED',start_key='bad';");held(corrupt);
}
void automatic(){
 Fixture f;auto r=registry();r.ranges={{1000,First,plus(First,2),"Open",{}},{1001,plus(First,2),plus(First,3),"Reserved","PUBLIC"},{1002,plus(First,3),Last,"Open",{}}};
 check(f.store.recordSourceCompletion(QString(32,'a'),First,plus(First,1),A).isEmpty(),"seed genuine local finished range only");check(f.store.queue(r,plus(First,1),plus(First,2),B).isEmpty(),"other GPU queued overlap");
 const auto result=f.store.prepareInitialStart(Request,A,scope(),r,proof());checkPrepared(result);check(result.mode==MiningSessionMode::Automatic&&result.dispatch->start==plus(First,3)&&result.dispatch->end==plus(First,4),"automatic first ENTIRE free block skips occupied+Reserved across Open rows, stays zone1000");
 check(f.store.automaticAfterManual(A).has_value()&&!f.store.automaticAfterManual(A).value(),"initial auto changes no after-manual preference");
 Fixture noBlock;auto tiny=registry();tiny.ranges={{1000,addUnsigned(First,"1"),subtractUnsigned(plus(First,1),"1"),"Open",{}}};held(noBlock,Request,scope(),tiny);
}
void invalid(){
 for(const auto boundary:{WorkBoundary::Unknown,WorkBoundary::Running,WorkBoundary::Completed,WorkBoundary::Interrupted}){
  Fixture f;const auto before=f.db.snapshot();auto p=proof();p.boundary=boundary;const auto result=f.store.prepareInitialStart(Request,A,scope(),registry(),p);
  check(result.status==(boundary==WorkBoundary::Interrupted?InitialStartStatus::ResumeRequired:InitialStartStatus::Held),"noninitial boundaries cannot allocate a new run");check(f.db.snapshot()==before&&!f.db.dispatchExists(),"rejected boundary creates no table/default/booking");
 }
 for(int i=0;i<10;++i){Fixture f;auto s=scope();auto r=registry();auto p=proof();QString request=Request;
  switch(i){case 0:s.autoEnd=plus(Last,1);break;case 1:s.blockWidth="0";break;case 2:s.executionStart="01";break;case 3:r.error="unknown registry";break;case 4:r.ranges[1].status="unknown";break;case 5:r.ranges[1].owner="owner on Open";break;case 6:r.observedUtc={};break;case 7:r.sha256="not hash";break;case 8:p.observationUUID="unknown";break;case 9:request="00000000-0000-0000-0000-000000000000";break;}held(f,request,s,r,p);
 }
 for(const auto& raw:QVector<QByteArray>{"invalid",QByteArray("manual-reservations\0",20)}){Fixture f;f.db.rawMeta("mining-session-mode/v1/"+A.toLower(),raw);held(f);}
 {Fixture f;f.db.rawMeta("queue-order-mode","manual",true);held(f);}
 {Fixture f;check(f.store.queue(registry(),First,plus(First,1),A).isEmpty(),"seed interrupted reservation");f.db.sql("UPDATE bookings SET state='PAUSED';");const auto before=f.db.snapshot();const auto result=f.store.prepareInitialStart(Request,A,scope(),registry(),proof());check(result.status==InitialStartStatus::ResumeRequired&&f.db.snapshot()==before,"stored PAUSED cannot become auto/new initial mapping");}
}
void rollbackBusy(){
 {Fixture f;Database other(f.file);other.sql("BEGIN IMMEDIATE");held(f);other.sql("ROLLBACK");const auto result=f.store.prepareInitialStart(Request,A,scope(),registry(),proof());checkPrepared(result);}
 for(bool manualCase:{false,true}){Fixture f;if(manualCase)check(f.store.queue(registry(),First,plus(First,1),A).isEmpty(),"manual rollback fixture");
  f.db.sql("CREATE TRIGGER fail_receipt AFTER INSERT ON booking_metadata WHEN NEW.key LIKE 'mining-initial-start/%' BEGIN INSERT INTO booking_metadata VALUES('fixture/sideeffect','rollback'); SELECT RAISE(FAIL,'AFTERFAIL'); END;");
  held(f);check(!f.db.dispatchExists(),"DDL+mode+booking+audit+receipt+AFTERFAIL sideeffect all roll back");
 }
 {Fixture f;auto first=f.store.prepareInitialStart(Request,A,scope(),registry(),proof());checkPrepared(first);BookingStore other(f.file);const auto before=f.db.snapshot();auto second=other.prepareInitialStart(Other,A,scope(),registry(),proof());check(second.status==InitialStartStatus::ReconcilePrepared&&second.dispatch->bookingId==first.dispatch->bookingId&&f.db.snapshot()==before,"second real connection sees same pending, never double allocation");
  auto otherDevice=other.prepareInitialStart("bbbbbbbb-cccc-4ddd-8eee-ffffffffffff",B,scope(),registry(),proof());checkPrepared(otherDevice);check(otherDevice.dispatch->device==B&&otherDevice.dispatch->start==plus(First,1),"different device never receives another device pending; overlap skipped");
 }
}
void capacity(){
 for(bool finished:{false,true}){Fixture f;f.db.sql("BEGIN IMMEDIATE");sqlite3_stmt* q{};sqlite3_prepare_v2(f.db.db,"INSERT INTO bookings VALUES(?,?,?,?,?,?,?,?,?)",-1,&q,nullptr);
  for(int i=0;i<(finished?4100:4097);++i){const auto a=QString::number(i*10),b=QString::number(i*10+10);const QStringList values{"PUBLIC-"+QString::number(i),a,b,sortKey(a),sortKey(b),finished?"SOURCE_FINISHED":"QUEUED",finished?"RUNTIME":"LOCAL_INTENT",B,"FIXTURE"};
   for(int c=0;c<values.size();++c){const auto raw=values[c].toUtf8();sqlite3_bind_text(q,c+1,raw.constData(),raw.size(),SQLITE_TRANSIENT);}if(sqlite3_step(q)!=SQLITE_DONE)throw std::runtime_error("own capacity fixture insert");sqlite3_reset(q);sqlite3_clear_bindings(q);
  }sqlite3_finalize(q);f.db.sql("COMMIT");
  if(finished){const auto result=f.store.prepareInitialStart(Request,A,scope(),registry(),proof());checkPrepared(result);check(result.dispatch->start==First,"4100 historical rows are NOT the4097 active-insertion limit");}else held(f);
 }
}
void preparedDispatchObservation(){
 Fixture f;const auto absent=f.db.snapshot();
 check(!f.store.hasPreparedDispatches().has_value(),"Absent dispatch table is unknown, never a known empty queue");
 check(f.db.snapshot()==absent,"Prepared-presence read cannot create schema or mutate history");
 checkPrepared(f.store.prepareInitialStart(Request,A,scope(),registry(),proof()));
 const auto before=f.db.snapshot();check(f.store.hasPreparedDispatches()==true,"Actual SAMEStore PREPARED remains unresolved");
 check(f.db.snapshot()==before,"Presence observation leaves PREPARED and its receipts unchanged");
 f.db.sql("UPDATE dispatch_requests SET device='GPU-11111111-2222-3333-4444-555555555555';");
 check(f.store.hasPreparedDispatches()==true,"Prepared dispatch on another device is not omitted");
 for(const auto state:{"ARMED","STARTED","SOURCE_FINISHED","CANCELLED"}){
  f.db.sql(QByteArray("UPDATE dispatch_requests SET state='")+state+"';");
  check(f.store.hasPreparedDispatches()==false,"Only recognized nonprepared state can be a known negative");
 }
 f.db.sql("PRAGMA ignore_check_constraints=ON;UPDATE dispatch_requests SET state='UNKNOWN';");
 check(!f.store.hasPreparedDispatches().has_value(),"Unknown textual state is unavailable, not false");
 f.db.sql("UPDATE dispatch_requests SET state=CAST('PREPARED' AS BLOB);");
 check(!f.store.hasPreparedDispatches().has_value(),"Blob state cannot masquerade as prepared text or an empty queue");
 f.db.sql("UPDATE dispatch_requests SET state='STARTED',device=CAST('GPU-11111111-2222-3333-4444-555555555555' AS BLOB);");
 check(!f.store.hasPreparedDispatches().has_value(),"Invalid device type holds a negative observation");
 f.db.sql("DELETE FROM dispatch_requests;");check(f.store.hasPreparedDispatches()==false,"Existing typed empty table has a genuine negative observation");
 Fixture malformed;malformed.db.sql("CREATE TABLE dispatch_requests(state TEXT);");
 check(!malformed.store.hasPreparedDispatches().has_value(),"Missing required column cannot be treated as an empty queue");
 Fixture view;view.db.sql("CREATE VIEW dispatch_requests AS SELECT 'STARTED' AS state,'GPU-11111111-2222-3333-4444-555555555555' AS device;");
 check(!view.store.hasPreparedDispatches().has_value(),"A substituted view is not the native dispatch table");
 BookingStore unavailable(f.temp.path());check(!unavailable.valid()&&!unavailable.hasPreparedDispatches().has_value(),"Unavailable SAMEStore connection stays unknown");
}
void contradictoryHead(){
 for(int poison=0;poison<3;++poison){Fixture f;check(f.store.queue(registry(),First,plus(First,1),A).isEmpty(),"seed manual head for dispatch discriminator");const auto id=f.store.active().first().id;
  f.db.sql("CREATE TABLE dispatch_requests(id TEXT PRIMARY KEY,start TEXT NOT NULL,end TEXT NOT NULL,device TEXT NOT NULL,state TEXT NOT NULL,predecessor TEXT,run TEXT,created_utc TEXT NOT NULL);");
  const auto state=poison==0?QString("UNKNOWN"):poison==1?QString("CANCELLED"):QString("ARMED"),predecessor=poison==2?"'"+QString(32,'a')+"'":QString("NULL");
  f.db.sql(("INSERT INTO dispatch_requests VALUES('"+id+"','"+First+"','"+plus(First,1)+"','"+A+"','"+state+"',"+predecessor+",NULL,'FIXTURE');").toUtf8());held(f);
 }
 Fixture old;check(old.store.queue(registry(),First,plus(First,1),A).isEmpty(),"legacy pending without mode");const auto id=old.store.active().first().id;
 old.db.sql("CREATE TABLE dispatch_requests(id TEXT PRIMARY KEY,start TEXT NOT NULL,end TEXT NOT NULL,device TEXT NOT NULL,state TEXT NOT NULL,predecessor TEXT,run TEXT,created_utc TEXT NOT NULL);");
 old.db.sql(("INSERT INTO dispatch_requests VALUES('"+id+"','"+First+"','"+plus(First,1)+"','"+A+"','PREPARED',NULL,NULL,'FIXTURE');UPDATE bookings SET state='DISPATCHING';").toUtf8());
 const auto before=old.db.snapshot();const auto pending=old.store.prepareInitialStart(Request,A,scope(),registry(),proof());
 check(pending.status==InitialStartStatus::ReconcilePrepared&&pending.mode==MiningSessionMode::Unselected&&pending.dispatch&&!pending.dispatch->automatic.has_value(),"legacy pending mode remains UNKNOWN, not guessed Manual/Auto");check(old.db.snapshot()==before,"legacy pending reconciliation writes no mode or receipt");
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{if(argc==3&&QString::fromLocal8Bit(argv[1])=="--replay-store"){replayRetainedStore(QString::fromLocal8Bit(argv[2]));return 0;}if(app.arguments().contains("--prepared-presence-only"))preparedDispatchObservation();else{manual();zones();automatic();retiredAbortedHistory();invalid();rollbackBusy();capacity();contradictoryHead();preparedDispatchObservation();}std::cout<<"PASS checks="<<checks<<" failures=0; real sameSQLite INITIAL_START admission only, synthetic scopes/provenance/caller boundary; no launcher/process/GPU/UI\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL checks="<<checks<<" "<<e.what()<<'\n';return 1;}}

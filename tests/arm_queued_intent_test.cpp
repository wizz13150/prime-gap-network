#include "booking/booking_store.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>

// CLOSED: owned temporary SQLite and synthetic TESTONLY scope/source tuples.
// No writer/transport/PS/controller/UI/runtime/driver/GPU is instantiated.
using namespace pgn::booking;using namespace pgn::mining;
namespace {
int checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",Other="GPU-11111111-2222-3333-4444-555555555555",Root(32,'a');
const QString First="199900000000000000000",Width="1000000000000000",Birth="2026-10-15T00:00:00.1234567Z";
QString plus(int n){return addUnsigned(First,QString::number(quint64(n)*1000000000000000ULL));}
QString id(int n){return QString("00000000-0000-4000-8000-%1").arg(n,12,16,QChar('0'));}
QString key(const QString& s){return QString::number(s.size()).rightJustified(3,'0')+s;}
SourceRegistry registry(){SourceRegistry r;r.url="https://PUBLIC-TESTONLY.invalid/source";r.sha256=QString(64,'1');r.observedUtc=QDateTime::fromString("2026-10-15T00:00:00Z",Qt::ISODate);r.ranges={{1000,First,plus(100),"Open",{}}};return r;}
InitialStartScope scope(){return {First,plus(100),First,plus(100),First,Width};}
class Sql {
public:
 sqlite3* db{};
 explicit Sql(const QString& path){need(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"own fixture DB");}
 ~Sql(){if(db)sqlite3_close(db);}
 void exec(const char* sql,const QStringList& values={}){sqlite3_stmt* q{};need(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK,"own fixture SQL prepare");for(int i=0;i<values.size();++i){const auto b=values[i].toUtf8();sqlite3_bind_text(q,i+1,b.constData(),b.size(),SQLITE_TRANSIENT);}const int rc=sqlite3_step(q);const auto error=std::string(sqlite3_errmsg(db));sqlite3_finalize(q);if(rc!=SQLITE_DONE)throw std::runtime_error("own fixture SQL mutation: "+error+" SQL="+sql);}
 QString value(const char* sql,const QString& parameter={}){sqlite3_stmt* q{};need(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK,"own fixture SQL read");if(!parameter.isNull()){auto b=parameter.toUtf8();sqlite3_bind_text(q,1,b.constData(),b.size(),SQLITE_TRANSIENT);}QString s;if(sqlite3_step(q)==SQLITE_ROW)s=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(q,0)),sqlite3_column_bytes(q,0));sqlite3_finalize(q);return s;}
 QByteArray dump(){QByteArray out;sqlite3_stmt* tables{};need(sqlite3_prepare_v2(db,"SELECT name FROM sqlite_master WHERE type='table' ORDER BY name",-1,&tables,nullptr)==SQLITE_OK,"own tables snapshot");while(sqlite3_step(tables)==SQLITE_ROW){auto name=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(tables,0)));out+=name.toUtf8()+':';sqlite3_stmt* q{};auto sql=("SELECT * FROM \""+name+"\" ORDER BY rowid").toUtf8();need(sqlite3_prepare_v2(db,sql.constData(),-1,&q,nullptr)==SQLITE_OK,"own rows snapshot");while(sqlite3_step(q)==SQLITE_ROW){for(int c=0;c<sqlite3_column_count(q);++c){out+=QByteArray::number(sqlite3_column_type(q,c))+':';out+=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(q,c)),sqlite3_column_bytes(q,c)).toHex()+';';}out+='\n';}sqlite3_finalize(q);}sqlite3_finalize(tables);return out;}
};
struct Fixture {
 QTemporaryDir temp;QString file,oldBooking;BookingStore store;Sql db;
 Fixture():file(temp.filePath("native.sqlite")),store(file),db(file){
  check(temp.isValid()&&store.valid(),"private real native DB");const auto end=addUnsigned(plus(33),"500");check(store.queue(registry(),plus(33),end,Device).isEmpty(),"existing 500-position manual fragment");
  const auto initial=store.prepareInitialStart(id(1),Device,scope(),registry(),{WorkBoundary::Ready,id(2)});check(initial.status==InitialStartStatus::Prepared&&initial.dispatch.has_value(),"real initial fragment admitted");oldBooking=initial.dispatch->bookingId;
  db.exec("UPDATE dispatch_requests SET state='STARTED',run=? WHERE id=?",{Root,oldBooking});db.exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{oldBooking});check(store.observeRuntime(Root,plus(33),end,Device,true).isEmpty(),"existing current 500-position ACTIVE runtime");
  const QJsonObject m{{"schema","pgn-native-process-1"},{"run",Root},{"uuid",Device},{"pid",4242},{"birth",Birth},{"start",plus(33)},{"end",end},{"queued_intent",oldBooking},{"stdout","C:/PUBLIC-TESTONLY/old.stdout"}};check(store.saveSourceRunManifest(Device,m).isEmpty(),"existing actual-shaped immutable manifest");
  check(store.setMiningSessionMode(Device,MiningSessionMode::Automatic).isEmpty()&&store.setAutomaticAfterManual(Device,true).isEmpty(),"old Auto provenance + independent preference ON");
  db.exec("INSERT INTO booking_metadata VALUES('fixture/unrelated','KEEP')");
 }
 QString queue(int a,int z,const QString& device=Device){check(store.queue(registry(),plus(a),plus(z),device).isEmpty(),"existing reservation only");return db.value("SELECT id FROM bookings WHERE start=? AND state='QUEUED'",plus(a));}
 ArmQueuedResult arm(const QString& booking,const InitialStartScope& s=scope(),const SourceRegistry& r=registry()){return store.armQueuedIntent(booking,Device,s,r);}
 QString legacy(int a,QChar epoch,bool adopted=false){
  const auto booking=queue(a,a+1);db.exec("CREATE TABLE IF NOT EXISTS automatic_next(current_run TEXT PRIMARY KEY,booking_id TEXT UNIQUE NOT NULL,created_utc TEXT NOT NULL)");db.exec("CREATE TABLE IF NOT EXISTS automatic_adoptions(booking_id TEXT PRIMARY KEY,created_utc TEXT NOT NULL)");
  db.exec("INSERT INTO dispatch_requests VALUES(?,?,?,?,'ARMED',NULL,NULL,?)",{booking,plus(a),plus(a+1),Device,"UNCHANGED-AUTO-DATE"});db.exec("UPDATE bookings SET state='ARMED' WHERE id=?",{booking});db.exec("INSERT INTO automatic_next VALUES(?,?,?)",{QString(32,epoch),booking,"UNCHANGED-ORIGIN-DATE"});if(adopted)db.exec("INSERT INTO automatic_adoptions VALUES(?,?)",{booking,"UNCHANGED-ADOPTION-DATE"});return booking;
 }
};
void primary(){
 Fixture f;const auto selected=f.queue(8,9),first=f.queue(5,6);const auto count=f.db.value("SELECT count(*) FROM bookings"),born=f.db.value("SELECT created_utc FROM bookings WHERE id=?",selected);const auto result=f.arm(selected);
 check(result.status==ArmQueuedStatus::Armed&&result.dispatch&&result.dispatch->bookingId==selected&&result.dispatch->start==plus(8)&&result.dispatch->end==plus(9)&&result.dispatch->state=="ARMED"&&!result.dispatch->predecessor&&!result.dispatch->run&&!result.replayed,"arm actual existing full block despite current fragment500");
 check(f.db.value("SELECT count(*) FROM bookings")==count&&f.db.value("SELECT created_utc FROM bookings WHERE id=?",selected)==born,"no booking creation or restamped history");check(f.db.value("SELECT state FROM bookings WHERE id=?",selected)=="ARMED"&&f.db.value("SELECT state FROM dispatch_requests WHERE id=?",selected)=="ARMED","local and dispatch armed together");
 check(f.db.value("SELECT state FROM bookings WHERE id=?",Root)=="ACTIVE"&&f.db.value("SELECT state FROM dispatch_requests WHERE id=?",f.oldBooking)=="STARTED","arm never stops, completes or launches actual worker");
 check(f.store.miningSessionMetadata(Device)->mode==MiningSessionMode::ManualReservations&&f.store.automaticAfterManual(Device)==true&&f.db.value("SELECT value FROM booking_metadata WHERE key='fixture/unrelated'")=="KEEP","explicit arm sets Manual only, switch/unrelated metadata preserved");
 const auto before=f.db.dump();const auto replay=f.arm(selected);check(replay.status==ArmQueuedStatus::Armed&&replay.replayed&&f.db.dump()==before,"exact replay has no UPDATE or audit/date churn");
 check(f.store.recordQueuedSourceCompletion(Root,plus(33),addUnsigned(plus(33),"500"),Device,f.oldBooking).isEmpty(),"fixture separately pays SOURCE storage after current fragment");
 const auto next=f.store.prepareNextAtBoundary(Device,{Root,Birth,plus(33),addUnsigned(plus(33),"500"),f.oldBooking},scope(),registry(),{WorkBoundary::Ready,id(20)});check(next.status==NextBoundaryStatus::Prepared&&next.dispatch&&next.dispatch->bookingId==first,"arm is not force-booking override: numeric first remains prepareNext authority");
}
void automaticOrigins(){
 Fixture f;const auto selected=f.legacy(8,'b'),retire=f.legacy(70,'c'),keep=f.legacy(71,'d',true),plain=f.queue(72,73);check(f.arm(plain).status==ArmQueuedStatus::Armed,"explicit plain arm first");
 // First arm retired unadopted origins, including the formerly selected one;
 // use a fresh fixture to test choosing/adopting an automatic origin directly.
 check(f.db.value("SELECT state FROM bookings WHERE id=?",retire)=="RELEASED"&&f.db.value("SELECT state FROM dispatch_requests WHERE id=?",retire)=="CANCELLED"&&f.db.value("SELECT state FROM bookings WHERE id=?",keep)=="ARMED","retire only unadopted ARMED automatic, preserve adopted intent");
 Fixture a;const auto chosen=a.legacy(8,'b'),other=a.legacy(70,'c'),adopted=a.legacy(71,'d',true);const auto count=a.db.value("SELECT count(*) FROM bookings");const auto r=a.arm(chosen);check(r.status==ArmQueuedStatus::Armed&&!r.replayed,"explicit automatic-origin adoption accepted");
 check(a.db.value("SELECT booking_id FROM automatic_adoptions WHERE booking_id=?",chosen)==chosen&&a.db.value("SELECT state FROM bookings WHERE id=?",chosen)=="ARMED"&&a.db.value("SELECT state FROM bookings WHERE id=?",other)=="RELEASED"&&a.db.value("SELECT state FROM bookings WHERE id=?",adopted)=="ARMED","adopt selected, retire only eligible other");
 check(a.db.value("SELECT count(*) FROM bookings")==count&&a.db.value("SELECT created_utc FROM automatic_next WHERE booking_id=?",other)=="UNCHANGED-ORIGIN-DATE"&&a.db.value("SELECT created_utc FROM automatic_adoptions WHERE booking_id=?",adopted)=="UNCHANGED-ADOPTION-DATE","origin/history/date rows retained");const auto before=a.db.dump();check(a.arm(chosen).replayed&&a.db.dump()==before,"adoption replay exact no audit churn");
 Fixture p;const auto target=p.queue(8,9),claimed=p.legacy(70,'b');p.db.exec("UPDATE dispatch_requests SET state='PREPARED',predecessor=? WHERE id=?",{Root,claimed});p.db.exec("UPDATE bookings SET state='DISPATCHING' WHERE id=?",{claimed});check(p.arm(target).status==ArmQueuedStatus::Armed&&p.db.value("SELECT state FROM dispatch_requests WHERE id=?",claimed)=="PREPARED"&&p.db.value("SELECT state FROM bookings WHERE id=?",claimed)=="DISPATCHING","never retire physical PREPARED automatic work");
}
void invalid(){
 using Poison=std::function<void(Fixture&,const QString&)>;const QList<Poison> poisons={
  [](auto& f,const auto& b){f.db.exec("UPDATE bookings SET device=? WHERE id=?",{Other,b});},
  [](auto& f,const auto& b){f.db.exec("UPDATE bookings SET kind='RUNTIME' WHERE id=?",{b});},
  [](auto& f,const auto& b){f.db.exec("UPDATE bookings SET state='DISPATCHING' WHERE id=?",{b});},
  [](auto& f,const auto& b){f.db.exec("UPDATE bookings SET start_key='corrupt' WHERE id=?",{b});},
  [](auto& f,const auto& b){f.db.exec("UPDATE bookings SET start=CAST(start AS BLOB) WHERE id=?",{b});},
  [](auto& f,const auto& b){f.db.exec("INSERT INTO dispatch_requests VALUES(?,?,?,?,'ARMED',NULL,NULL,'UNCHANGED')",{b,plus(7),plus(9),Device});},
  [](auto& f,const auto& b){f.db.exec("INSERT INTO dispatch_requests VALUES(?,?,?,?,'PREPARED',NULL,NULL,'UNCHANGED')",{b,plus(8),plus(9),Device});},
  [](auto& f,const auto& b){f.db.exec("INSERT INTO dispatch_requests VALUES(?,?,?,?,'ARMED',?,NULL,'UNCHANGED')",{b,plus(8),plus(9),Device,Root});},
  [](auto& f,const auto& b){f.db.exec("INSERT INTO dispatch_requests VALUES(?,?,?,?,'ARMED',NULL,?,'UNCHANGED')",{b,plus(8),plus(9),Device,QString(32,'b')});},
  [](auto& f,const auto&){f.db.exec("UPDATE booking_metadata SET value='unknown' WHERE key LIKE 'mining-session-mode/v1/%'");},
  [](auto& f,const auto&){f.db.exec("UPDATE booking_metadata SET value='FALSE' WHERE key LIKE 'mining-auto-after-manual/v1/%'");},
  [](auto& f,const auto&){f.db.exec("INSERT INTO bookings VALUES(?,?,?,?,?,'SOURCE_FINISHED','RUNTIME',?,'UNCHANGED')",{QString(32,'e'),plus(8),plus(9),key(plus(8)),key(plus(9)),Other});}
 };
 for(const auto& poison:poisons){Fixture f;const auto selected=f.queue(8,9);poison(f,selected);const auto before=f.db.dump();const auto r=f.arm(selected);check(r.status==ArmQueuedStatus::Held&&!r.dispatch&&!r.error.isEmpty()&&f.db.dump()==before,"corrupt/claimed/overlap input Held with no partial mutation");}
 for(int mode=0;mode<6;++mode){Fixture f;const auto selected=f.queue(8,9);auto s=scope();auto r=registry();if(mode==0)s.blockWidth="500";if(mode==1)s.executionEnd=plus(8);if(mode==2)s.blockWidth="0";if(mode==3)r.ranges[0].status="Complete";if(mode==4)r.sha256="bad";if(mode==5)s.blockWidth="01000000000000000";const auto before=f.db.dump();check(f.arm(selected,s,r).status==ArmQueuedStatus::Held&&f.db.dump()==before,"capacity/scope/registry must be explicit exact and valid");}
 {Fixture f;const auto selected=f.queue(8,9),legacy=f.legacy(70,'b');f.db.exec("UPDATE bookings SET end_key='corrupt' WHERE id=?",{legacy});const auto before=f.db.dump();check(f.arm(selected).status==ArmQueuedStatus::Held&&f.db.dump()==before,"retirement prevalidation cannot erase corrupt automatic origin");}
 {Fixture f;const auto selected=f.queue(8,9);f.legacy(70,'b');f.db.exec("CREATE TRIGGER fixture_fail BEFORE UPDATE OF state ON bookings WHEN NEW.state='ARMED' BEGIN SELECT RAISE(ABORT,'TESTONLY arm failure'); END");const auto before=f.db.dump();check(f.arm(selected).status==ArmQueuedStatus::Held&&f.db.dump()==before,"trigger abort rolls back retirement/adoption/dispatch/mode atomically");}
 {Fixture f;const auto selected=f.legacy(8,'b');f.legacy(70,'c');f.db.exec("CREATE TRIGGER fixture_fail BEFORE UPDATE ON booking_metadata WHEN NEW.value='manual-reservations' BEGIN SELECT RAISE(ABORT,'TESTONLY mode failure'); END");const auto before=f.db.dump();check(f.arm(selected).status==ArmQueuedStatus::Held&&f.db.dump()==before,"late mode-trigger failure rolls back selected adoption and other retirement together");}
 {Fixture f;const auto selected=f.queue(8,9),old=f.legacy(70,'b');f.db.exec("DELETE FROM dispatch_requests WHERE id=?",{old});const auto before=f.db.dump();check(f.arm(selected).status==ArmQueuedStatus::Held&&f.db.dump()==before,"orphan automatic ARMED cannot be silently skipped or retired");}
 {Fixture f;const auto selected=f.queue(8,9);f.db.exec("CREATE TABLE automatic_claims(booking_id TEXT PRIMARY KEY,predecessor TEXT NOT NULL,created_utc TEXT NOT NULL)");f.db.exec("INSERT INTO automatic_claims VALUES(?,?,'UNCHANGED')",{selected,Root});const auto before=f.db.dump();check(f.arm(selected).status==ArmQueuedStatus::Held&&f.db.dump()==before,"claimed automatic identity is not adoptable legacy ARMED lookahead");}
 {Fixture f;const auto selected=f.legacy(8,'b');f.db.exec("UPDATE automatic_next SET booking_id=CAST(booking_id AS BLOB)");const auto before=f.db.dump();check(f.arm(selected).status==ArmQueuedStatus::Held&&f.db.dump()==before,"automatic origin byte/type ambiguity cannot become an adoption");}
 {Fixture f;const auto selected=f.queue(8,9);f.db.exec("UPDATE bookings SET state='ARMED' WHERE id=?",{selected});f.db.exec("INSERT INTO dispatch_requests VALUES(?,?,?,?,'ARMED',NULL,NULL,'UNCHANGED')",{selected,plus(8),plus(9),Device.toLower()});const auto before=f.db.dump();const auto result=f.arm(selected);check(result.status==ArmQueuedStatus::Held&&result.error.contains("Existing arm dispatch identity/state cannot be rebound")&&f.db.dump()==before,"raw noncanonical dispatch device rejected during prevalidation, not late normalized readback");}
 {Fixture f;const auto before=f.db.dump();check(f.arm("bad").status==ArmQueuedStatus::Held&&f.arm(id(999)).status==ArmQueuedStatus::Held&&f.db.dump()==before,"unknown/malformed identity never creates a booking");}
 {Fixture f;const QString boundary="202914184810805067776",a=subtractUnsigned(boundary,"500"),z=addUnsigned(boundary,"500"),booking=id(500);auto r=registry();r.ranges={{1000,a,z,"Open",{}}};InitialStartScope s{a,z,a,z,a,"1000"};f.db.exec("INSERT INTO bookings VALUES(?,?,?,?,?,'QUEUED','LOCAL_INTENT',?,'UNCHANGED')",{booking,a,z,key(a),key(z),Device});const auto before=f.db.dump();check(f.arm(booking,s,r).status==ArmQueuedStatus::Held&&f.db.dump()==before,"qualified capacity cannot cross exact low64 ownership window");}
 {Fixture f;const auto selected=f.queue(8,9);f.db.exec("DELETE FROM booking_metadata WHERE key LIKE 'mining-%'");check(f.arm(selected).status==ArmQueuedStatus::Armed&&f.store.miningSessionMetadata(Device)->mode==MiningSessionMode::ManualReservations&&f.store.automaticAfterManual(Device)==false,"missing defaults become explicit Manual only");check(f.db.value("SELECT count(*) FROM booking_metadata WHERE key LIKE 'mining-auto-after-manual/v1/%'")=="0","do not insert or erase independent switch default row");}
}
void cold(){QTemporaryDir t;const auto file=t.filePath("cold.sqlite");BookingStore store(file);check(t.isValid()&&store.valid()&&store.queue(registry(),plus(8),plus(9),Device).isEmpty(),"cold existing reservation");Sql db(file);const auto booking=db.value("SELECT id FROM bookings");check(db.value("SELECT count(*) FROM sqlite_master WHERE name='dispatch_requests'")=="0","cold fixture has no dispatch table");const auto r=store.armQueuedIntent(booking,Device,scope(),registry());check(r.status==ArmQueuedStatus::Armed&&db.value("SELECT count(*) FROM bookings")=="1"&&db.value("SELECT count(*) FROM bookings WHERE kind='RUNTIME'")=="0"&&db.value("SELECT state||':'||coalesce(run,'NULL')||':'||coalesce(predecessor,'NULL') FROM dispatch_requests WHERE id=?",booking)=="ARMED:NULL:NULL","cold arm creates dispatch only, never RUNTIME/worker/future reservation");const auto before=db.dump();BookingStore reopened(file);check(reopened.armQueuedIntent(booking,Device,scope(),registry()).replayed&&db.dump()==before,"reopen exact arm idempotent without timestamp/audit mutation");}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{primary();automaticOrigins();invalid();cold();std::cout<<"ARM_QUEUED_INTENT "<<checks<<" checks / 0 failures; CLOSED tempSQLite only, arm is not launch\n";return 0;}catch(const std::exception& e){std::cerr<<"ARM_QUEUED_INTENT failed check "<<checks<<": "<<e.what()<<'\n';return 1;}}

#include "booking/booking_store.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>

using namespace pgn::booking;using namespace pgn::mining;
namespace {
int checks{};
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",OtherDevice="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265";
const QString First="199932000000000000000",Last="199933000000000000000",Run(32,'a'),OtherRun(32,'b');
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
QString sk(const QString& value){return QString::number(value.size()).rightJustified(3,'0')+value;}
class Database {
public:
 sqlite3* db{};
 explicit Database(const QString& file){if(sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)throw std::runtime_error("own SQLite open");}
 ~Database(){if(db)sqlite3_close(db);}
 void sql(const QByteArray& text){if(sqlite3_exec(db,text.constData(),nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
 void exec(const char* text,const QStringList& values){sqlite3_stmt* q{};if(sqlite3_prepare_v2(db,text,-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture prepare");
  for(int i=0;i<values.size();++i){const auto raw=values[i].toUtf8();sqlite3_bind_text(q,i+1,raw.constData(),raw.size(),SQLITE_TRANSIENT);}const int rc=sqlite3_step(q);sqlite3_finalize(q);if(rc!=SQLITE_DONE)throw std::runtime_error("fixture write");}
 QByteArray dump(const char* text){sqlite3_stmt* q{};if(sqlite3_prepare_v2(db,text,-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("fixture dump prepare");QByteArray out;int rc;
  while((rc=sqlite3_step(q))==SQLITE_ROW){for(int c=0;c<sqlite3_column_count(q);++c){out+=QByteArray::number(sqlite3_column_type(q,c))+":";out+=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(q,c)),sqlite3_column_bytes(q,c)).toHex()+";";}out+='\n';}sqlite3_finalize(q);if(rc!=SQLITE_DONE)throw std::runtime_error("fixture dump step");return out;}
 bool table(){return !dump("SELECT name FROM sqlite_master WHERE type='table' AND name='dispatch_requests'").isEmpty();}
 QByteArray snapshot(){return dump("SELECT type,name,sql FROM sqlite_master ORDER BY name")+dump("SELECT * FROM bookings ORDER BY id")+dump("SELECT * FROM booking_events ORDER BY sequence")+dump("SELECT * FROM sqlite_sequence ORDER BY name")+dump("SELECT * FROM booking_metadata ORDER BY key")+dump("SELECT * FROM queue_zone_order ORDER BY zone_start")+dump("SELECT * FROM booking_derivations ORDER BY child_id")+(table()?dump("SELECT * FROM dispatch_requests ORDER BY id"):QByteArray{});}
};
struct Fixture {
 QTemporaryDir temp;QString file;BookingStore store;Database db;QString intent;
 Fixture():file(temp.filePath("local-bookings.sqlite")),store(file),db(file){check(temp.isValid()&&store.valid(),"own temp real SQLite store");db.sql("INSERT INTO booking_metadata VALUES('fixture/unrelated','KEEP');");}
 void seed(const QString& dispatchState="STARTED",const std::optional<QString>& mapped=Run,const QString& physical="PAUSED",const QString& local="DISPATCHED"){
  SourceRegistry registry;registry.url="https://PUBLIC-FIXTURE.invalid/ranges";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString("2026-10-14T00:00:00Z",Qt::ISODate);registry.ranges={{1000,First,Last,"Open",{}}};
  check(store.queue(registry,First,Last,Device).isEmpty(),"real LOCAL_INTENT reservation");
  InitialStartScope scope{First,Last,First,Last,First,subtractUnsigned(Last,First)};
  auto ready=store.prepareInitialStart("11111111-2222-4333-8444-555555555555",Device,scope,registry,{WorkBoundary::Ready,"12345678-1234-4567-89ab-123456789abc"});
  check(ready.status==InitialStartStatus::Prepared&&ready.dispatch.has_value(),"real atomic INITIAL_START PREPARED, no launcher");intent=ready.dispatch->bookingId;
  db.exec("UPDATE bookings SET state=? WHERE id=?",{local,intent});
  db.exec("UPDATE dispatch_requests SET state=?,run=? WHERE id=?",{dispatchState,mapped.value_or(QString{}),intent});
  if(!mapped)db.exec("UPDATE dispatch_requests SET run=NULL WHERE id=?",{intent});
  if(physical=="ACTIVE"||physical=="PAUSED")check(store.observeRuntime(Run,First,Last,Device,physical=="ACTIVE").isEmpty(),"real runtime observation storage only, not physical process");
  else if(physical=="SOURCE_FINISHED")check(store.recordSourceCompletion(Run,First,Last,Device).isEmpty(),"reproduce old runtime-only completion hole");
 }
 QString complete(const std::optional<QString>& id){return store.recordQueuedSourceCompletion(Run,First,Last,Device,id);}
 void finished(){
  check(db.dump("SELECT state FROM bookings ORDER BY id").count("534f555243455f46494e4953484544")==2,"both physical and LOCAL_INTENT SOURCE_FINISHED");
  const auto before=db.snapshot();db.sql("CREATE TRIGGER no_booking_update BEFORE UPDATE ON bookings BEGIN SELECT RAISE(FAIL,'replay UPDATE'); END;CREATE TRIGGER no_dispatch_update BEFORE UPDATE ON dispatch_requests BEGIN SELECT RAISE(FAIL,'replay UPDATE'); END;");
  const auto locked=db.snapshot();check(complete(intent).isEmpty(),"exact completed replay succeeds with UPDATE forbidden");check(db.snapshot()==locked,"replay preserves events, metadata, schema, sequence and rows");
  {BookingStore reopened(file);check(reopened.valid()&&reopened.recordQueuedSourceCompletion(Run,First,Last,Device,intent).isEmpty(),"close/reopen handle exact replay, no forward fixture state");}
  check(db.snapshot()==locked,"reopened exact replay no UPDATE");(void)before;
 }
};
void refused(Fixture& f,const QString& run=Run,const QString& first=First,const QString& last=Last,const QString& device=Device,const std::optional<QString>& id={}){
 const auto before=f.db.snapshot();const auto error=f.store.recordQueuedSourceCompletion(run,first,last,device,id);check(!error.isEmpty()&&error.size()<=512,"refusal is explicit and bounded");check(f.db.snapshot()==before,"refusal preserves ALL local tables/audit/sequence/schema");
}
void positive(){
 for(const auto& physical:QStringList{"","ACTIVE","PAUSED","SOURCE_FINISHED"}){
  Fixture f;f.seed("STARTED",Run,physical);check(f.complete(f.intent).isEmpty(),"complete runtime absent/active/paused/previously SOURCE atomically");f.finished();
 }
 {Fixture f;f.seed("PREPARED",std::nullopt,"","DISPATCHING");check(f.complete(f.intent).isEmpty(),"explicit caller-validated PREPARED NULL run lost-ACK may bind");f.finished();}
 {Fixture f;f.seed("PREPARED",Run,"PAUSED","DISPATCHING");check(f.complete(f.intent).isEmpty(),"PREPARED known exact run may complete");f.finished();}
 {Fixture f;f.seed("SOURCE_FINISHED",Run,"PAUSED","SOURCE_FINISHED");check(f.complete(f.intent).isEmpty(),"physical projection repaired only with exact completed intent/run");f.finished();}
 {Fixture f;check(f.store.recordQueuedSourceCompletion(Run,First,Last,Device,std::nullopt).isEmpty(),"nonqueued runtime may be inserted without creating dispatch table");check(!f.db.table(),"old schema preserved, no extra queue/table");const auto before=f.db.snapshot();check(f.store.recordQueuedSourceCompletion(Run,First,Last,Device,std::nullopt).isEmpty()&&f.db.snapshot()==before,"nonqueued exact replay no UPDATE");}
}
void nextInitial(){
 Fixture f;f.seed();check(f.complete(f.intent).isEmpty(),"SOURCE projection closes blocking DISPATCHED intent");
 const auto nextEnd=addUnsigned(Last,subtractUnsigned(Last,First));
 SourceRegistry registry;registry.url="https://PUBLIC-FIXTURE.invalid/ranges";registry.sha256=QString(64,'2');registry.observedUtc=QDateTime::fromString("2026-10-14T01:00:00Z",Qt::ISODate);registry.ranges={{1000,First,nextEnd,"Open",{}}};
 InitialStartScope scope{First,nextEnd,First,nextEnd,First,subtractUnsigned(Last,First)};
 const auto result=f.store.prepareInitialStart("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee",Device,scope,registry,{WorkBoundary::Ready,"87654321-1234-4567-89ab-123456789abc"});
 check(result.status==InitialStartStatus::Prepared&&result.dispatch&&result.dispatch->start==Last&&result.dispatch->end==nextEnd&&result.dispatch->automatic==std::optional<bool>(true),"actual same-store next INITIAL_START selects next whole free block, no launcher");
 check(f.db.dump("SELECT state FROM bookings WHERE state='SOURCE_FINISHED'").count("534f555243455f46494e4953484544")==2,"next prepare preserves both completed historical identities");
}
void wrongTuples(){
 Fixture f;f.seed();refused(f); // mapped run needs explicit queued id, never infer by overlap.
 refused(f,OtherRun,First,Last,Device,f.intent);refused(f,Run,addUnsigned(First,"1"),Last,Device,f.intent);
 refused(f,Run,First,addUnsigned(Last,"1"),Device,f.intent);refused(f,Run,First,Last,OtherDevice,f.intent);
 refused(f,Run,First,Last,Device,QString("missing"));refused(f,Run,First,Last,Device,QString{});
 refused(f,Run,First,Last,Device,QString::fromUtf8("bad\0id",6));refused(f,Run,First,Last,Device,Run);
 refused(f,Run.toUpper(),First,Last,Device,f.intent);refused(f,Run,"0"+First,Last,Device,f.intent);
 refused(f,Run,First,First,Device,f.intent);refused(f,Run,First,Last,"not a GPU",f.intent);
 {Fixture missing;refused(missing,Run,First,Last,Device,QString("known-but-missing"));check(!missing.db.table(),"missing queued id never fabricates LOCAL_INTENT or dispatch");}
}
void poison(){
 const QList<QByteArray> statements={
  "UPDATE dispatch_requests SET run=NULL;", "UPDATE dispatch_requests SET run='bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';",
  "UPDATE dispatch_requests SET state='ARMED';", "UPDATE dispatch_requests SET state='CANCELLED';",
  "UPDATE dispatch_requests SET state='UNKNOWN';", "UPDATE bookings SET state='ABORTED' WHERE kind='RUNTIME';",
  "UPDATE bookings SET state='QUEUED' WHERE kind='LOCAL_INTENT';", "UPDATE bookings SET kind='WRONG' WHERE kind='LOCAL_INTENT';",
  "UPDATE bookings SET start_key='BAD' WHERE kind='LOCAL_INTENT';", "UPDATE bookings SET end_key='BAD' WHERE kind='RUNTIME';",
  "UPDATE bookings SET state=CAST(x'504155534544' AS BLOB) WHERE kind='RUNTIME';",
  "UPDATE bookings SET state='PAUSED'||char(0) WHERE kind='RUNTIME';",
  "UPDATE dispatch_requests SET device='GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265';",
  "DELETE FROM bookings WHERE kind='LOCAL_INTENT';", "DROP TABLE dispatch_requests;"
 };
 for(const auto& statement:statements){Fixture f;f.seed();f.db.sql(statement);refused(f,Run,First,Last,Device,f.intent);}
 {Fixture f;f.seed("SOURCE_FINISHED",std::nullopt,"SOURCE_FINISHED","SOURCE_FINISHED");refused(f,Run,First,Last,Device,f.intent);}
 {Fixture f;f.seed();f.db.exec("INSERT INTO dispatch_requests(id,start,end,device,state,predecessor,run,created_utc) VALUES('other',?,?,?,'STARTED',NULL,NULL,'fixture')",{First,Last,Device});f.db.sql("DROP INDEX dispatch_run;UPDATE dispatch_requests SET run='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';");refused(f,Run,First,Last,Device,f.intent);}
 {Fixture f;f.seed();f.db.sql("UPDATE dispatch_requests SET run=CAST('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' AS BLOB);");refused(f);refused(f,Run,First,Last,Device,f.intent);}
}
void rollback(){
 for(int point=0;point<3;++point){Fixture f;f.seed("STARTED",Run,point==0?QString{}:"PAUSED");
  const QByteArray trigger=point==0?"AFTER INSERT ON bookings WHEN NEW.kind='RUNTIME'":point==1?"AFTER UPDATE ON bookings WHEN NEW.kind='LOCAL_INTENT'":"AFTER UPDATE ON dispatch_requests";
  f.db.sql("CREATE TRIGGER fail_completion "+trigger+" BEGIN INSERT INTO booking_metadata VALUES('fixture/AFTERFAIL','rollback'); SELECT RAISE(FAIL,'AFTERFAIL'); END;");refused(f,Run,First,Last,Device,f.intent);
  f.db.sql("DROP TRIGGER fail_completion;");check(f.complete(f.intent).isEmpty(),"retry after rolled-back AFTERFAIL can complete");f.finished();
 }
 {Fixture f;f.seed();Database blocker(f.file);blocker.sql("BEGIN IMMEDIATE;");refused(f,Run,First,Last,Device,f.intent);blocker.sql("ROLLBACK;");check(f.complete(f.intent).isEmpty(),"busy transaction refuses, then exact retry succeeds");}
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{positive();nextInitial();wrongTuples();poison();rollback();std::cout<<"booking queued SOURCE completion checks="<<checks<<" failures=0\n";return 0;}catch(const std::exception& e){std::cerr<<"booking queued SOURCE completion checks="<<checks<<" failure="<<e.what()<<"\n";return 1;}}

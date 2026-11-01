#include "booking/booking_store.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>

using namespace pgn::booking;using namespace pgn::mining;
namespace {
int checks{};
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",OtherDevice="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265";
const QString Old(32,'a'),Next(32,'b'),Third(32,'c');
const QString Initial="11111111-2222-4333-8444-555555555555",Request="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
const QString Observation="12345678-1234-4567-89ab-123456789abc",OtherRequest="87654321-1234-4567-89ab-123456789abc";
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
QString sk(const QString& value){return QString::number(value.size()).rightJustified(3,'0')+value;}
StoredSourceResume checkpoint(const QString& run=Old,quint64 positions=9007199254740993ULL,
        quint64 tests=9007199254741995ULL,quint64 gaps=41,quint64 batches=17){
 StoredSourceResume result;auto& r=result.resume;auto& bytes=result.checkpointBytes;bytes="PGNCG001";
 const quint64 low=100,high=10,length=9007199254749999ULL;
 for(const auto word:{low,high,length,positions,tests,gaps,batches})for(unsigned i=0;i<8;++i)bytes.append(char((word>>(8*i))&255));
 QString first="10";for(int i=0;i<64;++i)first=addUnsigned(first,first);first=addUnsigned(first,QString::number(low));
 r.run=run;r.start=first;r.length=QString::number(length);r.end=addUnsigned(first,r.length);
 r.positions=QString::number(positions);r.nextStart=addUnsigned(first,r.positions);r.remaining=subtractUnsigned(r.length,r.positions);
 r.tests=QString::number(tests);r.gaps=QString::number(gaps);r.batches=QString::number(batches);
 r.counterTimeNs="9007199254742995";r.checkpointLabel="C:/PUBLIC-FIXTURE/current-job/checkpoint";
 r.checkpointSha256=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());return result;
}
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
 bool table(const char* name){sqlite3_stmt* q{};sqlite3_prepare_v2(db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",-1,&q,nullptr);sqlite3_bind_text(q,1,name,-1,SQLITE_STATIC);const bool yes=sqlite3_step(q)==SQLITE_ROW;sqlite3_finalize(q);return yes;}
 QByteArray snapshot(){return dump("SELECT type,name,sql FROM sqlite_master ORDER BY name")+dump("SELECT * FROM bookings ORDER BY id")+dump("SELECT * FROM booking_events ORDER BY sequence")+dump("SELECT * FROM sqlite_sequence ORDER BY name")+dump("SELECT * FROM booking_metadata ORDER BY key")+dump("SELECT * FROM queue_zone_order ORDER BY zone_start")+dump("SELECT * FROM booking_derivations ORDER BY child_id")+(table("dispatch_requests")?dump("SELECT * FROM dispatch_requests ORDER BY id"):QByteArray{})+(table("dispatch_resume_epochs")?dump("SELECT * FROM dispatch_resume_epochs ORDER BY run"):QByteArray{});}
 void epochTable(){sql("CREATE TABLE dispatch_resume_epochs(run TEXT PRIMARY KEY,previous_run TEXT NOT NULL UNIQUE,booking_id TEXT NOT NULL,request_id TEXT NOT NULL UNIQUE,receipt TEXT NOT NULL);");}
};
struct Fixture {
 QTemporaryDir temp;QString file;std::unique_ptr<BookingStore> store;std::unique_ptr<Database> db;StoredSourceResume original{checkpoint()};QString booking;
 Fixture():file(temp.filePath("local-bookings.sqlite")),store(std::make_unique<BookingStore>(file)),db(std::make_unique<Database>(file)){
  check(temp.isValid()&&store->valid(),"own temp real Store/SQLite");check(store->setAutomaticAfterManual(Device,true).isEmpty(),"preference fixture");
  db->sql("INSERT INTO booking_metadata VALUES('fixture/original-raw-and-age','PUBLIC-REFERENCE-UNCHANGED');");
 }
 void seed(){
  const auto& r=original.resume;SourceRegistry registry;registry.url="https://PUBLIC-FIXTURE.invalid/ranges";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString("2026-10-14T00:00:00Z",Qt::ISODate);registry.ranges={{1000,r.start,r.end,"Open",{}}};
  check(store->queue(registry,r.start,r.end,Device).isEmpty(),"real immutable LOCAL_INTENT reservation");
  const auto initial=store->prepareInitialStart(Initial,Device,{r.start,r.end,r.start,r.end,r.start,r.length},registry,{WorkBoundary::Ready,Observation});
  check(initial.status==InitialStartStatus::Prepared&&initial.dispatch.has_value(),"real initial PREPARED, no actual launch");booking=initial.dispatch->bookingId;
  db->exec("UPDATE dispatch_requests SET state='STARTED',run=? WHERE id=?",{Old,booking});db->exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{booking});
  check(store->observeRuntime(Old,r.start,r.end,Device,false).isEmpty(),"store old synthetic runtime PAUSED");
  check(store->saveResumeCheckpoint(Device,r,original.checkpointBytes).isEmpty(),"store original exact64 checkpoint with counters above2^53");
 }
 CheckpointResumeAdmission prepare(const QString& request=Request,const QString& next=Next){return store->prepareCheckpointResume(request,Device,booking,Old,next,original.resume.checkpointSha256,Observation);}
 void reopen(){db.reset();store.reset();store=std::make_unique<BookingStore>(file);db=std::make_unique<Database>(file);check(store->valid(),"close/reopen BOTH real SQLite connections");}
};
void exportPreparedContract(Fixture& f,const CheckpointResumeAdmission& result){
 // TEST ONLY. No default output and no launcher, production DB or process
 // access. Preserve partial export on error; never overwrite/reinitialize it.
 if(!qEnvironmentVariableIsSet("PGN_TEST_RESUME_CONTRACT_EXPORT"))return;
 const auto requested=qEnvironmentVariable("PGN_TEST_RESUME_CONTRACT_EXPORT");
 const QFileInfo info(requested);const QFileInfo parent(info.absolutePath());
 if(requested.isEmpty()||requested.contains(QChar(0))||!info.isAbsolute()||info.fileName().isEmpty()||
    info.exists()||info.isSymLink()||!parent.exists()||!parent.isDir())
  throw std::runtime_error("TEST resume export requires explicit absolute NEW directory and existing parent");
 QDir parentDir(parent.absoluteFilePath());
 if(!parentDir.mkdir(info.fileName()))throw std::runtime_error("TEST resume export directory already exists or creation refused");
 const QDir output(info.absoluteFilePath());
 check(result.status==CheckpointResumeAdmissionStatus::Prepared&&result.epoch&&!result.receiptUtf8.isEmpty(),"export only actual native Prepared admission");
 const auto sourceBefore=f.db->snapshot();
 const auto destination=output.filePath("local-bookings.sqlite");
 {QFile empty(destination);if(!empty.open(QIODevice::WriteOnly|QIODevice::NewOnly))throw std::runtime_error("TEST backup target already exists");}
 sqlite3* raw{};const int opened=sqlite3_open_v2(destination.toUtf8().constData(),&raw,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr);
 std::unique_ptr<sqlite3,decltype(&sqlite3_close)> target(raw,&sqlite3_close);
 if(opened!=SQLITE_OK)throw std::runtime_error("TEST backup destination open failed");
 sqlite3_backup* backup=sqlite3_backup_init(target.get(),"main",f.db->db,"main");
 if(!backup)throw std::runtime_error("TEST sqlite3_backup_init failed");
 const int copied=sqlite3_backup_step(backup,-1),finished=sqlite3_backup_finish(backup);
 check(copied==SQLITE_DONE&&finished==SQLITE_OK,"SQLite backup copies committed actual native DB, not reconstructed rows");
 sqlite3_stmt* query{};const auto& e=*result.epoch;const auto run=e.run.toUtf8();
 if(sqlite3_prepare_v2(target.get(),"SELECT receipt FROM dispatch_resume_epochs WHERE run=?",-1,&query,nullptr)!=SQLITE_OK)throw std::runtime_error("TEST backup receipt query failed");
 sqlite3_bind_text(query,1,run.constData(),run.size(),SQLITE_TRANSIENT);
 const bool row=sqlite3_step(query)==SQLITE_ROW&&sqlite3_column_type(query,0)==SQLITE_TEXT;
 const auto copiedReceipt=row?QByteArray(reinterpret_cast<const char*>(sqlite3_column_text(query,0)),sqlite3_column_bytes(query,0)):QByteArray{};
 sqlite3_finalize(query);check(row&&copiedReceipt==result.receiptUtf8,"backup contains EXACT receiptUtf8 from real admission");
 target.reset();
 const auto writeNew=[&](const QString& name,const QByteArray& bytes){QFile file(output.filePath(name));
  if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)||file.write(bytes)!=bytes.size()||!file.flush())throw std::runtime_error("TEST contract export write failed; partial evidence retained");};
 writeNew("receipt.json",result.receiptUtf8); // no re-encoding, BOM or newline
 const QJsonObject command{{"action","resume_prepared"},{"device",e.device},{"request_id",e.requestUUID},
  {"observation_id",e.observationUUID},{"booking_id",e.bookingId},{"old_run",e.previousRun},{"new_run",e.run},
  {"base_birth","2026-10-14T00:00:00.000Z"},{"base_start",e.checkpoint.resume.start},{"base_end",e.checkpoint.resume.end}};
 check(command.size()==10,"TESTONLY command exactly10keys; synthetic birth is not physical evidence");
 writeNew("command.json",QJsonDocument(command).toJson(QJsonDocument::Compact));
 check(f.db->snapshot()==sourceBefore,"export backup does not modify logical source DB");
 std::cout<<"TESTONLY resume contract export="<<output.absolutePath().toStdString()<<"; synthetic >GPU profile range, decoder fixture only; binding launch MUST refuse\n";
}
template<class Action>void held(Fixture& f,Action action){const auto before=f.db->snapshot();const auto result=action();check(result.status==CheckpointResumeAdmissionStatus::Held&&!result.error.isEmpty()&&result.error.size()<=512&&!result.epoch&&result.receiptUtf8.isEmpty(),"explicit bounded Held, no false epoch");check(f.db->snapshot()==before,"Held preserves all tables, metadata, rows, audit, sequence and DDL");}
template<class Action>void errorUnchanged(Fixture& f,Action action){const auto before=f.db->snapshot();check(!action().isEmpty(),"stale write explicitly refused");check(f.db->snapshot()==before,"stale write changes nothing");}
void positive(){
 Fixture f;f.seed();f.reopen();
 const auto oldRow=f.db->dump("SELECT * FROM bookings WHERE kind='RUNTIME'"),metadata=f.db->dump("SELECT * FROM booking_metadata ORDER BY key"),ids=f.db->dump("SELECT id,created_utc FROM bookings ORDER BY id");
 const auto result=f.prepare();check(result.status==CheckpointResumeAdmissionStatus::Prepared&&result.epoch&&!result.replayed,"new epoch only PREPARED, NOT launched");
 exportPreparedContract(f,result);
 check(f.db->dump("SELECT * FROM bookings WHERE kind='RUNTIME'")==oldRow&&f.db->dump("SELECT id,created_utc FROM bookings ORDER BY id")==ids,"old runtime PAUSED, all immutable ids/dates, no new booking");
 check(f.db->dump("SELECT * FROM booking_metadata ORDER BY key")==metadata,"original checkpoint, raw reference/age, mode and preference unchanged");
 const auto& e=*result.epoch;check(e.previousRun==Old&&e.run==Next&&e.bookingId==f.booking&&e.device==Device&&!e.predecessor,"exact new identity, no invented completed predecessor");
 check(e.checkpoint.checkpointBytes==f.original.checkpointBytes&&e.checkpoint.resume.positions=="9007199254740993"&&e.checkpoint.resume.tests=="9007199254741995"&&e.checkpoint.resume.counterTimeNs==f.original.resume.counterTimeNs,"all64 bytes and exact cumulative baselines retained, no new clock");
 check(e.rawConservationRequirement=="LINEAGE_RAW_CONSERVATION_REQUIRED","raw conservation is an unpaid explicit obligation");
 const auto doc=QJsonDocument::fromJson(result.receiptUtf8);const auto o=doc.object();
 check(result.receiptUtf8.size()<=24576&&doc.toJson(QJsonDocument::Compact)==result.receiptUtf8&&o.size()==12&&o.value("schema")=="pgn-checkpoint-resume-epoch-1"&&o.value("origin")=="RESUME_CHECKPOINT","versioned canonical exact transport receipt");
 const auto cp=o.value("checkpoint").toObject();check(cp.size()==4&&cp.value("resume").toObject().size()==13&&cp.value("checkpoint_base64").toString().toLatin1()==f.original.checkpointBytes.toBase64()&&o.value("counter_basis")=="PGNCG001_CUMULATIVE_BASELINE","encoded exact checkpoint and original13strings, never float");
 f.reopen();const auto saved=f.store->savedCheckpointResumeEpoch(Device,Next);check(saved.status==CheckpointResumeReadStatus::Present&&saved.epoch&&saved.receiptUtf8==result.receiptUtf8,"epoch durably readable after connection close/reopen");
 f.db->sql("CREATE TRIGGER no_job_update BEFORE UPDATE ON bookings BEGIN SELECT RAISE(FAIL,'replay UPDATE'); END;CREATE TRIGGER no_dispatch_update BEFORE UPDATE ON dispatch_requests BEGIN SELECT RAISE(FAIL,'replay UPDATE'); END;CREATE TRIGGER no_epoch_insert BEFORE INSERT ON dispatch_resume_epochs BEGIN SELECT RAISE(FAIL,'replay INSERT'); END;");
 const auto before=f.db->snapshot();const auto replay=f.prepare();check(replay.status==CheckpointResumeAdmissionStatus::ReconcilePrepared&&replay.replayed&&replay.receiptUtf8==result.receiptUtf8,"same request is reconciliation, never a second unconditional launch");check(f.db->snapshot()==before,"exact replay no UPDATE/INSERT/audit");
 check(f.store->saveResumeCheckpoint(Device,f.original.resume,f.original.checkpointBytes).isEmpty()&&f.db->snapshot()==before,"retired original checkpoint exact replay still no UPDATE");
}
void staleAndConflicts(){
 Fixture f;f.seed();check(f.prepare().status==CheckpointResumeAdmissionStatus::Prepared,"reserve successor");
 const auto& r=f.original.resume;
 errorUnchanged(f,[&]{return f.store->observeRuntime(Old,r.start,r.end,Device,true);});
 errorUnchanged(f,[&]{return f.store->recordSourceCompletion(Old,r.start,r.end,Device);});
 errorUnchanged(f,[&]{return f.store->recordQueuedSourceCompletion(Old,r.start,r.end,Device,f.booking);});
 errorUnchanged(f,[&]{return f.store->recordQueuedSourceCompletion(Old,r.start,r.end,Device,std::nullopt);});
 const auto changed=checkpoint(Old,9007199254740994ULL,9007199254741996ULL);
 errorUnchanged(f,[&]{return f.store->saveResumeCheckpoint(Device,changed.resume,changed.checkpointBytes);});
 held(f,[&]{return f.prepare(OtherRequest,Third);});
 // Synthetic accepted observation only; this does NOT execute a process or
 // prove that it loaded the planned checkpoint. Old observations still cannot
 // pause this new current row or reassign the dispatch's epoch.
 f.db->exec("UPDATE dispatch_requests SET state='STARTED' WHERE id=?",{f.booking});f.db->exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{f.booking});
 check(f.store->observeRuntime(Next,r.start,r.end,Device,true).isEmpty(),"store new observed runtime only after fixture's explicit accepted-observation transition");
 errorUnchanged(f,[&]{return f.store->observeRuntime(Old,r.start,r.end,Device,true);});
 const auto replay=f.prepare();check(replay.status==CheckpointResumeAdmissionStatus::ReconcilePrepared&&replay.replayed,"late exact request reconciles STARTED without changing it");
}
void identities(){
 Fixture f;f.seed();
 for(int i=0;i<11;++i){QString request=Request,device=Device,booking=f.booking,old=Old,next=Next,sha=f.original.resume.checkpointSha256,observation=Observation;
  switch(i){case 0:request=Initial;break;case 1:request="00000000-0000-0000-0000-000000000000";break;case 2:device=OtherDevice;break;case 3:booking=OtherRequest;break;case 4:old=Third;break;case 5:next=Old;break;case 6:next=Next.toUpper();break;case 7:sha=QString(64,'0');break;case 8:observation="unknown";break;case 9:booking+=QChar(0);break;case 10:request=Request.toUpper();break;}
  held(f,[&]{return f.store->prepareCheckpointResume(request,device,booking,old,next,sha,observation);});
 }
 check(f.prepare().status==CheckpointResumeAdmissionStatus::Prepared,"identity fixture accepted once");
 held(f,[&]{return f.store->prepareCheckpointResume(Request,Device,f.booking,Old,Next,f.original.resume.checkpointSha256,OtherRequest);});
 const auto before=f.db->snapshot();const auto read=f.store->savedCheckpointResumeEpoch(OtherDevice,Next);check(read.status==CheckpointResumeReadStatus::Unavailable&&!read.error.isEmpty()&&f.db->snapshot()==before,"cross-device read not an absent/default epoch");
}
void poison(){
 const QList<QByteArray> statements={
  "UPDATE dispatch_requests SET run=NULL;", "UPDATE dispatch_requests SET run='cccccccccccccccccccccccccccccccc';", "UPDATE dispatch_requests SET state='PREPARED';", "UPDATE dispatch_requests SET state='SOURCE_FINISHED';",
  "UPDATE bookings SET state='ACTIVE' WHERE kind='RUNTIME';", "UPDATE bookings SET state='SOURCE_FINISHED' WHERE kind='RUNTIME';", "DELETE FROM bookings WHERE kind='RUNTIME';",
  "UPDATE bookings SET start_key='BAD' WHERE kind='RUNTIME';", "UPDATE bookings SET state=CAST('PAUSED' AS BLOB) WHERE kind='RUNTIME';", "UPDATE bookings SET state='PAUSED'||char(0) WHERE kind='RUNTIME';",
  "UPDATE bookings SET state='DISPATCHING' WHERE kind='LOCAL_INTENT';", "UPDATE bookings SET kind='WRONG' WHERE kind='LOCAL_INTENT';",
  "UPDATE booking_metadata SET value=CAST(value AS BLOB) WHERE key LIKE 'mining-resume/%';", "DELETE FROM booking_metadata WHERE key LIKE 'mining-resume/%';",
  "UPDATE booking_metadata SET value='UNKNOWN' WHERE key LIKE 'mining-session-mode/%';", "DROP TABLE dispatch_requests;"
 };
 for(const auto& sql:statements){Fixture f;f.seed();f.db->sql(sql);held(f,[&]{return f.prepare();});check(!f.db->table("dispatch_resume_epochs"),"failed legacy admission creates no epoch table");}
 {Fixture f;f.seed();const auto& r=f.original.resume;check(f.store->observeRuntime(Next,r.start,r.end,OtherDevice,false).isEmpty(),"reserve already-used runtime id fixture");held(f,[&]{return f.prepare();});}
 {Fixture f;f.seed();f.db->exec("INSERT INTO dispatch_requests VALUES(?,?,?,?,'PREPARED',NULL,NULL,'fixture')",{OtherRequest,f.original.resume.start,f.original.resume.end,Device});held(f,[&]{return f.prepare();});}
}
void readsAndRollback(){
 {Fixture f;const auto before=f.db->snapshot();const auto absent=f.store->savedCheckpointResumeEpoch(Device,Next);check(absent.status==CheckpointResumeReadStatus::Absent&&!absent.epoch&&absent.error.isEmpty()&&f.db->snapshot()==before,"absent legacy read creates nothing, not zero");}
 for(int point=0;point<3;++point){Fixture f;f.seed();
  QByteArray target;
  if(point==0){f.db->epochTable();target="AFTER INSERT ON dispatch_resume_epochs";}
  else target=point==1?"AFTER UPDATE ON dispatch_requests":"AFTER UPDATE ON bookings WHEN NEW.kind='LOCAL_INTENT'";
  f.db->sql("CREATE TRIGGER fail_resume "+target+" BEGIN INSERT INTO booking_metadata VALUES('fixture/AFTERFAIL','rollback'); SELECT RAISE(FAIL,'AFTERFAIL'); END;");
  held(f,[&]{return f.prepare();});if(point!=0)check(!f.db->table("dispatch_resume_epochs"),"rollback includes newly created epoch table");
  f.db->sql("DROP TRIGGER fail_resume;");check(f.prepare().status==CheckpointResumeAdmissionStatus::Prepared,"retry only after full rollback can reserve epoch");
 }
 {Fixture f;f.seed();Database blocker(f.file);blocker.sql("BEGIN IMMEDIATE;");held(f,[&]{return f.prepare();});blocker.sql("ROLLBACK;");check(f.prepare().status==CheckpointResumeAdmissionStatus::Prepared,"busy reservation refuses then retry succeeds");}
 for(int poison=0;poison<4;++poison){Fixture f;f.seed();check(f.prepare().status==CheckpointResumeAdmissionStatus::Prepared,"receipt poison fixture");
  switch(poison){case 0:f.db->sql("UPDATE dispatch_resume_epochs SET receipt=receipt||' ';");break;case 1:f.db->sql("UPDATE dispatch_resume_epochs SET receipt=CAST(receipt AS BLOB);");break;case 2:f.db->sql("UPDATE dispatch_resume_epochs SET receipt=replace(receipt,'LINEAGE_RAW_CONSERVATION_REQUIRED','READY');");break;case 3:f.db->sql("UPDATE dispatch_resume_epochs SET previous_run='cccccccccccccccccccccccccccccccc';");break;}
  const auto before=f.db->snapshot();const auto read=f.store->savedCheckpointResumeEpoch(Device,Next);check(read.status==CheckpointResumeReadStatus::Unavailable&&!read.error.isEmpty()&&!read.epoch&&f.db->snapshot()==before,"corrupt/mismatched receipt explicitly unavailable without mutation");held(f,[&]{return f.prepare();});
 }
}
void cumulativeFloorIsNotProof(){
 Fixture f;f.seed();check(f.prepare().status==CheckpointResumeAdmissionStatus::Prepared,"baseline epoch");const auto& r=f.original.resume;
 f.db->exec("UPDATE dispatch_requests SET state='STARTED' WHERE id=?",{f.booking});f.db->exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{f.booking});
 check(f.store->observeRuntime(Next,r.start,r.end,Device,false).isEmpty(),"synthetic next PAUSED observation, no physical execution");
 const auto delta=checkpoint(Next,123,456,1,1);check(f.store->saveResumeCheckpoint(Device,delta.resume,delta.checkpointBytes).isEmpty(),"byte-valid per-run delta fixture is not by itself lineage admission");
 held(f,[&]{return f.store->prepareCheckpointResume(OtherRequest,Device,f.booking,Next,Third,delta.resume.checkpointSha256,Observation);});
 // The floor can refute a counter reset. Values >= baseline cannot establish
 // cumulative semantics by themselves. Final conservation MUST retain old raw
 // cursors and account for new-row DELTAS, and the launch receipt must attest
 // the actual checkpoint SHA loaded by the correct producer. No such launch,
 // SOURCE full-completion or lineage-conservation proof is executed here.
}
QJsonObject sourceManifestFixture(const Fixture& f,const QString& run,bool resumed=false){
 const auto& r=f.original.resume;
 QJsonObject manifest{{"schema","pgn-native-process-1"},{"run",run},{"uuid",Device},
  {"pid",1234},{"birth","2026-10-14T00:00:00.000Z"},{"start",r.start},{"end",r.end},
  {"queued_intent",f.booking},{"stdout","C:/PUBLIC-FIXTURE/"+run+".stdout"}};
 if(resumed){manifest.insert("resume_previous_run",Old);manifest.insert("checkpoint_input_sha256",r.checkpointSha256);}
 return manifest;
}
void completedResumeFixture(Fixture& f){
 f.seed();const auto& r=f.original.resume;
 check(f.store->saveSourceRunManifest(Device,sourceManifestFixture(f,Old)).isEmpty(),"fixture original actual-shape manifest before retirement");
 check(f.prepare().status==CheckpointResumeAdmissionStatus::Prepared,"fixture reserve exact resumed epoch");
 f.db->exec("UPDATE dispatch_requests SET state='STARTED' WHERE id=?",{f.booking});
 f.db->exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{f.booking});
 check(f.store->observeRuntime(Next,r.start,r.end,Device,false).isEmpty(),"fixture resumed observation preserves original PAUSED row");
 check(f.store->saveSourceRunManifest(Device,sourceManifestFixture(f,Next,true)).isEmpty(),"fixture immutable resumed manifest matches epoch");
 // This is a synthetic caller-admitted SOURCE DTO, not execution of the raw
 // lineage verifier, remote process, checkpoint load or primality proof.
 check(f.store->recordQueuedSourceCompletion(Next,r.start,r.end,Device,f.booking).isEmpty(),"fixture native SOURCE transaction for CURRENT resumed run only");
}
InitialStartResult nextInitial(Fixture& f){
 const auto& r=f.original.resume;const auto end=addUnsigned(r.end,r.length);
 SourceRegistry registry;registry.url="https://PUBLIC-FIXTURE.invalid/ranges";registry.sha256=QString(64,'2');
 registry.observedUtc=QDateTime::fromString("2026-10-14T00:00:00Z",Qt::ISODate);registry.ranges={{1000,r.start,end,"Open",{}}};
 return f.store->prepareInitialStart("22222222-3333-4444-8555-666666666666",Device,
  {r.start,end,r.start,end,r.start,r.length},registry,{WorkBoundary::Ready,Observation});
}
QByteArray oldPausedEvidence(Fixture& f){
 return f.db->dump("SELECT * FROM bookings WHERE id='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'")+
  f.db->dump("SELECT * FROM booking_events WHERE booking_id='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' ORDER BY sequence")+
  f.db->dump("SELECT * FROM booking_metadata WHERE key LIKE '%aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' OR key='fixture/original-raw-and-age' ORDER BY key");
}
void coveredPausedInitialStart(){
 {
  Fixture f;completedResumeFixture(f);f.reopen();const auto old=oldPausedEvidence(f);
  check(f.store->active().isEmpty(),"completed resume ancestor is history, not a ghost active job");
  const auto result=nextInitial(f);const auto& r=f.original.resume;
  check(result.status==InitialStartStatus::Prepared&&result.dispatch&&result.dispatch->automatic==true&&
   result.dispatch->start==r.end&&result.dispatch->end==addUnsigned(r.end,r.length),"A PAUSED -> B SOURCE -> next explicit INITIAL picks next ENTIRE block");
  check(oldPausedEvidence(f)==old,"covered PAUSED ancestor row/checkpoint/original time/raw reference/events retained byte-exact");
  const auto visible=f.store->active();
  check(visible.size()==1&&visible.first().id==result.dispatch->bookingId,"new current job remains visible without its retired PAUSED ancestor");
  check(f.db->dump("SELECT state FROM bookings WHERE id='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'")=="3:504155534544;\n","A remains PAUSED, never falsely SOURCE_FINISHED");
  check(f.store->recordQueuedSourceCompletion(Old,r.start,r.end,Device,f.booking).size()>0,"retired A still cannot claim SOURCE after next admission");
 }
 for(int poison=0;poison<10;++poison){
  Fixture f;completedResumeFixture(f);
  switch(poison){
   case 0:check(f.store->observeRuntime(Third,f.original.resume.start,f.original.resume.end,Device,false).isEmpty(),"unrelated PAUSED fixture");break;
   case 1:f.db->sql("UPDATE dispatch_resume_epochs SET receipt=receipt||' ';");break;
   case 2:f.db->sql("DELETE FROM dispatch_resume_epochs;");break;
   case 3:f.db->exec("DELETE FROM bookings WHERE id=?",{Next});break;
   case 4:f.db->exec("DELETE FROM booking_metadata WHERE key=?",{"mining-source-manifest/v1/"+Next});break;
   case 5:f.db->exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{f.booking});break;
   case 6:f.db->exec("UPDATE dispatch_requests SET state='STARTED' WHERE id=?",{f.booking});
          f.db->exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{f.booking});break;
   case 7:f.db->exec("UPDATE bookings SET device=? WHERE id=?",{OtherDevice,Next});break;
   case 8:f.db->exec("UPDATE bookings SET start='1',start_key='0011' WHERE id=?",{Next});break;
   case 9:{
    const auto saved=f.store->savedCheckpointResumeEpoch(Device,Next);check(saved.epoch.has_value(),"cycle poison starts from exact real receipt");
    auto receipt=QJsonDocument::fromJson(saved.receiptUtf8).object();receipt.insert("run",Old);receipt.insert("previous_run",Next);receipt.insert("request_id",OtherRequest);
    auto cp=receipt.value("checkpoint").toObject(),tuple=cp.value("resume").toObject();tuple.insert("run",Next);cp.insert("resume",tuple);receipt.insert("checkpoint",cp);
    f.db->exec("INSERT INTO dispatch_resume_epochs VALUES(?,?,?,?,?)",{Old,Next,f.booking,OtherRequest,QString::fromUtf8(QJsonDocument(receipt).toJson(QJsonDocument::Compact))});
    auto manifest=sourceManifestFixture(f,Old);manifest.insert("resume_previous_run",Next);manifest.insert("checkpoint_input_sha256",f.original.resume.checkpointSha256);
    f.db->exec("UPDATE booking_metadata SET value=? WHERE key=?",{QString::fromUtf8(QJsonDocument(manifest).toJson(QJsonDocument::Compact)),"mining-source-manifest/v1/"+Old});break;
   }
  }
  const auto before=f.db->snapshot(),old=oldPausedEvidence(f);const auto result=nextInitial(f);
  check((result.status==InitialStartStatus::Held||result.status==InitialStartStatus::ResumeRequired)&&!result.error.isEmpty()&&!result.dispatch,
   "unrelated/missing/corrupt/cyclic/contradictory PAUSED ancestry remains explicitly Held, never skipped");
  check(f.db->snapshot()==before&&oldPausedEvidence(f)==old,"refused initial admission preserves ALL rows/epochs/metadata/events/mode");
 }
 {
  Fixture f;completedResumeFixture(f);const auto old=oldPausedEvidence(f);
  f.db->sql("BEGIN IMMEDIATE;");
  for(int i=0;i<4096;++i){const auto start=QString::number(i*2),end=QString::number(i*2+1);
   f.db->exec("INSERT INTO bookings VALUES(?,?,?,?,?,'QUEUED','LOCAL_INTENT',?,'2026-10-14T00:00:00Z')",
    {"capacity-"+QString::number(i),start,end,sk(start),sk(end),OtherDevice});}
  f.db->sql("COMMIT;");
  const auto result=nextInitial(f);
  check(result.status==InitialStartStatus::Prepared&&result.dispatch,"historical PAUSED ancestor does not consume existing4097 operational INITIAL capacity");
  check(oldPausedEvidence(f)==old,"capacity classification is not historical deletion or state normalization");
 }
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{positive();staleAndConflicts();identities();poison();readsAndRollback();cumulativeFloorIsNotProof();coveredPausedInitialStart();std::cout<<"checkpoint resume admission checks="<<checks<<" failures=0; sameSQLite only, no physical resume/conservation proof\n";return 0;}catch(const std::exception& e){std::cerr<<"checkpoint resume admission checks="<<checks<<" failure="<<e.what()<<"\n";return 1;}}

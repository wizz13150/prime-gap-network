#include "booking/booking_store.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
#include <stdexcept>

using namespace pgn::booking;
using namespace pgn::mining;
namespace {
int checks{};
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",OtherDevice="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265";
const QString Root(32,'a'),First="184467440737095516260",Last="184467440737095526260",Birth="2026-10-14T00:00:00.123456789Z";
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
QString metadataKey(const QString& run){return "mining-source-manifest/v1/"+run;}
QString commandId(int i){return "20000000-0000-4000-8000-"+QString::number(i,16).rightJustified(12,'0');}
QString observationId(int i){return "30000000-0000-4000-8000-"+QString::number(i,16).rightJustified(12,'0');}
QString runId(int i){return QString::number(i,16).rightJustified(32,'0');}
class Database {
public:
 sqlite3* db{};
 explicit Database(const QString& file){if(sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)throw std::runtime_error("own temporary SQLite open");}
 ~Database(){if(db)sqlite3_close(db);}
 void sql(const QByteArray& s){if(sqlite3_exec(db,s.constData(),nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
 void exec(const char* sql,const QStringList& args){sqlite3_stmt* q{};if(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("own fixture prepare");for(int i=0;i<args.size();++i){const auto raw=args[i].toUtf8();sqlite3_bind_text(q,i+1,raw.constData(),raw.size(),SQLITE_TRANSIENT);}const auto result=sqlite3_step(q);sqlite3_finalize(q);if(result!=SQLITE_DONE)throw std::runtime_error("own fixture write");}
 QByteArray raw(const QByteArray& sql){sqlite3_stmt* q{};if(sqlite3_prepare_v2(db,sql.constData(),-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("own fixture read");QByteArray result;if(sqlite3_step(q)==SQLITE_ROW)result=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(q,0)),sqlite3_column_bytes(q,0));sqlite3_finalize(q);return result;}
 void metadata(const QString& key,const QByteArray& value,bool blob=false){sqlite3_stmt* q{};sqlite3_prepare_v2(db,"INSERT INTO booking_metadata VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",-1,&q,nullptr);const auto name=key.toUtf8();sqlite3_bind_text(q,1,name.constData(),name.size(),SQLITE_TRANSIENT);if(blob)sqlite3_bind_blob(q,2,value.constData(),value.size(),SQLITE_TRANSIENT);else sqlite3_bind_text(q,2,value.constData(),value.size(),SQLITE_TRANSIENT);const auto result=sqlite3_step(q);sqlite3_finalize(q);if(result!=SQLITE_DONE)throw std::runtime_error("own metadata poison fixture");}
 QByteArray dump(const char* sql){sqlite3_stmt* q{};if(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("own fixture snapshot");QByteArray out;int rc;while((rc=sqlite3_step(q))==SQLITE_ROW){for(int c=0;c<sqlite3_column_count(q);++c){out+=QByteArray::number(sqlite3_column_type(q,c))+":";out+=QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(q,c)),sqlite3_column_bytes(q,c)).toHex()+";";}out+='\n';}sqlite3_finalize(q);if(rc!=SQLITE_DONE)throw std::runtime_error("own snapshot step");return out;}
 bool epochs(){return !raw("SELECT name FROM sqlite_master WHERE type='table' AND name='dispatch_resume_epochs'").isEmpty();}
 QByteArray jobs(){const bool dispatch=!raw("SELECT name FROM sqlite_master WHERE type='table' AND name='dispatch_requests'").isEmpty();return dump("SELECT * FROM bookings ORDER BY id")+dump("SELECT * FROM booking_events ORDER BY sequence")+dump("SELECT * FROM sqlite_sequence ORDER BY name")+(dispatch?dump("SELECT * FROM dispatch_requests ORDER BY id"):QByteArray{});}
 QByteArray snapshot(){return dump("SELECT type,name,sql FROM sqlite_master ORDER BY name")+jobs()+dump("SELECT * FROM booking_metadata ORDER BY key")+dump("SELECT * FROM queue_zone_order ORDER BY zone_start")+dump("SELECT * FROM booking_derivations ORDER BY child_id")+(epochs()?dump("SELECT * FROM dispatch_resume_epochs ORDER BY run"):QByteArray{});}
};
StoredSourceResume checkpoint(const QString& run){
 StoredSourceResume result;result.checkpointBytes="PGNCG001";for(const quint64 word:{100ULL,10ULL,10000ULL,123ULL,9007199254740993ULL,7ULL,2ULL})for(unsigned i=0;i<8;++i)result.checkpointBytes.append(char((word>>(8*i))&255));
 auto& r=result.resume;r.run=run;r.start=First;r.end=Last;r.length="10000";r.positions="123";r.nextStart=addUnsigned(First,"123");r.remaining="9877";r.tests="9007199254740993";r.gaps="7";r.batches="2";r.counterTimeNs="9007199254740995";r.checkpointLabel="C:/PUBLIC-FIXTURE/current-job/checkpoint";r.checkpointSha256=QString::fromLatin1(QCryptographicHash::hash(result.checkpointBytes,QCryptographicHash::Sha256).toHex());return result;
}
struct Fixture {
 QTemporaryDir temp;QString file;BookingStore store;Database db;QString booking;
 Fixture():file(temp.filePath("local-bookings.sqlite")),store(file),db(file){
  check(temp.isValid()&&store.valid(),"own actual BookingStore temporary SQLite");SourceRegistry source;source.url="https://PUBLIC-FIXTURE.invalid/source";source.sha256=QString(64,'1');source.observedUtc=QDateTime::fromString("2026-10-14T00:00:00Z",Qt::ISODate);source.ranges={{1,First,Last,"Open",{}}};
  check(store.queue(source,First,Last,Device).isEmpty(),"real local intent fixture");
  const auto prepared=store.prepareInitialStart("11111111-2222-4333-8444-555555555555",Device,{First,Last,First,Last,First,"10000"},source,{WorkBoundary::Ready,"12345678-1234-4567-89ab-123456789abc"});
  check(prepared.status==InitialStartStatus::Prepared&&prepared.dispatch.has_value(),"real native initial receipt fixture without launcher");booking=prepared.dispatch->bookingId;
  db.exec("UPDATE dispatch_requests SET state='STARTED',run=? WHERE id=?",{Root,booking});db.exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{booking});
  check(store.observeRuntime(Root,First,Last,Device,false).isEmpty(),"real RUNTIME PAUSED storage fixture, not remote proof");db.metadata("fixture/unrelated","KEEP");
 }
 QJsonObject manifest(const QString& run=Root)const{return {{"schema","pgn-native-process-1"},{"run",run},{"uuid",Device},{"pid",4242},{"birth",Birth},{"start",First},{"end",Last},{"queued_intent",booking},
  {"stdout","C:/PUBLIC-FIXTURE/"+run+".stdout"},{"stderr","C:/PUBLIC-FIXTURE/"+run+".stderr"},{"stop","C:/PUBLIC-FIXTURE/stop-"+run},{"pci","0000:01:00.0"},{"coverage_complete",false},{"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"},
  {"real_extra",QJsonObject{{"producer","PUBLIC-FIXTURE λ"},{"fields",QJsonArray{"preserve","not reconstruct"}}}}};}
 QJsonObject next(const QString& previous,const QString& nextRun,int index){
  const auto saved=checkpoint(previous);check(store.saveResumeCheckpoint(Device,saved.resume,saved.checkpointBytes).isEmpty(),"real original stopped tuple saved");
  const auto prepared=store.prepareCheckpointResume(commandId(index),Device,booking,previous,nextRun,saved.resume.checkpointSha256,observationId(index));check(prepared.status==CheckpointResumeAdmissionStatus::Prepared,"real native epoch admission, no process");
  check(store.observeRuntime(nextRun,First,Last,Device,false).isEmpty(),"new exact runtime fixture exists before observed manifest can be persisted");
  db.exec("UPDATE dispatch_requests SET state='STARTED' WHERE id=?",{booking});db.exec("UPDATE bookings SET state='DISPATCHED' WHERE id=?",{booking});
  auto m=manifest(nextRun);m.insert("pid",5000+index);m.insert("resume_previous_run",previous);m.insert("checkpoint_input_sha256",saved.resume.checkpointSha256);return m;
 }
};
void refused(Fixture& f,const QJsonObject& manifest,const QString& device=Device){const auto before=f.db.snapshot();const auto error=f.store.saveSourceRunManifest(device,manifest);check(!error.isEmpty()&&error.size()<=512,"invalid/mismatched/changed source manifest explicitly held");check(f.db.snapshot()==before,"refusal preserves every metadata/job/audit/index/sequence row");}
void unavailable(Fixture& f,const QString& run=Root,const QString& device=Device){const auto before=f.db.snapshot();const auto read=f.store.savedSourceRunManifest(device,run);check(read.status==SourceRunManifestReadStatus::Unavailable&&!read.error.isEmpty()&&read.manifest.isEmpty()&&read.receiptUtf8.isEmpty()&&read.blockStartedUtc.isEmpty(),"corruption is Unavailable/Held, never absence or partial data");check(f.db.snapshot()==before,"reader never repairs/re-stamps corrupt storage");}
void positive(){
 for(int state=0;state<5;++state){Fixture f;const auto absent=f.store.savedSourceRunManifest(Device,Root);check(absent.status==SourceRunManifestReadStatus::Absent&&absent.error.isEmpty(),"genuine missing source manifest is typed absence, not fabricated origin");
  if(state<2){f.db.exec("UPDATE dispatch_requests SET state='PREPARED' WHERE id=?",{f.booking});f.db.exec("UPDATE bookings SET state='DISPATCHING' WHERE id=?",{f.booking});}
  if(state==0||state==2)f.db.exec("UPDATE bookings SET state='ACTIVE' WHERE id=?",{Root});
  if(state==4)check(f.store.recordQueuedSourceCompletion(Root,First,Last,Device,f.booking).isEmpty(),"all three SOURCE projections settled before compatible manifest save");
  const auto jobs=f.db.jobs();auto m=f.manifest();m.insert("pid",4294967295LL);const auto raw=QJsonDocument(m).toJson(QJsonDocument::Compact);
  check(f.store.saveSourceRunManifest(Device,m).isEmpty(),"store exact actual-observation-shaped manifest in compatible state");check(f.db.jobs()==jobs,"manifest save creates no runtime/job/points/history dates");
  const auto read=f.store.savedSourceRunManifest(Device.toLower(),Root);check(read.status==SourceRunManifestReadStatus::Present&&read.manifest==m&&read.receiptUtf8==raw&&read.receiptUtf8.size()<=16384,"all real extra fields, Unicode, >2^53 bounds and birth precision preserved byte-exact");
  f.db.sql("CREATE TRIGGER fixture_no_manifest_update BEFORE UPDATE ON booking_metadata WHEN OLD.key LIKE 'mining-source-manifest/%' BEGIN SELECT RAISE(FAIL,'exact replay UPDATE forbidden'); END;");const auto before=f.db.snapshot();
  check(f.store.saveSourceRunManifest(Device,m).isEmpty()&&f.db.snapshot()==before,"exact replay never UPDATEs metadata or audit");
  {BookingStore reopened(f.file);const auto again=reopened.savedSourceRunManifest(Device,Root);check(reopened.valid()&&again.status==SourceRunManifestReadStatus::Present&&again.receiptUtf8==raw&&reopened.saveSourceRunManifest(Device,m).isEmpty(),"reopen preserves exact immutable source manifest without new clock");}
  check(f.db.snapshot()==before,"reopen getter/replay preserves every table and date");
 }
}
void malformed(){
 for(int poison=0;poison<28;++poison){Fixture f;auto m=f.manifest();switch(poison){
  case 0:m.insert("schema","unknown");break;case 1:m.insert("run",Root.toUpper());break;case 2:m.insert("run",Root+'\n');break;case 3:m.insert("uuid",OtherDevice);break;case 4:m.insert("uuid",Device.toLower());break;
  case 5:m.insert("pid","4242");break;case 6:m.insert("pid",true);break;case 7:m.insert("pid",0);break;case 8:m.insert("pid",1.5);break;case 9:m.insert("pid",4294967296LL);break;
  case 10:m.insert("birth",Birth+'\n');break;case 11:m.insert("birth","2026-02-30T00:00:00Z");break;case 12:m.insert("birth","2026-10-14T00:00:00+00:00");break;case 13:m.remove("birth");break;
  case 14:m.insert("start",184467440737095516260.0);break;case 15:m.insert("start","0"+First);break;case 16:m.insert("end",First);break;case 17:m.remove("queued_intent");break;case 18:m.insert("queued_intent",Root);break;
  case 19:m.insert("resume_previous_run",runId(1));break;case 20:m.insert("checkpoint_input_sha256",QString(64,'1'));break;case 21:m.insert("resume_previous_run",QJsonValue(QJsonValue::Null));m.insert("checkpoint_input_sha256",QJsonValue(QJsonValue::Null));break;
  case 22:m.insert("resume_previous_run",Root);m.insert("checkpoint_input_sha256",QString(64,'1'));break;case 23:m.insert("resume_previous_run",runId(1));m.insert("checkpoint_input_sha256","bad");break;
  case 24:m.insert("real_extra",QString(QChar(0xd800)));break;case 25:m.insert(QString(QChar(0xd800)),"invalid key");break;case 26:m.insert("stdout",QString(17000,'x'));break;case 27:m.insert("birth",false);break;
 }refused(f,m);check(f.store.savedSourceRunManifest(Device,Root).status==SourceRunManifestReadStatus::Absent,"invalid first observation never creates a metadata origin");}
 {Fixture f;refused(f,f.manifest(),OtherDevice);refused(f,f.manifest(),"unknown GPU");unavailable(f,Root,"unknown GPU");}
}
void immutable(){
 Fixture f;const auto m=f.manifest();check(f.store.saveSourceRunManifest(Device,m).isEmpty(),"immutable fixture first actual manifest saved");
 for(const auto& name:QStringList{"birth","stdout","stderr","stop","pci","proof_status"}){auto changed=m;changed.insert(name,changed.value(name).toString()+"changed");refused(f,changed);}
 auto changed=m;changed.insert("new_real_key","must not appear later");refused(f,changed);changed=m;changed.remove("real_extra");refused(f,changed);changed=m;changed.insert("coverage_complete",true);refused(f,changed);
 check(f.store.savedSourceRunManifest(Device,Root).manifest==m,"refused changed bytes/keys/markers never replace original");
}
void relations(){
 const QList<QByteArray> changes={"DELETE FROM bookings WHERE kind='RUNTIME'","UPDATE bookings SET state='ABORTED' WHERE kind='RUNTIME'","UPDATE bookings SET state='UNKNOWN' WHERE kind='RUNTIME'","UPDATE bookings SET start_key='BAD' WHERE kind='RUNTIME'","UPDATE bookings SET device='GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265' WHERE kind='RUNTIME'","DELETE FROM bookings WHERE kind='LOCAL_INTENT'","UPDATE bookings SET kind='WRONG' WHERE kind='LOCAL_INTENT'","UPDATE bookings SET state='QUEUED' WHERE kind='LOCAL_INTENT'","UPDATE bookings SET end_key='BAD' WHERE kind='LOCAL_INTENT'","UPDATE dispatch_requests SET state='ARMED'","UPDATE dispatch_requests SET state='UNKNOWN'","UPDATE dispatch_requests SET run=NULL","UPDATE dispatch_requests SET run='bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'","UPDATE dispatch_requests SET end='200000000000000000000'","DROP TABLE dispatch_requests","UPDATE bookings SET state='SOURCE_FINISHED' WHERE kind='RUNTIME'"};
 for(const auto& change:changes){Fixture f;f.db.sql(change);refused(f,f.manifest());}
 Fixture f;check(f.store.saveSourceRunManifest(Device,f.manifest()).isEmpty(),"saved manifest dependency poison fixture");f.db.sql("UPDATE bookings SET state='ABORTED' WHERE kind='RUNTIME'");unavailable(f);refused(f,f.manifest());
}
void corruption(){
 for(int poison=0;poison<9;++poison){Fixture f;const auto m=f.manifest();check(f.store.saveSourceRunManifest(Device,m).isEmpty(),"stored corruption fixture");auto raw=QJsonDocument(m).toJson(QJsonDocument::Compact);
  if(poison==0)f.db.metadata(metadataKey(Root),raw,true);if(poison==1)f.db.metadata(metadataKey(Root),raw+'\n');if(poison==2)f.db.metadata(metadataKey(Root),QJsonDocument(m).toJson(QJsonDocument::Indented));
  if(poison==3)f.db.metadata(metadataKey(Root),QByteArray("{\"run\":\"wrong\",")+raw.mid(1));if(poison==4)f.db.metadata(metadataKey(Root),QByteArray(16385,'x'));
  if(poison==5){auto wrong=m;wrong.insert("run",runId(1));f.db.metadata(metadataKey(Root),QJsonDocument(wrong).toJson(QJsonDocument::Compact));}
  if(poison==6)f.db.sql("UPDATE booking_metadata SET value=CAST(x'ff' AS TEXT) WHERE key LIKE 'mining-source-manifest/%'");
  if(poison==7)f.db.sql("UPDATE booking_metadata SET key=CAST(key AS BLOB) WHERE key LIKE 'mining-source-manifest/%'");
  if(poison==8)f.db.sql("INSERT INTO booking_metadata(key,value) SELECT CAST(key AS BLOB),value FROM booking_metadata WHERE key LIKE 'mining-source-manifest/%'");
  unavailable(f);refused(f,m);
 }
 Fixture f;check(f.store.saveSourceRunManifest(Device,f.manifest()).isEmpty(),"foreign device reader fixture");unavailable(f,Root,OtherDevice);
}
void rollback(){
 {Fixture f;f.db.sql("CREATE TRIGGER fixture_afterfail AFTER INSERT ON booking_metadata WHEN NEW.key LIKE 'mining-source-manifest/%' BEGIN INSERT INTO booking_metadata VALUES('fixture/sideeffect','rollback'); SELECT RAISE(FAIL,'AFTERFAIL'); END;");refused(f,f.manifest());check(f.store.savedSourceRunManifest(Device,Root).status==SourceRunManifestReadStatus::Absent,"AFTERFAIL rolls back source origin and sideeffect");f.db.sql("DROP TRIGGER fixture_afterfail");check(f.store.saveSourceRunManifest(Device,f.manifest()).isEmpty(),"exact retry after fully rolled-back insert succeeds");}
 {Fixture f;f.db.sql("CREATE TRIGGER fixture_rebind AFTER INSERT ON booking_metadata WHEN NEW.key LIKE 'mining-source-manifest/%' BEGIN UPDATE booking_metadata SET value='{}' WHERE key=NEW.key; END;");refused(f,f.manifest());check(f.store.savedSourceRunManifest(Device,Root).status==SourceRunManifestReadStatus::Absent,"silent AFTER trigger mutation fails post-insert readback and rolls back");}
 {Fixture f;Database busy(f.file);busy.sql("BEGIN IMMEDIATE");refused(f,f.manifest());busy.sql("ROLLBACK");check(f.store.saveSourceRunManifest(Device,f.manifest()).isEmpty(),"busy refusal retains safe retry, never partial metadata");}
}
void lineage(){
 Fixture f;const auto original=f.manifest();check(f.store.saveSourceRunManifest(Device,original).isEmpty(),"actual origin stored BEFORE native resume preparation");auto child=f.next(Root,runId(1),1);child.insert("birth","2026-10-14T00:05:00.987654321Z");
 check(f.store.savedSourceRunManifest(Device,Root).status==SourceRunManifestReadStatus::Present&&f.store.saveSourceRunManifest(Device,original).isEmpty(),"retired actual origin reader/replay survives current dispatch pointer moving");
 check(f.store.saveSourceRunManifest(Device,child).isEmpty(),"new actual process markers match exact native epoch/checkpoint and saved origin");
 const auto timing=f.store.savedSourceRunManifest(Device,runId(1));check(timing.status==SourceRunManifestReadStatus::Present&&timing.blockStartedUtc==Birth&&timing.manifest.value("birth")!=timing.blockStartedUtc,"block wall origin survives a later process resume without resetting");
 auto changed=child;changed.insert("checkpoint_input_sha256",QString(64,'0'));refused(f,changed);changed=child;changed.insert("resume_previous_run",runId(2));refused(f,changed);
 const auto before=f.db.snapshot();{BookingStore reopened(f.file);check(reopened.savedSourceRunManifest(Device,runId(1)).blockStartedUtc==Birth&&reopened.savedSourceRunManifest(Device,Root).status==SourceRunManifestReadStatus::Present,"reopened complete ancestry preserves both actual process manifests");}check(f.db.snapshot()==before,"lineage reader/reopen writes no counters or dates");
 f.db.exec("DELETE FROM booking_metadata WHERE key=?",{metadataKey(Root)});unavailable(f,runId(1));refused(f,original);check(f.store.savedSourceRunManifest(Device,Root).status==SourceRunManifestReadStatus::Absent,"missing retired legacy source remains explicit gap, never retrospective adoption");
}
void missingOriginAndMarkers(){
 {Fixture f;const auto child=f.next(Root,runId(1),1);refused(f,child);refused(f,f.manifest());check(f.store.savedSourceRunManifest(Device,Root).status==SourceRunManifestReadStatus::Absent,"native epoch alone cannot invent actual previous source manifest");}
 {Fixture f;check(f.store.saveSourceRunManifest(Device,f.manifest()).isEmpty(),"missing-marker fixture origin");auto child=f.next(Root,runId(1),1);child.remove("resume_previous_run");child.remove("checkpoint_input_sha256");refused(f,child);}
 {Fixture f;check(f.store.saveSourceRunManifest(Device,f.manifest()).isEmpty(),"missing-epoch fixture origin");const auto child=f.next(Root,runId(1),1);f.db.sql("DROP TABLE dispatch_resume_epochs");refused(f,child);}
 for(int poison=0;poison<5;++poison){Fixture f;check(f.store.saveSourceRunManifest(Device,f.manifest()).isEmpty(),"epoch index/marker poison fixture origin");auto child=f.next(Root,runId(1),1);
  if(poison==0)f.db.sql("UPDATE dispatch_resume_epochs SET run=CAST(run AS BLOB)");
  if(poison==1)f.db.sql("UPDATE dispatch_resume_epochs SET previous_run='bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'");
  if(poison==2)f.db.sql("ALTER TABLE dispatch_resume_epochs RENAME TO old_epochs;CREATE VIEW dispatch_resume_epochs AS SELECT * FROM old_epochs");
  if(poison==3){auto receipt=QJsonDocument::fromJson(f.db.raw("SELECT receipt FROM dispatch_resume_epochs")).object();auto cp=receipt.value("checkpoint").toObject();auto tuple=cp.value("resume").toObject();tuple.insert("tests","1");cp.insert("resume",tuple);receipt.insert("checkpoint",cp);f.db.exec("UPDATE dispatch_resume_epochs SET receipt=?",{QString::fromUtf8(QJsonDocument(receipt).toJson(QJsonDocument::Compact))});}
  if(poison==4)child.insert("checkpoint_input_sha256",QString(64,'0'));refused(f,child);
 }
}
void longChain(){
 Fixture f;check(f.store.saveSourceRunManifest(Device,f.manifest()).isEmpty(),"long-chain actual origin saved");QString previous=Root;
 for(int i=1;i<=70;++i){auto actual=f.next(previous,runId(i),i);check(f.store.saveSourceRunManifest(Device,actual).isEmpty(),"iterative lineage beyond64 retains every actual manifest without depth quota");previous=runId(i);}
 const auto before=f.db.snapshot();check(f.store.savedSourceRunManifest(Device,previous).status==SourceRunManifestReadStatus::Present&&f.store.savedSourceRunManifest(Device,Root).status==SourceRunManifestReadStatus::Present,"latest and oldest actual source readable through70 immutable native epochs");check(f.db.snapshot()==before,"long ancestry walk is read-only");
}
void cycle(){
 Fixture f;auto original=f.manifest();check(f.store.saveSourceRunManifest(Device,original).isEmpty(),"cycle fixture actual origin");const auto child=f.next(Root,runId(1),1);check(f.store.saveSourceRunManifest(Device,child).isEmpty(),"cycle fixture valid child");
 auto receipt=QJsonDocument::fromJson(f.db.raw("SELECT receipt FROM dispatch_resume_epochs")).object();receipt.insert("run",Root);receipt.insert("previous_run",runId(1));receipt.insert("request_id",commandId(2));receipt.insert("observation_id",observationId(2));auto cp=receipt.value("checkpoint").toObject();auto tuple=cp.value("resume").toObject();tuple.insert("run",runId(1));cp.insert("resume",tuple);receipt.insert("checkpoint",cp);
 f.db.exec("INSERT INTO dispatch_resume_epochs VALUES(?,?,?,?,?)",{Root,runId(1),f.booking,commandId(2),QString::fromUtf8(QJsonDocument(receipt).toJson(QJsonDocument::Compact))});
 original.insert("resume_previous_run",runId(1));original.insert("checkpoint_input_sha256",checkpoint(Root).resume.checkpointSha256);f.db.metadata(metadataKey(Root),QJsonDocument(original).toJson(QJsonDocument::Compact));
 unavailable(f,runId(1));check(f.store.savedSourceRunManifest(Device,runId(1)).error.contains("cycle"),"cycle is detected by identity set, not accepted origin or arbitrary depth quota");
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{positive();malformed();immutable();relations();corruption();rollback();lineage();missingOriginAndMarkers();longChain();cycle();std::cout<<"Source run manifest store: "<<checks<<" checks / 0 failures; actual temporary SQLite/reopen/transactions; synthetic remote-manifest-shaped fixtures ONLY, NO process/UI/Node/SSH/GPU/authentication claim\n";return 0;}catch(const std::exception& e){std::cerr<<"Source run manifest store failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}

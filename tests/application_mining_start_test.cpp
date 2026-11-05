#include "mining/application_mining_start.h"
#include "mining/native_bridge.h"
#include "mining/source_stop_checkpoint_packet.h"
#include "booking/booking_store.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <sqlite3.h>
#include <cstdio>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace pgn::mining {struct NativeBridgeTestAccess {static QProcess& process(NativeBridge& b){return b.relay_;}static void receive(NativeBridge& b,const QJsonObject& p){b.receive(p);}};}
using namespace pgn::mining;
using namespace pgn::booking;
namespace {
int checks{};
const QString A="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",B="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265";
const QString First="199900000000000000000",Last="200000000000000000000",Width="1000000000000000";
const QString Run="1234567890abcdef1234567890abcdef",Utc="2026-10-14T00:00:01.000Z";
const QString Request="11111111-2222-4333-8444-555555555555",Other="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
QString currentCase;
void check(bool b,const char* s){++checks;if(!b)throw std::runtime_error(s);}
QString block(int n){QString result=First;for(int i=0;i<n;++i)result=addUnsigned(result,Width);return result;}
void wait(const std::function<bool()>& done){QElapsedTimer t;t.start();while(!done()&&t.elapsed()<6000){QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}check(done(),qPrintable("bounded own SELF/event loop deadline: "+currentCase));}
class Database {
public:
 sqlite3* db{};
 explicit Database(const QString& path){if(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)throw std::runtime_error("own temporary SQLite open");sqlite3_busy_timeout(db,200);}
 ~Database(){if(db)sqlite3_close(db);}
 void sql(const QByteArray& s){if(sqlite3_exec(db,s.constData(),nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
 QString value(const QByteArray& s){sqlite3_stmt* q{};if(sqlite3_prepare_v2(db,s.constData(),-1,&q,nullptr)!=SQLITE_OK)throw std::runtime_error("own fixture query prepare");QString out;const auto rc=sqlite3_step(q);if(rc==SQLITE_ROW)out=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(q,0)),sqlite3_column_bytes(q,0));sqlite3_finalize(q);return out;}
};
void write(const QString& path,const QByteArray& bytes){QFile f(path);check(f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size(),"own temporary fixture write");}
QVector<QJsonObject> actions(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QVector<QJsonObject> rows;for(const auto& line:f.readAll().split('\n')){const auto doc=QJsonDocument::fromJson(line);if(doc.isObject())rows.append(doc.object());}return rows;}
int count(const QVector<QJsonObject>& rows,const QString& name){int n=0;for(const auto& row:rows)if(row.value("action")==name)++n;return n;}
QJsonObject action(const QVector<QJsonObject>& rows,const QString& name){for(const auto& row:rows)if(row.value("action")==name)return row;return {};}
bool full(const QString& name){return QStringList{"full","source-only","missing-conservation","conservation-string","source-bool","source-scope","wrong-exit","foreign-binding","commit-fail","partial-count","full-no-lineage","full-held-lineage"}.contains(name);}
bool resumeScenario(const QString& name){return name.startsWith("resume-");}
const QString ResumeStart="184467440737095516260",ResumeEnd="184467440737095526260";
const QString ResumeLabel="C:\\PGN\\runtime\\current-job\\checkpoint",ResumePredecessor=QString(32,'d');
QByteArray resumeBytes(){
 QByteArray bytes="PGNCG001";for(const quint64 word:{100ULL,10ULL,10000ULL,123ULL,9007199254740993ULL,7ULL,2ULL})for(unsigned i=0;i<8;++i)bytes.append(char((word>>(8*i))&255));return bytes;
}
QByteArray resumeTail(){return ("[pgn-counters] time_ns=9007199254740995 positions=123 gaps=7 tests=9007199254740993 batches=2 processed_gaps=7\n[phase0-gpu] stopped positions=123 checkpoint="+ResumeLabel+"\n").toUtf8();}
QByteArray resumeRaw(){return QByteArray("[PUBLIC-FIXTURE] original GAP source bytes remain an immutable old epoch\n")+resumeTail();}
QByteArray readOwn(const QString& path){QFile f(path);check(f.open(QIODevice::ReadOnly)&&f.size()<65536,"bounded own source fixture read");return f.readAll();}
QJsonObject resumeSnapshot(const QString& scenario,const QString& intent){
 const QJsonObject binding{{"uuid",A},{"start",ResumeStart},{"end",ResumeEnd},{"length","10000"},{"queued_intent",intent},
  {"test_scope",QJsonObject{{"start",ResumeStart},{"end",ResumeEnd},{"order","ascending"}}}};
 const QJsonObject manifest{{"schema","pgn-native-process-1"},{"run",Run},{"birth",Utc},{"pid",4242},{"uuid",A},{"start",ResumeStart},{"end",ResumeEnd},{"queued_intent",intent}};
 QJsonObject capture{{"schema","pgn-stop-checkpoint-evidence-1"},{"status","captured"},{"error",""},{"run",Run},{"checkpoint_label",ResumeLabel},
  {"checkpoint_base64",QString::fromLatin1(resumeBytes().toBase64())},{"source_tail_utf8",QString::fromUtf8(resumeTail())}};
 if(scenario=="resume-capture"){capture.insert("status","unavailable");capture.insert("error","SELF unknown capture, never empty checkpoint");}
 if(scenario=="resume-bytes"){auto bytes=resumeBytes();bytes[0]='X';capture.insert("checkpoint_base64",QString::fromLatin1(bytes.toBase64()));}
 if(scenario=="resume-footer")capture.insert("source_tail_utf8",QString::fromUtf8(resumeTail()).replace("stopped positions=123","stopped positions=124"));
 if(scenario=="resume-lineage")capture.insert("run",QString(32,'e'));
 QJsonObject s{{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},{"operator_stop",true},{"driver_valid",true},{"observed_utc",Utc},
  {"binding",binding},{"manifest",manifest},{"last_exit",QJsonObject{{"run",Run},{"exit_code",0}}},{"has_checkpoint",true},
  {"checkpoint_resume_protocol","pgn-checkpoint-resume-admission-1"},{"checkpoint_evidence",capture},{"next_offset",resumeRaw().size()},{"pending_bytes",0}};
 if(scenario=="resume-no-protocol")s.remove("checkpoint_resume_protocol");
 if(scenario=="resume-owned-unknown")s.insert("owned",QJsonValue(QJsonValue::Null));
 return s;
}
QJsonObject resumeInventory(const QJsonObject& command,const QString& scenario,const QString& intent){
 const auto end=resumeRaw().size();QJsonObject conservation{{"schema","pgn-source-conservation-1"},{"scope","LOGICAL_SOURCE_EPOCH_CONSERVATION_NOT_GLOBAL_POWER_CUT_DURABILITY"},
  {"run",Run},{"source","native/"+Run+".stdout"},{"ready",scenario!="resume-not-conserved"},{"required_end",end},{"committed_offset",end},{"mirror_size",end},{"transport_offset",end}};
 return {{"type","application_start_inventory"},{"request_id",command.value("request_id")},{"inventory_id",command.value("inventory_id")},{"accepted",true},
  {"snapshot",resumeSnapshot(scenario,intent)},{"source_conservation",conservation}};
}
QJsonObject snapshot(const QString& scenario,const QString& intent){
 QJsonObject binding{{"uuid",A},{"start",block(32)},{"end",block(33)},{"test_scope",QJsonObject{{"start",First},{"end",Last},{"order","ascending"}}}};
 const bool running=scenario=="running"||scenario=="running-bad-pid"||scenario=="running-birth-newline"||scenario=="running-observed-newline";
 const bool hasManifest=full(scenario)||running||scenario=="checkpoint";
 QJsonObject m{{"schema","pgn-native-process-1"},{"run",Run},{"birth",Utc},{"pid",4242},{"uuid",A},{"start",block(32)},{"end",block(33)}};
 if(!intent.isEmpty()){binding.insert("queued_intent",intent);m.insert("queued_intent",intent);}
 if(scenario=="running-bad-pid")m.insert("pid","4242");
 if(scenario=="running-birth-newline")m.insert("birth",Utc+'\n');
 QJsonObject s{{"schema","pgn-runtime-snapshot-1"},{"state",running?"RUNNING":"OFF"},{"owned",running},{"operator_stop",!running},
  {"driver_valid",true},{"observed_utc",Utc},{"binding",binding},{"manifest",hasManifest?QJsonValue(m):QJsonValue(QJsonValue::Null)},
  {"last_exit",hasManifest?QJsonValue(QJsonObject{{"run",Run},{"exit_code",0}}):QJsonValue(QJsonValue::Null)},{"has_checkpoint",scenario=="checkpoint"},{"next_offset",1000},{"pending_bytes",20}};
 if(scenario=="running-observed-newline")s.insert("observed_utc",Utc+'\n');
 if(full(scenario)){
  QJsonObject receipt{{"schema","pgn-source-range-completion-1"},{"run",Run},{"start",block(32)},{"end",block(33)},{"source_positions",Width},
   {"source_range_complete",true},{"source_exit_code",0},{"independent_coverage",false},{"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"}};
  if(!intent.isEmpty())receipt.insert("queued_intent",intent);
  if(scenario=="partial-count")receipt.insert("source_positions","1");
  if(scenario=="source-bool")receipt.insert("independent_coverage","false");
  if(scenario=="source-only"||scenario=="source-scope"){
   receipt.insert("evidence_scope","VERIFIED_SNAPSHOT_SOURCE_COMPLETION");receipt.insert("source_observed_operator_stop",true);receipt.insert("source_observed_stop_file",true);
   if(scenario=="source-scope")receipt.insert("source_observed_operator_stop",0);
   s.insert("source_completion_observation",receipt);
  }else s.insert("completion_candidate",receipt);
  // Typed consumer fixture only, not a real RAW certificate or production data.
  // Valid completions now require the same proof fields for flat and resumed
  // runs. Negative cases retain the actual absence/refusal rather than an ACK.
  if(scenario!="full-no-lineage"){
   QJsonObject proof{{"schema","pgn-source-resume-lineage-1"},{"ready",scenario!="full-held-lineage"},
    {"scope","LOCAL_RAW_LINEAGE_COUNTER_DELTAS_NOT_INDEPENDENT_PRIME_COVERAGE"},
    {"run",Run},{"booking_id",intent},{"start",block(32)},{"end",block(33)},{"terminal","complete"},
    {"epochs",1},{"chain_sha256",QString(64,'a')},{"error",scenario=="full-held-lineage"?"TESTONLY RAW refusal":""}};
   s.insert("resume_lineage",proof);
  }
 }
 if(scenario=="missing-owned")s.remove("owned");
 if(scenario=="checkpoint-unknown")s.insert("has_checkpoint",QJsonValue(QJsonValue::Null));
 if(scenario=="missing-exit")s.remove("last_exit");
 if(scenario=="bad-scope"){auto t=binding.value("test_scope").toObject();t.insert("order","descending");binding.insert("test_scope",t);s.insert("binding",binding);}
 if(scenario=="foreign-binding"){binding.insert("end",block(34));s.insert("binding",binding);}
 if(scenario=="wrong-exit")s.insert("last_exit",QJsonObject{{"run",QString(32,'b')},{"exit_code",0}});
 return s;
}
QJsonObject inventory(const QJsonObject& c,const QString& scenario,const QString& intent){
 if(resumeScenario(scenario))return resumeInventory(c,scenario,intent);
 const auto s=snapshot(scenario,intent);
 QJsonObject conserved{{"schema","pgn-source-conservation-1"},{"scope","LOGICAL_SOURCE_EPOCH_CONSERVATION_NOT_GLOBAL_POWER_CUT_DURABILITY"},
  {"run",Run},{"source","native/"+Run+".stdout"},{"ready",true},{"required_end",1020},{"committed_offset",1020},{"mirror_size",1020},{"transport_offset",1020}};
 if(scenario=="conservation-string")conserved.insert("committed_offset","1020");
 QJsonObject p{{"type","application_start_inventory"},{"request_id",c.value("request_id")},{"inventory_id",c.value("inventory_id")},{"accepted",scenario!="unknown"},{"snapshot",s},{"source_conservation",conserved}};
 if(scenario=="missing-conservation")p.remove("source_conservation");return p;
}
void emitPacket(const QJsonObject& packet){const auto bytes=QJsonDocument(packet).toJson(QJsonDocument::Compact)+'\n';std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);std::fflush(stdout);}
int self(const QString& path){
 QFile config(path);if(!config.open(QIODevice::ReadOnly))return 20;const auto c=QJsonDocument::fromJson(config.readAll()).object();
 QFile audit(c.value("audit").toString());if(!audit.open(QIODevice::WriteOnly|QIODevice::Append))return 21;
 std::string line;while(std::getline(std::cin,line)){
  auto command=QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();const auto name=command.value("action").toString();
  if(name=="start_prepared"){
   Database db(c.value("database").toString());const auto key="mining-initial-start/v1/"+command.value("request_id").toString();
   const auto receipt=QJsonDocument::fromJson(db.value(("SELECT value FROM booking_metadata WHERE key='"+key+"'").toUtf8()).toUtf8()).object();
   command.insert("self_receipt_committed",receipt.value("request_id")==command.value("request_id")&&receipt.value("observation_id")==command.value("observation_id"));
   command.insert("self_receipt",receipt);
   command.insert("self_prepared",db.value("SELECT state FROM dispatch_requests WHERE id='"+receipt.value("booking_id").toString().toUtf8()+"'")=="PREPARED");
   if(full(c.value("scenario").toString()))command.insert("self_source_finished",db.value(("SELECT count(*) FROM bookings WHERE (id='"+Run+"' OR id='"+c.value("intent").toString()+"') AND state='SOURCE_FINISHED'").toUtf8())=="2");
  }
  if(name=="resume_prepared"){
   Database db(c.value("database").toString());const auto next=command.value("new_run").toString(),booking=command.value("booking_id").toString();
   const auto receipt=QJsonDocument::fromJson(db.value(("SELECT receipt FROM dispatch_resume_epochs WHERE run='"+next+"'").toUtf8()).toUtf8()).object();
   command.insert("self_resume_receipt",receipt);
   command.insert("self_resume_committed",receipt.value("request_id")==command.value("request_id")&&receipt.value("observation_id")==command.value("observation_id")&&receipt.value("run")==next&&receipt.value("previous_run")==Run);
   command.insert("self_resume_prepared",db.value(("SELECT state||'|'||run||'|'||predecessor FROM dispatch_requests WHERE id='"+booking+"'").toUtf8())=="PREPARED|"+next+"|"+ResumePredecessor);
   command.insert("self_old_paused",db.value(("SELECT state FROM bookings WHERE id='"+Run+"'").toUtf8())=="PAUSED");
  }
  audit.write(QJsonDocument(command).toJson(QJsonDocument::Compact)+'\n');audit.flush();
  if(name=="application_start_inventory"){
   const auto scenario=c.value("scenario").toString();
   if(scenario=="late"||scenario=="stop"||scenario=="closing"||(scenario=="resume-nonce"&&c.value("request_id")==Request))continue;
   emitPacket(inventory(command,scenario,c.value("intent").toString()));
  }
  if(name=="start_prepared")emitPacket({{"type","control"},{"device",A},{"request_id",command.value("request_id")},{"result",QJsonObject{{"status","START_REQUESTED"},{"request_id",command.value("request_id")}}}});
  if(name=="resume_prepared")emitPacket({{"type","control"},{"device",A},{"request_id",command.value("request_id")},{"result",QJsonObject{{"status","START_REQUESTED"},{"request_id",command.value("request_id")}}}});
  if(name=="application_close_prepare")emitPacket({{"type","application_close_frozen"},{"request_id",command.value("request_id")},{"accepted",true}});
 }return 0;
}
struct Fixture {
 QTemporaryDir temp;QString db,config,audit,intent;NativeBridge bridge;std::unique_ptr<ApplicationMiningStart> coordinator;QStringList reports;QJsonArray controls;int samples{},inventories{};
 explicit Fixture(const QString& scenario):db(temp.filePath("local-bookings.sqlite")),config(temp.filePath("self.json")),audit(temp.filePath("commands.jsonl")){
  currentCase=scenario;
  check(temp.isValid(),"own temp directory");const QByteArray csv="Start *1e15,End *1e15,Status,Owner\n199900,200000,Open,\n";
  write(temp.filePath("community-source.csv"),csv);
  write(temp.filePath("binding.json"),QJsonDocument(QJsonObject{{"community",QJsonObject{{"source","https://docs.google.com/spreadsheets/d/1y0OWkPbf8dvg6cpNqGNawhIykAlAWAtI8vqUA4KPiQ8/PUBLIC-FIXTURE"},{"source_sha256",QString::fromLatin1(QCryptographicHash::hash(csv,QCryptographicHash::Sha256).toHex())},{"observed_utc",Utc}}}}).toJson(QJsonDocument::Compact));
  BookingStore store(db);check(store.valid(),"same temporary booking store");const auto registry=loadSourceRegistry(temp.path());check(registry.valid(),"real source loader on public synthetic fixtures");
  if(full(scenario)){
   check(store.queue(registry,block(32),block(33),A).isEmpty(),"old exact local intent queued");intent=store.active().first().id;
   check(store.observeRuntime(Run,block(32),block(33),A,false).isEmpty(),"old actual RUNTIME PAUSED fixture");
   Database raw(db);raw.sql("CREATE TABLE dispatch_requests(id TEXT PRIMARY KEY,start TEXT NOT NULL,end TEXT NOT NULL,device TEXT NOT NULL,state TEXT NOT NULL,predecessor TEXT,run TEXT,created_utc TEXT NOT NULL);");
   raw.sql(("UPDATE bookings SET state='DISPATCHED' WHERE id='"+intent+"';INSERT INTO dispatch_requests VALUES('"+intent+"','"+block(32)+"','"+block(33)+"','"+A+"','STARTED',NULL,'"+Run+"','FIXTURE');").toUtf8());
   if(scenario=="commit-fail")raw.sql("CREATE TRIGGER fixture_fail AFTER UPDATE OF state ON bookings WHEN NEW.kind='LOCAL_INTENT' AND NEW.state='SOURCE_FINISHED' BEGIN INSERT INTO booking_metadata VALUES('fixture/sideeffect','rollback'); SELECT RAISE(FAIL,'FIXTURE SOURCE FAIL'); END;");
  }
  if(resumeScenario(scenario)){
   auto original=registry;original.ranges={{2,ResumeStart,ResumeEnd,"Open",{}}};original.start=ResumeStart;original.end=ResumeEnd;
   check(store.queue(original,ResumeStart,ResumeEnd,A).isEmpty(),"actual original local checkpoint job queued");intent=store.active().first().id;
   check(store.observeRuntime(Run,ResumeStart,ResumeEnd,A,false).isEmpty(),"actual native old RUNTIME PAUSED fixture");
   check(store.setMiningSessionMode(A,MiningSessionMode::ManualReservations).isEmpty()&&store.setAutomaticAfterManual(A,false).isEmpty(),"resume retains pre-existing Manual/OFF session provenance");
   Database raw(db);raw.sql("CREATE TABLE dispatch_requests(id TEXT PRIMARY KEY,start TEXT NOT NULL,end TEXT NOT NULL,device TEXT NOT NULL,state TEXT NOT NULL,predecessor TEXT,run TEXT,created_utc TEXT NOT NULL);");
   raw.sql(("UPDATE bookings SET state='DISPATCHED' WHERE id='"+intent+"';INSERT INTO dispatch_requests VALUES('"+intent+"','"+ResumeStart+"','"+ResumeEnd+"','"+A+"','STARTED','"+ResumePredecessor+"','"+Run+"','FIXTURE');").toUtf8());
   check(store.queue(registry,block(7),block(8),A).isEmpty(),"future manual block is deliberately present but must not be claimed by resume");
   write(temp.filePath("old-source.stdout"),resumeRaw());
   if(scenario=="resume-old-physical")raw.sql(("UPDATE bookings SET state='ACTIVE' WHERE id='"+Run+"'").toUtf8());
   if(scenario=="resume-commit-fail")raw.sql("CREATE TABLE dispatch_resume_epochs(run TEXT PRIMARY KEY,previous_run TEXT NOT NULL UNIQUE,booking_id TEXT NOT NULL,request_id TEXT NOT NULL UNIQUE,receipt TEXT NOT NULL);CREATE TRIGGER fixture_resume_fail AFTER INSERT ON dispatch_resume_epochs BEGIN INSERT INTO booking_metadata VALUES('fixture/resume-sideeffect','rollback'); SELECT RAISE(FAIL,'SELF resume epoch commit failure'); END;");
  }
  if(scenario=="manual"||scenario=="full"||scenario=="source-only")check(store.queue(registry,block(10),block(11),A).isEmpty()&&store.queue(registry,block(7),block(8),A).isEmpty(),"manual11 then8 fixture");
  if(scenario=="prepared"){
   const auto prepared=store.prepareInitialStart(Other,A,{First,Last,First,Last,First,Width},registry,{WorkBoundary::Ready,"12345678-1234-4567-89ab-123456789abc"});
   check(prepared.status==InitialStartStatus::Prepared,"existing durable initial PREPARED fixture");
  }
  if(scenario=="receipt-fail"){Database raw(db);raw.sql("CREATE TRIGGER fixture_receipt_fail AFTER INSERT ON booking_metadata WHEN NEW.key LIKE 'mining-initial-start/%' BEGIN INSERT INTO booking_metadata VALUES('fixture/sideeffect','rollback'); SELECT RAISE(FAIL,'FIXTURE RECEIPT FAIL'); END;");}
  write(config,QJsonDocument(QJsonObject{{"scenario",scenario},{"audit",audit},{"database",db},{"intent",intent}}).toJson(QJsonDocument::Compact));
  bridge.onControl=[this](const QString& gpu,const QString& request,bool applied,const QString& detail){controls.append(QJsonObject{{"device",gpu},{"id",request},{"applied",applied},{"detail",detail}});};
  bridge.onDevice=[this](const auto&){++samples;};bridge.onApplicationStartInventory=[this](const auto&,const auto&){++inventories;};
  bridge.start(QCoreApplication::applicationFilePath(),"--self-mining-start",config);check(NativeBridgeTestAccess::process(bridge).waitForStarted(3000),"only own QtCore SELF subprocess starts");
  coordinator=std::make_unique<ApplicationMiningStart>(&bridge,db,temp.path());coordinator->onStatus=[this](const QString& s){reports.append(s);};
 }
 void start(){coordinator->control(A,Request,true);}
 void finish(){auto& p=NativeBridgeTestAccess::process(bridge);p.closeWriteChannel();wait([&]{return p.state()==QProcess::NotRunning;});check(p.exitStatus()==QProcess::NormalExit&&p.exitCode()==0,"own SELF exits cleanly");}
 void terminal(){wait([&]{return !controls.isEmpty();});}
};
QJsonObject physical(const QJsonObject& receipt,const QString& request=Request){
 auto s=snapshot("running",receipt.value("booking_id").toString());auto m=s.value("manifest").toObject();m.insert("start",receipt.value("start"));m.insert("end",receipt.value("end"));m.insert("run",QString(32,'c'));s.insert("manifest",m);
 auto binding=s.value("binding").toObject();binding.insert("start",receipt.value("start"));binding.insert("end",receipt.value("end"));binding.insert("initial_start",QJsonObject{{"origin","INITIAL_START"},{"request_id",request}});s.insert("binding",binding);s.insert("type","snapshot");return s;
}
void selected(const QString& name){
 Fixture f(name);f.start();wait([&]{return count(actions(f.audit),"start_prepared")==1;});
 const auto cmd=action(actions(f.audit),"start_prepared"),receipt=cmd.value("self_receipt").toObject();
 const auto exportPath=qEnvironmentVariable("PGN_TEST_INITIAL_CONTRACT_EXPORT");
 if(name=="manual"&&!exportPath.isEmpty()){
  check(QFileInfo(exportPath).isAbsolute()&&!QFileInfo::exists(exportPath),"explicit new own closed-contract artifact directory");
  check(QDir().mkpath(exportPath),"create only named test artifact directory");
  Database source(f.db);sqlite3* copy{};const auto path=(exportPath+"/native-bookings.sqlite").toUtf8();
  check(sqlite3_open_v2(path.constData(),&copy,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)==SQLITE_OK,"own SQLite contract destination");
  auto* backup=sqlite3_backup_init(copy,"main",source.db,"main");check(backup!=nullptr,"consistent actual native SQLite backup");
  const auto copied=sqlite3_backup_step(backup,-1),finished=sqlite3_backup_finish(backup);sqlite3_close(copy);
  check(copied==SQLITE_DONE&&finished==SQLITE_OK,"actual committed native state snapshot, not reconstructed SQL fixture");
  QJsonObject control;for(const auto* key:{"action","request_id","device","observation_id","base_run","base_birth","base_start","base_end"})control.insert(key,cmd.value(key));
  write(exportPath+"/command.json",QJsonDocument(control).toJson(QJsonDocument::Compact));
  write(exportPath+"/fixture.json",QJsonDocument(QJsonObject{{"schema","pgn-closed-native-initial-contract-1"},{"scope","TEST_ONLY_NO_RUNTIME_OR_PHYSICAL_PROOF"}}).toJson(QJsonDocument::Compact));
  write(exportPath+"/community-source.csv","Start *1e15,End *1e15,Status,Owner\n199900,200000,Open,\n");
 }
 check(cmd.value("self_receipt_committed").toBool()&&cmd.value("self_prepared").toBool(),"native receipt and PREPARED committed before real pipe callback");
 check(f.controls.isEmpty()&&f.coordinator->status().contains("awaiting exact physical"),"START_REQUESTED transport ACK is not physical control success");
 check(receipt.value("start")==((name=="manual"||name=="full"||name=="source-only")?block(7):First)&&receipt.value("start")!=block(32),"binding33 replaced by manual8 or first whole free block1");
 check(receipt.value("mode")==((name=="manual"||name=="full"||name=="source-only")?"manual-reservations":"automatic"),"committed explicit mode matches selected work");
 check(f.samples==0&&f.inventories==1,"read-only startup inventory never touches telemetry/age; old callback chained");
 if(full(name)){
  check(cmd.value("self_source_finished").toBool(),"physical RUNTIME and LOCAL_INTENT finished before prepared frame");
  Database db(f.db);check(db.value(("SELECT state FROM dispatch_requests WHERE id='"+f.intent+"'").toUtf8())=="SOURCE_FINISHED","old dispatch SOURCE committed with same identity");
  check(cmd.value("base_run")==Run&&cmd.value("base_birth")==Utc&&cmd.value("base_start")==block(32)&&cmd.value("base_end")==block(33),"old epoch carried exactly as base, not a fake predecessor");
 }else check(cmd.value("base_run").isNull()&&cmd.value("base_birth").isNull()&&cmd.value("base_start").isNull()&&cmd.value("base_end").isNull(),"no manifest means four explicit null base fields");
 // Duplicate UI input, successful receipt and foreign physical snapshot cannot
 // manufacture a second launch or satisfy this one.
 f.coordinator->control(A,Request,true);check(f.controls.isEmpty(),"same in-flight request is not negatively/positively ACKed");
 NativeBridgeTestAccess::receive(f.bridge,physical(receipt,Other));check(f.controls.isEmpty(),"other initial request cannot resolve prepared ownership");
 auto wrong=physical(receipt);auto m=wrong.value("manifest").toObject();m.insert("start",block(32));wrong.insert("manifest",m);NativeBridgeTestAccess::receive(f.bridge,wrong);check(f.controls.isEmpty(),"wrong exact prepared range cannot resolve ownership");
 auto unknown=physical(receipt);unknown.insert("owned","true");NativeBridgeTestAccess::receive(f.bridge,unknown);check(f.controls.isEmpty(),"untyped physical owned cannot resolve prepared start");
 for(int poison=0;poison<12;++poison){
  auto packet=physical(receipt),manifest=packet.value("manifest").toObject(),binding=packet.value("binding").toObject();
  switch(poison){
   case 0:manifest.insert("uuid",B);break;case 1:manifest.insert("run","unknown");break;case 2:manifest.insert("birth","2026-02-30T00:00:00Z");break;
   case 3:manifest.insert("schema","unknown");break;case 4:manifest.insert("pid","4242");break;case 5:manifest.insert("pid",0);break;
   case 6:binding.insert("uuid",B);break;case 7:binding.insert("end",block(33));break;case 8:binding.remove("queued_intent");break;
   case 9:binding.insert("initial_start",QJsonObject{{"request_id",Request},{"origin","ADVANCE"}});break;
   case 10:packet.insert("schema","unknown");break;case 11:manifest.remove("birth");break;
  }
  packet.insert("manifest",manifest);packet.insert("binding",binding);NativeBridgeTestAccess::receive(f.bridge,packet);
  check(f.controls.isEmpty(),"foreign/malformed physical epoch/schema/PID/origin cannot satisfy prepared identity");
 }
 NativeBridgeTestAccess::receive(f.bridge,physical(receipt));check(f.controls.size()==1&&f.controls[0].toObject().value("applied").toBool(),"only exact typed owned epoch satisfies physical pending start");
 Database db(f.db);check(db.value(("SELECT state FROM dispatch_requests WHERE id='"+receipt.value("booking_id").toString()+"'").toUtf8())=="PREPARED","ACK alone does not mutate dispatch to STARTED");
 f.coordinator->control(A,Other,true);f.terminal();wait([&]{return f.coordinator->status().startsWith("Initial start held:");});
 f.finish();check(count(actions(f.audit),"start_prepared")==1,"PREPARED reconciliation/replay never invents a second worker");
}
void refused(const QString& name){
 Fixture f(name);f.start();f.terminal();check(!f.controls[0].toObject().value("applied").toBool()&&f.coordinator->status().startsWith("Initial start held:"),"unknown/interrupted/conflict produces correlated Held");
 check(f.samples==0,"held inventory does not replace raw metrics");f.finish();check(count(actions(f.audit),"start_prepared")==0,"held initial state submits zero prepared/legacy starts");
 Database db(f.db);if(full(name)){
  check(db.value(("SELECT state FROM bookings WHERE id='"+Run+"'").toUtf8())=="PAUSED"&&db.value(("SELECT state FROM bookings WHERE id='"+f.intent+"'").toUtf8())=="DISPATCHED","unverified SOURCE / failed transaction preserves physical+local provenance");
  check(db.value("SELECT count(*) FROM booking_metadata WHERE key='fixture/sideeffect'")=="0","failed trigger sideeffect rolls back");
 }
 if(name=="checkpoint")check(f.coordinator->status().contains("ResumeRequired"),"checkpoint explicitly requests original epoch resume, never Auto fallback");
}
void running(){Fixture f("running");f.start();f.terminal();check(f.controls.size()==1&&f.controls[0].toObject().value("applied").toBool()&&f.coordinator->status().contains("already running observed"),"fresh exact owned RUNNING is an observation, not a command ACK");f.finish();check(count(actions(f.audit),"start_prepared")==0,"already running issues no launch/admission");Database db(f.db);check(db.value("SELECT count(*) FROM bookings")=="0"&&db.value("SELECT count(*) FROM booking_metadata WHERE key LIKE 'mining-session-%'")=="0","already owned does not create booking or choose mode");}
void cancelled(const QString& name){
 Fixture f(name);f.start();wait([&]{return count(actions(f.audit),"application_start_inventory")==1;});const auto c=action(actions(f.audit),"application_start_inventory");
 if(name=="closing")check(f.bridge.prepareApplicationClose(Other),"existing close local freeze accepted by own pipe");else f.coordinator->control(A,Other,false);
 wait([&]{return !f.controls.isEmpty();});NativeBridgeTestAccess::receive(f.bridge,inventory(c,"auto",{}));
 f.finish();check(count(actions(f.audit),"start_prepared")==0,"late response after STOP/close cannot prepare or launch");
 if(name=="stop")check(count(actions(f.audit),"stop")==1,"STOP continues through existing endpoint");
 Database db(f.db);check(db.value("SELECT count(*) FROM bookings")=="0","late cancelled admission does not allocate");
}
void nonceAndDeadline(){
 Fixture f("late");f.start();wait([&]{return count(actions(f.audit),"application_start_inventory")==1;});const auto c=action(actions(f.audit),"application_start_inventory");
 auto wrong=inventory(c,"auto",{});wrong.insert("inventory_id",Other);NativeBridgeTestAccess::receive(f.bridge,wrong);check(f.controls.isEmpty()&&f.inventories==0,"wrong inventory nonce cannot reach coordinator or grant Ready");
 wrong.insert("inventory_id",c.value("inventory_id"));wrong.insert("request_id",Other);NativeBridgeTestAccess::receive(f.bridge,wrong);check(f.controls.isEmpty()&&f.inventories==0,"wrong request generation cannot grant Ready");
 const auto timer=f.coordinator->findChild<QTimer*>("initial-start-deadline");check(timer&&timer->isSingleShot()&&timer->interval()==45000,"one bounded deadline, no retry/poll loop");
 check(QMetaObject::invokeMethod(timer,"timeout",Qt::DirectConnection),"closed test invokes deadline without waiting45s");
 check(f.controls.size()==1&&!f.controls[0].toObject().value("applied").toBool(),"expired inventory holds once without fabricated physical success");
 NativeBridgeTestAccess::receive(f.bridge,inventory(c,"auto",{}));f.finish();check(count(actions(f.audit),"start_prepared")==0,"late valid response after deadline cannot allocate/start");
}
void physicalDeadlineRetainsCorrelation(bool stop=false){
 Fixture f("auto");f.start();wait([&]{return count(actions(f.audit),"start_prepared")==1;});
 const auto receipt=action(actions(f.audit),"start_prepared").value("self_receipt").toObject();
 const auto timer=f.coordinator->findChild<QTimer*>("initial-start-deadline");check(timer&&timer->isActive(),"physical attempt has original diagnostic UI budget");
 check(QMetaObject::invokeMethod(timer,"timeout",Qt::DirectConnection),"physical diagnostic deadline exercised without waiting45s");
 check(f.controls.isEmpty()&&f.coordinator->status().contains("unconfirmed",Qt::CaseInsensitive),"UI timeout after submission keeps exact uncertain attempt, not false OFF or erased correlation");
 f.coordinator->control(A,Request,true);check(f.controls.isEmpty(),"same uncertain physical request remains single and pending");
 if(stop){
  f.coordinator->control(A,Other,false);check(!f.controls.isEmpty()&&!f.controls[0].toObject().value("applied").toBool(),"explicit STOP still cancels uncertain original after diagnostic deadline");
  const auto cancelled=f.controls.size();NativeBridgeTestAccess::receive(f.bridge,physical(receipt));
  check(f.controls.size()==cancelled,"late original ownership cannot ACK a request cancelled by explicit STOP");
  f.finish();check(count(actions(f.audit),"start_prepared")==1,"STOP after soft deadline does not retry initial start");return;
 }
 NativeBridgeTestAccess::receive(f.bridge,physical(receipt));
 check(f.controls.size()==1&&f.controls[0].toObject().value("applied").toBool(),"delayed exact physical ownership resolves SAME submitted attempt once");
 f.finish();check(count(actions(f.audit),"start_prepared")==1&&count(actions(f.audit),"application_start_inventory")==1,"diagnostic timeout issues no inventory poll, retry or second reservation");
}
void badAck(){
 Fixture f("auto");f.start();wait([&]{return count(actions(f.audit),"start_prepared")==1;});const auto receipt=action(actions(f.audit),"start_prepared").value("self_receipt").toObject();
 NativeBridgeTestAccess::receive(f.bridge,{{"type","control"},{"request_id",Request},{"device",A},{"error","SELF explicit transport failure"}});
 check(f.controls.size()==1&&!f.controls[0].toObject().value("applied").toBool()&&f.coordinator->status().contains("retained"),"negative control reply holds committed identity, no retry");
 NativeBridgeTestAccess::receive(f.bridge,physical(receipt));check(f.controls.size()==1,"late physical sample after cancellation cannot ACK removed pending request");
 f.finish();check(count(actions(f.audit),"start_prepared")==1,"failed control never implicitly retries/allocates");
}
void finiteAndMalformed(){Fixture f("auto");f.coordinator->control(A,"not-a-UUID",true);check(f.controls.size()==1&&!f.controls[0].toObject().value("applied").toBool(),"malformed explicit request fails before inventory");f.coordinator->control(B,Other,true);wait([&]{return count(actions(f.audit),"start")==1;});f.finish();const auto row=action(actions(f.audit),"start");check(row.value("device")==B&&count(actions(f.audit),"application_start_inventory")==0,"finite device path unchanged; malformed3090 never sent");}
QString oldResumeRow(Database& db){return db.value(("SELECT start||'|'||end||'|'||start_key||'|'||end_key||'|'||device||'|'||state||'|'||kind||'|'||created_utc FROM bookings WHERE id='"+Run+"'").toUtf8());}
QJsonObject resumedPhysical(const QJsonObject& command,const QJsonObject& receipt){
 auto s=resumeSnapshot("resume-valid",command.value("booking_id").toString());s.insert("type","snapshot");s.insert("state","RUNNING");s.insert("owned",true);s.insert("operator_stop",false);
 const auto sha=receipt.value("checkpoint").toObject().value("resume").toObject().value("checkpointSha256");
 auto m=s.value("manifest").toObject();m.insert("run",command.value("new_run"));m.insert("birth","2026-10-14T00:00:02.000Z");m.insert("pid",4343);m.insert("resume_previous_run",Run);m.insert("checkpoint_input_sha256",sha);s.insert("manifest",m);
 auto binding=s.value("binding").toObject();binding.insert("initial_start",QJsonObject{{"origin","INITIAL_START"},{"request_id",Other}});
 binding.insert("resume_start",QJsonObject{{"schema","pgn-checkpoint-resume-marker-1"},{"request_id",Request},{"observation_id",command.value("observation_id")},
  {"run",command.value("new_run")},{"previous_run",Run},{"checkpoint_sha256",sha}});s.insert("binding",binding);return s;
}
void checkpointResumeSelected(const QString& name){
 Fixture f(name);Database db(f.db);const auto old=oldResumeRow(db);const auto oldRaw=readOwn(f.temp.filePath("old-source.stdout"));
 const auto resumeExportPath=qEnvironmentVariable("PGN_TEST_RESUME_CALLER_EXPORT");const bool exportResume=name=="resume-valid"&&!resumeExportPath.isEmpty();
 if(exportResume)f.temp.setAutoRemove(false); // Any failed exported run retains its original private fixture.
 const auto oldHash=QCryptographicHash::hash(oldRaw,QCryptographicHash::Sha256);const auto bookings=db.value("SELECT count(*) FROM bookings");
 f.start();
 if(name=="resume-nonce"){
  wait([&]{return count(actions(f.audit),"application_start_inventory")==1;});const auto read=action(actions(f.audit),"application_start_inventory");
  auto wrong=resumeInventory(read,name,f.intent);wrong.insert("inventory_id",Other);NativeBridgeTestAccess::receive(f.bridge,wrong);
  check(f.controls.isEmpty()&&f.inventories==0&&count(actions(f.audit),"resume_prepared")==0,"wrong checkpoint inventory nonce grants no Ready/resume admission");
  check(db.value("SELECT count(*) FROM sqlite_master WHERE name='dispatch_resume_epochs'")=="0","stale checkpoint inventory cannot allocate an epoch");
  wrong=resumeInventory(read,name,f.intent);wrong.insert("request_id",Other);NativeBridgeTestAccess::receive(f.bridge,wrong);
  check(f.controls.isEmpty()&&f.inventories==0,"wrong request generation cannot consume the fresh resume nonce");
  NativeBridgeTestAccess::receive(f.bridge,resumeInventory(read,name,f.intent));
 }
 wait([&]{return count(actions(f.audit),"resume_prepared")==1||!f.controls.isEmpty();});
 check(count(actions(f.audit),"resume_prepared")==1,qPrintable("checkpoint resume command absent: "+f.coordinator->status()));
 const auto cmd=action(actions(f.audit),"resume_prepared"),receipt=cmd.value("self_resume_receipt").toObject(),cp=receipt.value("checkpoint").toObject(),tuple=cp.value("resume").toObject();
 check(cmd.value("self_resume_committed").toBool()&&cmd.value("self_resume_prepared").toBool()&&cmd.value("self_old_paused").toBool(),"actual SAME SQLite resume receipt/PREPARED/newrun and old PAUSED are committed before pipe callback");
 QJsonObject wire;for(const auto* field:{"action","device","request_id","observation_id","booking_id","old_run","new_run","base_birth","base_start","base_end"}){check(!cmd.value(field).isUndefined(),"all ten actual Bridge resume command fields present");wire.insert(field,cmd.value(field));}
 check(wire.size()==10&&wire.value("action")=="resume_prepared"&&wire.value("device")==A&&wire.value("booking_id")==f.intent&&wire.value("request_id")==Request,"resume command keeps same booking/device/request, not a new reservation");
 const auto next=wire.value("new_run").toString();bool canonical=next.size()==32;for(const auto c:next)canonical&=(c>='0'&&c<='9')||(c>='a'&&c<='f');
 check(canonical&&next!=Run&&wire.value("old_run")==Run&&wire.value("base_birth")==Utc&&wire.value("base_start")==ResumeStart&&wire.value("base_end")==ResumeEnd,"one NEW execution epoch preserves exact original binding/base, never only the remaining interval");
 check(receipt.size()==12&&receipt.value("schema")=="pgn-checkpoint-resume-epoch-1"&&receipt.value("origin")=="RESUME_CHECKPOINT"&&receipt.value("run")==next&&receipt.value("previous_run")==Run&&receipt.value("predecessor")==ResumePredecessor,"resume immutable receipt preserves completed predecessor separately from process lineage");
 check(receipt.value("counter_basis")=="PGNCG001_CUMULATIVE_BASELINE"&&receipt.value("raw_conservation_requirement")=="LINEAGE_RAW_CONSERVATION_REQUIRED"&&tuple.size()==13&&tuple.value("run")==Run,"resume declares cumulative baseline and unpaid lineage raw conservation, never new credit");
 check(cp.value("device")==A.toLower()&&QByteArray::fromBase64(cp.value("checkpoint_base64").toString().toLatin1())==resumeBytes()&&tuple.value("tests")=="9007199254740993"&&tuple.value("counterTimeNs")=="9007199254740995","native checkpoint64 and >2^53 cumulative counter strings are byte-exact");
 check(tuple.value("start")==ResumeStart&&tuple.value("end")==ResumeEnd&&tuple.value("positions")=="123"&&tuple.value("nextStart")==addUnsigned(ResumeStart,"123")&&tuple.value("remaining")=="9877","partial tuple has exact continuation arithmetic but keeps original binding bounds");
 check(f.controls.isEmpty()&&f.coordinator->status().contains("ACK is insufficient")&&f.samples==0,"successful resume command ACK is not physical ownership, nor raw-metrics publication");
 check(oldResumeRow(db)==old&&db.value("SELECT count(*) FROM bookings")==bookings,"admission preserves old runtime/range/date and creates no new physical or future booking");
 check(db.value(("SELECT state FROM bookings WHERE start='"+block(7)+"'").toUtf8())=="QUEUED","resume never claims the queued manual8 lookahead");
 BookingStore store(f.db);const auto saved=store.savedResumeCheckpoint(A,Run);const auto metadata=store.miningSessionMetadata(A);
 check(saved&&saved->checkpointBytes==resumeBytes()&&saved->resume.run==Run&&saved->resume.checkpointSha256==QString::fromLatin1(QCryptographicHash::hash(resumeBytes(),QCryptographicHash::Sha256).toHex()),"actual stopped checkpoint saved in SAME BookingStore before resume preparation");
 check(metadata&&metadata->mode==MiningSessionMode::ManualReservations&&!metadata->automaticAfterManual,"resume does not rewrite Manual/OFF into Auto");
 if(exportResume){
  try{
   const auto output=QDir::cleanPath(resumeExportPath);const QFileInfo target(output);const auto parent=target.dir();
   check(target.isAbsolute()&&!target.exists()&&!target.isSymLink()&&!target.fileName().isEmpty()&&parent.exists(),"resume export requires a NEW absolute directory with existing parent");
   check(parent.mkdir(target.fileName()),"create only named terminal resume test artifact directory");
   const auto native=store.savedCheckpointResumeEpoch(A,next);
   check(native.status==CheckpointResumeReadStatus::Present&&native.epoch&&native.epoch->run==next&&native.epoch->requestUUID==Request&&
    native.receiptUtf8==QJsonDocument(receipt).toJson(QJsonDocument::Compact),"export exact committed NativeStore resume receipt bytes, never a reconstructed receipt");
   sqlite3* copy{};const auto destination=(output+"/native-bookings.sqlite").toUtf8();
   const auto opened=sqlite3_open_v2(destination.constData(),&copy,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr);
   if(opened!=SQLITE_OK){if(copy)sqlite3_close(copy);check(false,"open own new resume SQLite backup destination");}
   auto* backup=sqlite3_backup_init(copy,"main",db.db,"main");
   if(!backup){sqlite3_close(copy);check(false,"initialize consistent actual committed native SQLite backup");}
   const auto copied=sqlite3_backup_step(backup,-1),finished=sqlite3_backup_finish(backup),closed=sqlite3_close(copy);
   check(copied==SQLITE_DONE&&finished==SQLITE_OK&&closed==SQLITE_OK,"actual PREPARED SQLite backup is complete and closed, not reconstructed fixture SQL");
   write(output+"/command.json",QJsonDocument(wire).toJson(QJsonDocument::Compact));
   write(output+"/receipt.json",native.receiptUtf8);
   write(output+"/snapshot.json",QJsonDocument(resumeSnapshot("resume-valid",f.intent)).toJson(QJsonDocument::Compact));
   const auto actualOldRaw=readOwn(f.temp.filePath("old-source.stdout"));check(actualOldRaw==oldRaw,"export reads actual unchanged old source bytes from fixture");write(output+"/old-source.stdout",actualOldRaw);
   write(output+"/fixture.json",QJsonDocument(QJsonObject{{"schema","pgn-closed-native-resume-caller-contract-1"},{"scope","TEST_ONLY_NO_RUNTIME_OR_PHYSICAL_PROOF"},
    {"artifacts",QJsonArray{"native-bookings.sqlite","command.json","receipt.json","snapshot.json","old-source.stdout","fixture.json"}},
    {"private_fixture_on_failure",f.temp.path()},{"profile_scope","FIXTURE_BINDING_HAS_NO_SDK_PINS_EXTERNAL_TEST_PROFILE_QUALIFICATION_REQUIRED"}}).toJson(QJsonDocument::Compact));
  }catch(...){std::cerr<<"TEST ONLY resume export failed; original fixture retained at "<<f.temp.path().toStdString()<<'\n';throw;}
 }
 for(int poison=0;poison<16;++poison){
  auto p=resumedPhysical(wire,receipt),m=p.value("manifest").toObject(),binding=p.value("binding").toObject(),marker=binding.value("resume_start").toObject();
  switch(poison){
   case 0:m.insert("run",Run);break;case 1:m.insert("resume_previous_run",ResumePredecessor);break;case 2:m.insert("checkpoint_input_sha256",QString(64,'0'));break;
   case 3:marker.insert("schema","unknown");break;case 4:marker.insert("request_id",Other);break;case 5:marker.insert("run",Run);break;
   case 6:marker.insert("previous_run",ResumePredecessor);break;case 7:marker.insert("checkpoint_sha256",QString(64,'0'));break;
   case 8:m.insert("start",tuple.value("nextStart"));binding.insert("start",tuple.value("nextStart"));break;
   case 9:binding.insert("queued_intent",Other);break;case 10:p.insert("owned","true");break;case 11:m.insert("pid",0);break;
   case 12:binding.insert("uuid",B);break;case 13:m.insert("birth",Utc+'\n');break;case 14:p.insert("state","STOPPING");break;
   case 15:marker.remove("checkpoint_sha256");break;
  }
  binding.insert("resume_start",marker);p.insert("manifest",m);p.insert("binding",binding);NativeBridgeTestAccess::receive(f.bridge,p);
  check(f.controls.isEmpty(),"old/foreign/partial-range/unknown resume epoch or checkpoint lineage cannot acknowledge ownership");
 }
 NativeBridgeTestAccess::receive(f.bridge,resumedPhysical(wire,receipt));check(f.controls.size()==1&&f.controls[0].toObject().value("applied").toBool(),"only exact NEW owned process plus resume lineage/checkpoint marker resolves physical pending");
 check(db.value(("SELECT state||'|'||run||'|'||predecessor FROM dispatch_requests WHERE id='"+f.intent+"'").toUtf8())=="PREPARED|"+next+"|"+ResumePredecessor,"physical callback alone never fabricates dispatch STARTED or rewrites predecessor");
 check(oldResumeRow(db)==old&&QCryptographicHash::hash(readOwn(f.temp.filePath("old-source.stdout")),QCryptographicHash::Sha256)==oldHash,"old source epoch bytes and old runtime dates/counters remain conserved through callback");
 f.coordinator->control(A,Other,true);wait([&]{return f.controls.size()>=2;});check(!f.controls.last().toObject().value("applied").toBool(),"existing prepared lineage requires reconciliation, not another worker");
 f.finish();const auto rows=actions(f.audit);check(count(rows,"resume_prepared")==1&&count(rows,"start_prepared")==0&&count(rows,"start")==0&&count(rows,"dispatch")==0,"resume emits exactly one resume frame and zero initial/legacy/future-dispatch frames");
 if(exportResume)f.temp.setAutoRemove(true); // Only successful entire SELF run may discard its private original; terminal exports remain.
}
void checkpointResumeRefused(const QString& name){
 Fixture f(name);Database db(f.db);const auto old=oldResumeRow(db);const auto oldRaw=readOwn(f.temp.filePath("old-source.stdout"));
 f.start();f.terminal();check(!f.controls[0].toObject().value("applied").toBool(),"unknown resume capability/capture/source/physical DB remains Held");
 f.finish();const auto rows=actions(f.audit);check(count(rows,"resume_prepared")==0&&count(rows,"start_prepared")==0&&count(rows,"start")==0&&count(rows,"dispatch")==0,"invalid resume evidence cannot launch or fall back to any future block");
 check(oldResumeRow(db)==old&&readOwn(f.temp.filePath("old-source.stdout"))==oldRaw,"refused resume preserves exact old physical range/state/date/source bytes");
 check(db.value(("SELECT state||'|'||run||'|'||predecessor FROM dispatch_requests WHERE id='"+f.intent+"'").toUtf8())=="STARTED|"+Run+"|"+ResumePredecessor,"refused resume leaves original dispatch pointer and predecessor unchanged");
 const auto hasEpoch=db.value("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='dispatch_resume_epochs'")=="1";
 check(!hasEpoch||db.value("SELECT count(*) FROM dispatch_resume_epochs")=="0","failed/unknown admission publishes no new resume epoch");
 check(db.value("SELECT count(*) FROM bookings WHERE state='SOURCE_FINISHED'")=="0"&&db.value("SELECT count(*) FROM booking_metadata WHERE key='fixture/resume-sideeffect'")=="0","partial checkpoint is not full SOURCE and failed epoch trigger sideeffect rolls back");
 if(name=="resume-no-protocol")check(db.value("SELECT count(*) FROM booking_metadata WHERE key LIKE 'mining-resume/%'")=="0","old controller without resume protocol holds BEFORE any checkpoint/epoch write");
}
}
namespace {
QString phaseUuid(int n){return "90000000-0000-4000-8000-"+QString::number(n,16).rightJustified(12,'0');}
void phaseLoop(int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
void nativeInventoryPhases(const QString& nativePhase){
 currentCase="native-inventory-phase-"+nativePhase;QTemporaryDir temp;check(temp.isValid(),"own native-phase temp paths");BookingStore store(temp.filePath("booking.sqlite"));check(store.valid(),"real own SAME SQLite, no admission created");
 QList<QJsonObject> commands;NativeBridge bridge;auto lifetime=std::make_unique<QObject>();QObject foreign;
 check(bridge.attachNativeRuntime(lifetime.get(),[&](const auto& p){commands.append(p);return true;}),"native creating-thread owner attached without process");
 int callbacks{};bridge.onApplicationInventoryPhase=[&](const auto&){++callbacks;};
 ApplicationMiningStart start(&bridge,temp.filePath("booking.sqlite"),temp.path());start.control(A,Request,true);wait([&]{return !commands.isEmpty();});
 auto* timer=start.findChild<QTimer*>("initial-start-deadline");check(timer&&timer->isActive(),"actual frontend deadline discoverable");
 QString request=Request,inventory=commands.last().value("inventory_id").toString();
 const auto packet=[&](const QString& id,const QString& phase,bool active){return QJsonObject{{"type","application_inventory_phase"},{"request_id",request},{"inventory_id",inventory},{"purpose","start"},{"phase",phase},{"phase_id",id},{"active",active}};};
 const auto begin=packet(phaseUuid(1),"profile",true);
 NativeBridgeTestAccess::receive(bridge,begin);check(callbacks==0,"Node/observer receive cannot manufacture native phase");
 check(!bridge.acceptNativeRuntimePacket(&foreign,begin),"wrong native lifetime rejected");
 for(const auto& delta:QList<QJsonObject>{{{"active","true"}},{{"purpose","boundary"}},{{"phase","unknown"}},{{"inventory_id",Other}},{{"phase_id","not-UUID"}},{{"extra",1}}}){
  auto bad=begin;for(auto it=delta.begin();it!=delta.end();++it)bad[it.key()]=it.value();check(!bridge.acceptNativeRuntimePacket(lifetime.get(),bad),"exact typed phase/correlation/keys required");
 }
 check(bridge.acceptNativeRuntimePacket(lifetime.get(),begin)&&callbacks==1,"actual native profile phase delivered once");
 check(!bridge.acceptNativeRuntimePacket(lifetime.get(),begin)&&callbacks==1,"duplicate active phase cannot renew timer");
 check(bridge.acceptNativeRuntimePacket(lifetime.get(),packet(phaseUuid(1),"profile",false)),"profile end exact pairing");
 // Compare the conserved observed budget: a CoarseTimer may coalesce its
 // scheduled timeout beyond the requested 25 ms interval on some platforms.
 timer->setInterval(25);const int observedBudget=timer->remainingTime();
 check(bridge.acceptNativeRuntimePacket(lifetime.get(),packet(phaseUuid(2),nativePhase,true)),"actual lineage begins before shortened UI deadline");
 check(!timer->isActive(),"only UI deadline suspended, no physical fact emitted");phaseLoop(60);
 check(!start.status().contains("deadline expired")&&!timer->isActive(),"RAW lineage may outlast UI budget without false timeout");
 check(!bridge.acceptNativeRuntimePacket(lifetime.get(),packet(phaseUuid(3),nativePhase,false)),"unmatched phase end cannot release lease");
 check(!bridge.acceptNativeRuntimePacket(lifetime.get(),packet(phaseUuid(4),"snapshot",true)),"new phase cannot overwrite an active lineage lease");
 check(bridge.acceptNativeRuntimePacket(lifetime.get(),packet(phaseUuid(2),nativePhase,false)),"real lineage end accepted");
 std::cout<<"TIMER_BUDGET phase="<<nativePhase.toStdString()<<" requested_ms=25 observed_ms="<<observedBudget<<" resumed_ms="<<timer->interval()<<'\n';
 check(timer->isActive()&&timer->interval()>0&&timer->interval()<=observedBudget,"end resumes saved observed budget, not fresh45s");
 const auto snapshot=packet(phaseUuid(4),"snapshot",true);check(bridge.acceptNativeRuntimePacket(lifetime.get(),snapshot)&&timer->interval()==45000,"new actual snapshot phase resumes45s lifecycle budget only");
 timer->setInterval(25);check(!bridge.acceptNativeRuntimePacket(lifetime.get(),snapshot)&&timer->interval()==25,"replayed snapshot cannot postpone actual short timer");phaseLoop(60);
 check(start.status().contains("deadline expired")&&!timer->isActive(),"without a genuine next phase actual deadline holds");
 check(!bridge.acceptNativeRuntimePacket(lifetime.get(),packet(phaseUuid(5),"snapshot",true)),"expired/cancelled request cannot be resurrected");
 const auto oldCommands=commands.size();request=Other;start.control(A,request,true);wait([&]{return commands.size()>oldCommands;});inventory=commands.last().value("inventory_id").toString();
 check(!bridge.acceptNativeRuntimePacket(lifetime.get(),packet(phaseUuid(4),"profile",true)),"phaseUUID cannot replay under a new request/nonce");
 check(bridge.acceptNativeRuntimePacket(lifetime.get(),packet(phaseUuid(6),nativePhase,true)),"new request lineage independently admitted");
 const auto oldInventory=bridge.onApplicationStartInventory;
 bridge.onApplicationStartInventory=[&](const auto& id,const auto& p){check(!bridge.acceptNativeRuntimePacket(lifetime.get(),packet(phaseUuid(7),"profile",true)),"phase latch closed BEFORE terminal callback");if(oldInventory)oldInventory(id,p);};
 check(bridge.acceptNativeRuntimePacket(lifetime.get(),{{"type","application_start_inventory"},{"request_id",request},{"inventory_id",inventory},{"accepted",false},{"error","PUBLIC_TEST_ONLY terminal refusal"}}),"actual terminal channel handled");
 check(start.status().contains("held",Qt::CaseInsensitive)&&!timer->isActive(),"terminal refusal leaves no suspended budget");
 bridge.onApplicationStartInventory=oldInventory;
 const auto beforeLoss=commands.size();request=phaseUuid(100);start.control(A,request,true);wait([&]{return commands.size()>beforeLoss;});inventory=commands.last().value("inventory_id").toString();
 check(bridge.acceptNativeRuntimePacket(lifetime.get(),packet(phaseUuid(8),nativePhase,true)),"owner-loss lineage entered");lifetime.reset();
 check(start.status().contains("held",Qt::CaseInsensitive)&&!timer->isActive(),"native owner loss closes lease and holds without fake completion");
 check(commands.size()==3,"no launch/control retry or extra work produced by phase callbacks");
}
}
void retainedCheckpointOrigin(){
 Fixture f("resume-valid");BookingStore store(f.db);
 const auto snapshot=resumeSnapshot("resume-valid",f.intent);
 const auto packet=decodeSourceStopCheckpointPacket(snapshot,Run,A,ResumeLabel);
 check(packet.evidence.has_value(),"real old stopped source fixture decoded");
 const auto checked=validateSourceStopCheckpoint(*packet.evidence);check(checked.resume.has_value(),"real old stopped tuple checked");
 check(store.saveSourceRunManifest(A,snapshot.value("manifest").toObject()).isEmpty(),"old original source retained");
 check(store.saveResumeCheckpoint(A,*checked.resume,packet.evidence->checkpointBytes).isEmpty(),"old checkpoint retained");
 const QString next=QString(32,'e'),origin="12345678-1234-4567-89ab-123456789abc";
 const auto saved=store.prepareCheckpointResume(Request,A,f.intent,Run,next,checked.resume->checkpointSha256,origin);
 check(saved.status==CheckpointResumeAdmissionStatus::Prepared,"real committed never-submitted resume retained");
 f.start();wait([&]{return count(actions(f.audit),"resume_prepared")==1||!f.controls.isEmpty();});
 check(count(actions(f.audit),"resume_prepared")==1,qPrintable("retained checkpoint resume absent: "+f.coordinator->status()));
 const auto command=action(actions(f.audit),"resume_prepared");
 check(command.value("observation_id")==origin&&command.value("new_run")==next,"fresh inventory retains exact old origin and allocated successor");
 const auto after=store.savedCheckpointResumeEpoch(A,next);
 check(after.receiptUtf8==saved.receiptUtf8&&f.controls.isEmpty(),"immutable receipt unchanged, ACK not physical success");
 f.finish();
}
void retainedInitialOrigin(bool submitted){
 Fixture f("auto");BookingStore store(f.db);const auto registry=loadSourceRegistry(f.temp.path());const QString observation="12345678-1234-4567-89ab-123456789abc";
 const auto prior=store.prepareInitialStart(Request,A,{First,Last,First,Last,First,Width},registry,{WorkBoundary::Ready,observation});check(prior.status==InitialStartStatus::Prepared&&prior.dispatch.has_value(),"retain actual original initial PREPARED, not a fabricated replay DTO");
 Database db(f.db);const auto sql=("SELECT value FROM booking_metadata WHERE key='mining-initial-start/v1/"+Request+"'").toUtf8();const auto before=db.value(sql);
 if(submitted)db.sql(("UPDATE dispatch_requests SET state='STARTED',run='"+Run+"' WHERE id='"+prior.dispatch->bookingId+"'").toUtf8());
 f.start();
 if(submitted){f.terminal();check(count(actions(f.audit),"start_prepared")==0&&f.coordinator->status().contains("held",Qt::CaseInsensitive),"changed STARTED provenance cannot be promoted into a second start");}
 else{wait([&]{return count(actions(f.audit),"start_prepared")==1;});const auto command=action(actions(f.audit),"start_prepared");check(command.value("observation_id")==observation&&command.value("request_id")==Request,"NEW inventory uses immutable retained origin and same request, not another reservation");f.start();check(count(actions(f.audit),"start_prepared")==1,"repeated input while correlated physical outcome pending cannot duplicate control");}
 check(db.value(sql)==before&&db.value("SELECT count(*) FROM bookings")=="1","original receipt bytes and exactly one booking preserved across fresh reconciliation");f.finish();
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);const auto args=app.arguments();if(args.size()==3&&args[1]=="--self-mining-start")return self(args[2]);try{
 retainedInitialOrigin(false);retainedInitialOrigin(true);retainedCheckpointOrigin();
 for(const auto* name:{"auto","manual","full","source-only"})selected(QString::fromLatin1(name));
 for(const auto* name:{"unknown","missing-owned","checkpoint-unknown","checkpoint","missing-exit","bad-scope","prepared","running-bad-pid","running-birth-newline","running-observed-newline","missing-conservation","conservation-string","source-bool","source-scope","wrong-exit","foreign-binding","commit-fail","partial-count","receipt-fail","full-no-lineage","full-held-lineage"})refused(QString::fromLatin1(name));
 running();cancelled("stop");cancelled("closing");nonceAndDeadline();physicalDeadlineRetainsCorrelation();physicalDeadlineRetainsCorrelation(true);badAck();finiteAndMalformed();nativeInventoryPhases("lineage");nativeInventoryPhases("source_read");
 checkpointResumeSelected("resume-valid");checkpointResumeSelected("resume-nonce");
 for(const auto* name:{"resume-no-protocol","resume-capture","resume-bytes","resume-footer","resume-lineage","resume-owned-unknown","resume-not-conserved","resume-old-physical","resume-commit-fail"})checkpointResumeRefused(QString::fromLatin1(name));
 std::cout<<"Application mining initial start: "<<checks<<" checks / 0 failures; actual QtCore NativeBridge own SELF + temporary SAME SQLite/source fixtures; NO QPA/UI/Node/SSH/GPU/runtime\n";return 0;
 }catch(const std::exception& e){std::cerr<<"Application mining initial start failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}

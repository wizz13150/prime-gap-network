#include "mining/application_mining_close.h"
#include "mining/native_bridge.h"
#include "booking/booking_store.h"
#include <QApplication>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QWidget>
#include <sqlite3.h>
#include <cstdio>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

namespace pgn::mining {struct NativeBridgeTestAccess {static QProcess& process(NativeBridge& b){return b.relay_;}static void receive(NativeBridge& b,const QJsonObject& p){b.receive(p);}};}
using namespace pgn::mining;
namespace {
int checks{};
const QString A="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",B="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265";
const QString Run="1234567890abcdef1234567890abcdef",Start="184467440737095516260",End="184467440737095526260";
const QString Booking="10000000-0000-4000-8000-000000000001";
const QString Label="C:/PUBLIC-FIXTURE/current-job/checkpoint",Utc="2026-10-14T00:00:01Z";
void require(bool ok,const char* s){++checks;if(!ok)throw std::runtime_error(s);}
QString closePhaseUuid(int n){return "91000000-0000-4000-8000-"+QString::number(n,16).rightJustified(12,'0');}
void closePhaseLoop(int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
void sql(const QString&,const QByteArray&);
std::optional<QString> storedState(const QString& path,const QString& table,const QString& id){
 require(table=="bookings"||table=="dispatch_requests","own fixed state table");
 sqlite3* db{};const int opened=sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READONLY,nullptr);
 if(opened!=SQLITE_OK){if(db)sqlite3_close(db);throw std::runtime_error("own readonly state database");}
 sqlite3_stmt* q{};const auto text=("SELECT state FROM "+table+" WHERE id=?").toUtf8();
 const int prepared=sqlite3_prepare_v2(db,text.constData(),-1,&q,nullptr);
 if(prepared==SQLITE_OK)sqlite3_bind_text(q,1,id.toUtf8().constData(),-1,SQLITE_TRANSIENT);
 const int step=prepared==SQLITE_OK?sqlite3_step(q):SQLITE_ERROR;std::optional<QString> value;
 const bool valid=step==SQLITE_DONE||(step==SQLITE_ROW&&sqlite3_column_type(q,0)==SQLITE_TEXT);
 if(step==SQLITE_ROW&&valid)value=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(q,0)));
 if(q)sqlite3_finalize(q);sqlite3_close(db);require(valid,"own exact stored state");return value;
}
void emitPacket(const QJsonObject& p){const auto bytes=QJsonDocument(p).toJson(QJsonDocument::Compact)+'\n';std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);std::fflush(stdout);}
QJsonObject snapshot(bool final,const QString& scenario){
 const bool idle=scenario=="source-only-idle"||scenario=="unmanaged-spoof"||scenario=="idle"||scenario.startsWith("driver-idle-")||scenario=="prepared-cold"||scenario.startsWith("finite-partial-");QJsonObject m{{"schema","pgn-native-process-1"},{"run",Run},{"uuid",A},{"start",Start},{"end",End}};
 QJsonObject binding{{"uuid",A},{"start",Start},{"end",End}};
 if(scenario.startsWith("completion-queued")){
  m.insert("queued_intent",final&&scenario=="completion-queued-poison"?QJsonValue(123):QJsonValue(Booking));
  binding.insert("queued_intent",final&&scenario=="completion-queued-rebound"?QString("OTHER"):Booking);
 }
 if(final&&scenario=="run-switch")m.insert("run",QString(32,'b'));
 QJsonObject s{{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},{"driver_valid",true},{"observed_utc",Utc},
  {"operator_stop",final},{"binding",binding},{"manifest",idle?QJsonValue(QJsonValue::Null):QJsonValue(m)},
  {"last_exit",QJsonObject{{"run",Run},{"exit_code",0}}},{"next_offset",1000},{"pending_bytes",0}};
 if(scenario.startsWith("driver-"))s.insert("driver_valid",scenario=="driver-idle-unknown"?QJsonValue(QJsonValue::Null):QJsonValue(false));
 if(scenario=="driver-active-nodriver"){s.insert("owned",true);s.insert("state","RUNNING");}
 QByteArray bytes="PGNCG001";for(const quint64 n:{100ULL,10ULL,10000ULL,123ULL,9007199254740993ULL,7ULL,2ULL})for(unsigned i=0;i<8;++i)bytes.append(char((n>>(8*i))&255));
 const auto tail="[pgn-counters] time_ns=9007199254740995 positions=123 gaps=7 tests=9007199254740993 batches=2 processed_gaps=7\n[phase0-gpu] stopped positions=123 checkpoint="+Label+"\n";
 QJsonObject capture{{"schema","pgn-stop-checkpoint-evidence-1"},{"status","captured"},{"error",""},{"run",Run},{"checkpoint_label",Label},{"checkpoint_base64",QString::fromLatin1(bytes.toBase64())},{"source_tail_utf8",tail}};
 if(final&&scenario=="bad-proof")capture.insert("checkpoint_label","wrong-label");s.insert("checkpoint_evidence",capture);
 if(final&&scenario.startsWith("completion"))s.insert("source_completion_observation",QJsonObject{{"schema","pgn-source-range-completion-1"},{"run",Run},{"start",Start},{"end",End},{"source_positions","10000"},{"source_range_complete",true},{"source_exit_code",0},{"independent_coverage",false},{"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"}});
  if(final&&scenario.startsWith("completion-queued")&&scenario!="completion-queued-lineage-missing"){
   // TESTONLY peer ABI; this fixture does not scan real SOURCE or qualify image.
   QJsonObject proof{{"schema","pgn-source-resume-lineage-1"},{"ready",true},{"error",""},{"terminal","complete"},{"run",Run},{"booking_id",Booking},
    {"start",Start},{"end",End},{"epochs",1},{"chain_sha256",QString(64,'d')},{"scope","LOCAL_RAW_LINEAGE_COUNTER_DELTAS_NOT_INDEPENDENT_PRIME_COVERAGE"}};
   if(scenario=="completion-queued-lineage-held-a73"){
    proof.insert("ready",false);proof.insert("error","v2 terminal group Held");proof.insert("source_boundary_protocol","pgn-source-boundary-v2");proof.insert("source_boundary_groups","1");
    binding.insert("executable","C:/PUBLIC-TESTONLY/A73.exe");binding.insert("pins",QJsonArray{QJsonObject{{"path","C:/PUBLIC-TESTONLY/A73.exe"},{"sha256","A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43"}}});s.insert("binding",binding);
   }
   s.insert("resume_lineage",proof);
  }
 return s;
}
QJsonObject inventory(const QJsonObject& command,int n,const QString& scenario){
 const bool final=n>1;const auto s=snapshot(final,scenario);
 QJsonObject finite{{"schema","pgn-device-control-observation-1"},{"device",B},{"scope","BOUNDED_SCIENCE_TEST"},{"owned",!final&&scenario=="finite-active"},{"desired_enabled",!final},{"observed_utc",Utc},{"manifest",QJsonValue(QJsonValue::Null)}};
 if(scenario=="finite-unknown-retry"&&n>1){finite.insert("manifest",QJsonObject{{"run",QString(32,'2')}});finite.insert("last_exit",QJsonObject{{"run",QString(32,'2')},{"exit_code",0}});}
 if(scenario.startsWith("finite-partial-")){
  const auto epoch=QString(32,scenario=="finite-partial-changed"&&n>1?'3':'2');
  finite.insert("owned",n==1&&scenario=="finite-partial-owned");finite.insert("desired_enabled",false);
  // Historical already-ended run, not a synthetic completion/checkpoint.
  // Its manifest/exit are immutable across the pre/post-STOP observations.
  finite.insert("manifest",QJsonObject{{"run",epoch},{"uuid",B},{"pid",1234},{"birth","2026-10-13T00:00:00.0000000Z"},
   {"image_sha256","A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43"},
   {"start","199999990000000000000"},{"length","1030792151040"},
   {"stop","C:/PGN/runtime/devices/test-3060/jobs/"+epoch+".stop"},
   {"stdout","C:/PGN/runtime/devices/test-3060/jobs/"+epoch+".stdout"},
   {"stderr","C:/PGN/runtime/devices/test-3060/jobs/"+epoch+".stderr"},{"request_id","PUBLIC-FIXTURE"}});
  finite.insert("last_exit",QJsonObject{{"run",epoch},{"exit_code",0},{"utc","2026-10-13T00:00:04.0000000Z"}});
  if(n==1&&scenario=="finite-partial-malformed")finite.insert("last_exit",QJsonObject{{"run",epoch},{"exit_code","0"}});
 }
 QJsonObject conservation{{"schema","pgn-source-conservation-1"},{"scope","LOGICAL_SOURCE_EPOCH_CONSERVATION_NOT_GLOBAL_POWER_CUT_DURABILITY"},{"run",Run},{"source","native/"+Run+".stdout"},{"ready",scenario!="not-conserved"},{"required_end",1000},{"transport_offset",1000},{"committed_offset",1000},{"mirror_size",1000}};
 if(scenario=="excess-offset")conservation.insert("mirror_size",1001);
 QJsonObject p{{"type","application_close_inventory"},{"request_id",command.value("request_id")},{"inventory_id",command.value("inventory_id")},
  {"accepted",true},{"inventory_known",true},{"constant_known",true},{"snapshot",s},{"finite_device",QJsonObject{{"device",B},{"status","observed"},{"snapshot",finite}}},{"unknown_devices",QJsonArray{}},{"source_conservation",conservation}};
 if(scenario=="source-only-idle"||scenario=="unmanaged-spoof"){p.insert("managed_devices",QJsonArray{A});p.insert("finite_device",QJsonObject{{"device",B},{"status","unmanaged"}});}
 if(scenario=="unknown"||(scenario=="finite-unknown-retry"&&n==1)){p.insert("accepted",false);p.insert("inventory_known",false);p.insert("unknown_devices",QJsonArray{B});}
 if(scenario.startsWith("finite-partial-")&&n==1){p.insert("accepted",false);p.insert("inventory_known",false);p.insert("constant_known",false);p.insert("snapshot",QJsonValue::Null);p.insert("unknown_devices",QJsonArray{A});}
 if(scenario=="finite-partial-contradictory-unknown"&&n==1)p.insert("unknown_devices",QJsonArray{B});
 if(scenario=="finite-partial-wrong-device"&&n==1){auto f=p.value("finite_device").toObject();f.insert("device",A);p.insert("finite_device",f);}
 if(scenario=="malformed")p.remove("constant_known");return p;
}
int self(const QString& config){
 QFile f(config);if(!f.open(QIODevice::ReadOnly))return 20;const auto c=QJsonDocument::fromJson(f.readAll()).object();const auto scenario=c.value("scenario").toString();QFile audit(c.value("audit").toString());if(!audit.open(QIODevice::WriteOnly|QIODevice::Append))return 21;
 int reads=0;std::string line;
 while(std::getline(std::cin,line)){
  const auto raw=QByteArray::fromStdString(line);audit.write(raw+'\n');audit.flush();const auto command=QJsonDocument::fromJson(raw).object();const auto action=command.value("action").toString();
  if(action=="application_close_prepare")emitPacket({{"type","application_close_frozen"},{"request_id",command.value("request_id")},{"accepted",scenario!="freeze-refused"},{"error","SELF refusal"}});
  if(action=="stop")emitPacket({{"type","control"},{"request_id",command.value("request_id")},{"device",command.value("device")},{"error","SELF weak negative ACK, not physical evidence"}});
  if(action=="application_close_inventory"){
   ++reads;auto p=inventory(command,reads,scenario);
   if(scenario=="prepared-final"&&reads==2)sql(c.value("database").toString(),"INSERT INTO dispatch_requests VALUES('FINAL-PENDING','100','200','PUBLIC','PREPARED',NULL,NULL,'FIXTURE');");
   if(scenario=="partial"&&reads==2){auto stale=p;stale.insert("inventory_id","aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");auto bad=stale.value("snapshot").toObject();bad.insert("manifest",QJsonObject{});stale.insert("snapshot",bad);emitPacket(stale);}
   emitPacket(p);
  }
 }return 0;
}
void wait(const std::function<bool()>& done){
 QElapsedTimer timer;timer.start();while(!done()&&timer.elapsed()<5000){QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}require(done(),"closed SELF/event-loop deadline");
}
void sql(const QString& file,const QByteArray& command){sqlite3* db{};if(sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE,nullptr)!=SQLITE_OK)throw std::runtime_error("own SQLite open");const auto rc=sqlite3_exec(db,command.constData(),nullptr,nullptr,nullptr);sqlite3_close(db);if(rc!=SQLITE_OK)throw std::runtime_error("own SQLite statement");}
QByteArray preserved(const QString& file){
 sqlite3* db{};if(sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)throw std::runtime_error("own SQLite read");sqlite3_stmt* q{};sqlite3_prepare_v2(db,"SELECT key,value FROM booking_metadata WHERE key NOT LIKE 'mining-resume/%' ORDER BY key",-1,&q,nullptr);QByteArray result;
 while(sqlite3_step(q)==SQLITE_ROW)for(int i=0;i<2;++i){result+=QByteArray(reinterpret_cast<const char*>(sqlite3_column_text(q,i)),sqlite3_column_bytes(q,i));result+='\n';}sqlite3_finalize(q);sqlite3_close(db);return result;
}
class Window final:public QWidget {public:int closeEvents{};void closeEvent(QCloseEvent* e)override{++closeEvents;e->accept();}};
QVector<QJsonObject> actions(const QString& file){QFile f(file);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("own SELF audit");QVector<QJsonObject> rows;for(const auto& line:f.readAll().split('\n'))if(!line.isEmpty())rows.push_back(QJsonDocument::fromJson(line).object());return rows;}
void scenario(const QString& name,bool expectedReady,const QString& drainMode={}){
 QTemporaryDir temp;require(temp.isValid(),"own temporary SQLite/config");const auto db=temp.filePath("local-bookings.sqlite"),config=temp.filePath("self.json"),audit=temp.filePath("commands.jsonl");
 {pgn::booking::BookingStore store(db);require(store.valid(),"actual local store initialized");require(store.setMiningSessionMode(A,MiningSessionMode::ManualReservations).isEmpty()&&store.setAutomaticAfterManual(A,true).isEmpty(),"seed actual preserved mode/preference");}
 sql(db,"INSERT INTO booking_metadata VALUES('fixture/unrelated','KEEP');");const auto before=preserved(db);
 if(name!="missing-dispatch")sql(db,"CREATE TABLE dispatch_requests(id TEXT PRIMARY KEY,start TEXT NOT NULL,end TEXT NOT NULL,device TEXT NOT NULL,state TEXT NOT NULL,predecessor TEXT,run TEXT,created_utc TEXT NOT NULL);");
 if(name.startsWith("completion-queued")){
  require(Start.size()==21&&End.size()==21,"fixture decimal key lengths");
  sql(db,"INSERT INTO bookings VALUES('"+Booking.toUtf8()+"','"+Start.toUtf8()+"','"+End.toUtf8()+"','021"+Start.toUtf8()+"','021"+End.toUtf8()+"','DISPATCHED','LOCAL_INTENT','"+A.toUtf8()+"','"+Utc.toUtf8()+"');");
  sql(db,"INSERT INTO dispatch_requests VALUES('"+Booking.toUtf8()+"','"+Start.toUtf8()+"','"+End.toUtf8()+"','"+A.toUtf8()+"','STARTED',NULL,'"+Run.toUtf8()+"','"+Utc.toUtf8()+"');");
  if(name=="completion-queued-commit-fail")sql(db,"CREATE TRIGGER fixture_source_fail BEFORE UPDATE OF state ON bookings WHEN NEW.kind='LOCAL_INTENT' AND NEW.state='SOURCE_FINISHED' BEGIN SELECT RAISE(ABORT,'source fixture failure'); END;");
  if(name=="completion-queued-missing")sql(db,"DELETE FROM dispatch_requests;");
 }
 if(name=="prepared"||name=="prepared-cold")sql(db,"INSERT INTO dispatch_requests VALUES('PENDING','100','200','PUBLIC','PREPARED',NULL,NULL,'FIXTURE');");
 if(name=="commit-fail")sql(db,"CREATE TRIGGER fixture_resume_fail AFTER INSERT ON booking_metadata WHEN NEW.key LIKE 'mining-resume/%' BEGIN INSERT INTO booking_metadata VALUES('fixture/sideeffect','rollback'); SELECT RAISE(FAIL,'SELF commit failure'); END;");
 QFile file(config);require(file.open(QIODevice::WriteOnly),"own SELF config");file.write(QJsonDocument(QJsonObject{{"scenario",name},{"audit",audit},{"database",db}}).toJson(QJsonDocument::Compact));file.close();
 Window window;NativeBridge bridge;int priorControls=0;bridge.onControl=[&](const QString&,const QString&,bool,const QString&){++priorControls;};
 bridge.start(QCoreApplication::applicationFilePath(),"--self-mining-close",config);auto& process=NativeBridgeTestAccess::process(bridge);require(process.waitForStarted(3000),"only own SELF client starts");
 ApplicationMiningClose close(&window,QCoreApplication::instance(),&bridge,db,Label,nullptr,name!="source-only-idle");QString last;close.onStatus=[&](const QString& s){last=s;};
 std::function<void()> drainDone;
 if(drainMode=="delayed")close.onDrainBeforeQuit=[&](auto done){drainDone=std::move(done);};
 if(drainMode=="throw")close.onDrainBeforeQuit=[](auto){throw std::runtime_error("PUBLIC-TESTONLY drain failure");};
 QCloseEvent closeEvent;QCoreApplication::sendEvent(&window,&closeEvent);require(!closeEvent.isAccepted()&&window.closeEvents==0&&!close.readyToQuit(),"real Close intercepted while pending");
 QEvent quit(QEvent::Quit);QCoreApplication::sendEvent(QCoreApplication::instance(),&quit);require(!quit.isAccepted()&&!close.readyToQuit(),"real Quit intercepted before Ready");
 wait([&]{return bool(drainDone)||close.readyToQuit()||close.status().startsWith("Close held:");});
 if(name.startsWith("finite-partial-")){
  require(!close.readyToQuit()&&close.status().contains("inventory"),"partial inventory never grants quit; both STOPs still required");
  close.requestClose();wait([&]{return close.readyToQuit()||close.status().startsWith("Close held:");});
 }
 if(drainDone){
  require(!close.readyToQuit()&&window.closeEvents==0,"Physical gate alone cannot quit while native observers still active");
  QCloseEvent blocked;QCoreApplication::sendEvent(&window,&blocked);require(!blocked.isAccepted()&&window.closeEvents==0,"Window close remains intercepted during native observer drain");
  QString request,inventory;for(const auto& row:actions(audit))if(row.value("action")=="application_close_inventory"){request=row.value("request_id").toString();inventory=row.value("inventory_id").toString();}
  auto* budget=close.findChild<QTimer*>("application-close-deadline");require(budget&&!budget->isActive(),"Draining has no active global UI budget");
  // Consumer guard only: no claim that the Node SELF can emit native phases.
  if(bridge.onApplicationInventoryPhase)bridge.onApplicationInventoryPhase({request,inventory,closePhaseUuid(99),NativeInventoryPurpose::Close,NativeInventoryPhaseKind::Snapshot,true});
  require(!budget->isActive()&&!close.readyToQuit(),"Even exact late phase cannot rearm or finish Draining");
  drainDone();drainDone();wait([&]{return close.readyToQuit();});
 }
  const auto terminalStatus=close.status(); // Capture before the explicit retry Close event below.
  require(close.readyToQuit()==expectedReady,"expected typed close outcome");require(!last.isEmpty(),"status observable to application");
 if(drainMode=="throw")require(close.status().contains("native observer cleanup failed"),"Failed cleanup cannot acquire quit permission");
 if(name=="prepared-cold")require(window.closeEvents==0&&close.status().contains("PREPARED"),"cold OFF cannot erase uncertain PREPARED launch or grant quit after local freeze");
 if(name=="finite-unknown-retry"){close.requestClose();wait([&]{return close.status().startsWith("Close held:");});require(!close.readyToQuit()&&close.status().contains("unknown inventory"),"explicit retry cannot erase unknown finite STOP provenance");}
 process.closeWriteChannel();wait([&]{return process.state()==QProcess::NotRunning;});require(process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0,"own SELF clean exit");
 const auto rows=actions(audit);int inventories=0,stops=0;QStringList stopDevices,nonces;QString closeId;
 for(const auto& row:rows){const auto action=row.value("action").toString();require(QStringList{"module_list","application_close_prepare","application_close_inventory","stop"}.contains(action),"no start/dispatch/advance/control extension submitted");
  if(action=="application_close_prepare")closeId=row.value("request_id").toString();
  if(action=="stop"){++stops;stopDevices.append(row.value("device").toString());const auto id=row.value("request_id").toString();require(QUuid::fromString(id).toString(QUuid::WithoutBraces)==id,"STOP UUID canonical");}
  if(action=="application_close_inventory"){++inventories;const auto nonce=row.value("inventory_id").toString();require(QUuid::fromString(nonce).toString(QUuid::WithoutBraces)==nonce&&!nonces.contains(nonce),"fresh unique inventory nonce");nonces.append(nonce);}
 }
 require(inventories<=4,"per-attempt inventory bound");
 const int expectedStops=name=="source-only-idle"?1:name=="finite-unknown-retry"||name.startsWith("finite-partial-")?4:2;
 if(name!="freeze-refused"){require(stops==expectedStops&&stopDevices.contains(A)&&(name=="source-only-idle"?!stopDevices.contains(B):stopDevices.contains(B)),"both existing endpoints STOP even initially OFF/unknown");require(priorControls==expectedStops,"previous UI control callback chained, not stolen");}
 else require(stops==0,"no inferred freeze when refused");
 if(expectedReady){QCloseEvent released;QCoreApplication::sendEvent(&window,&released);require(released.isAccepted()&&window.closeEvents>0,"Close released ONLY after Ready");}
 else {QCloseEvent held;QCoreApplication::sendEvent(&window,&held);require(!held.isAccepted(),"Held cannot silently close window");}
 {pgn::booking::BookingStore store(db);const auto saved=store.savedResumeCheckpoint(A,Run);
  if(name=="partial"||name=="driver-partial")require(saved&&saved->resume.tests=="9007199254740993"&&saved->checkpointBytes.size()==64,"actual checkpoint committed and read back exact");
  else if(name=="prepared-final")require(saved.has_value(),"late PREPARED holds quit but preserves already committed resume");
  else require(!saved,"no fake checkpoint for held/idle/completion");
   if(name=="completion"){
    const auto state=storedState(db,"bookings",Run);
    std::cout<<"RAW_GATE_TERMINAL "<<terminalStatus.toStdString()<<" AFTER_EXPLICIT_RETRY "<<close.status().toStdString()<<" STORED "<<state.value_or("ABSENT").toStdString()<<'\n';
    require(state!=QString("SOURCE_FINISHED"),"old nonresumed footer without RAW identity never commits SOURCE");
    require(terminalStatus.contains("RAW lineage"),"first terminal is exact RAW Held, not an older checkpoint fallback");
   }
  if(name.startsWith("completion-queued")){
   // history() deliberately projects duplicate LOCAL_INTENT rows away once
   // a physical run represents them. Inspect storage, not that UI projection.
   const auto physical=storedState(db,"bookings",Run),local=storedState(db,"bookings",Booking),dispatch=storedState(db,"dispatch_requests",Booking);
   require(expectedReady?physical&&*physical=="SOURCE_FINISHED":!physical,"atomic physical SOURCE outcome");
   require(local&&*local==(expectedReady?"SOURCE_FINISHED":"DISPATCHED"),"local intent committed or exactly preserved");
   require(name=="completion-queued-missing"?!dispatch:dispatch&&*dispatch==(expectedReady?"SOURCE_FINISHED":"STARTED"),"dispatch transaction shares physical/local outcome");
  }
 }
 require(preserved(db)==before,"mode/preference/unknown metadata preserved incl rollback sideeffect");
 if(name=="not-conserved"||name=="excess-offset")require(inventories==4&&!expectedReady,"conservation polling stops at4, no infinite loop");
 if(expectedReady){NativeBridgeTestAccess::receive(bridge,{{"type","application_close_frozen"},{"request_id",closeId},{"accepted",false}});require(close.readyToQuit(),"late weak failure cannot erase Ready");}
}
}
namespace {
void maintenanceDetach(){
 {
  QTemporaryDir temp;QWidget window;NativeBridge bridge;QObject lifetime;int commands=0,drains=0;std::function<void()> finish;
  require(bridge.attachNativeRuntime(&lifetime,[&](const auto&){++commands;return true;}),"own native maintenance sink");
  const auto db=temp.filePath("must-not-create.sqlite");ApplicationMiningClose close(&window,QCoreApplication::instance(),&bridge,db,Label);
  close.onDrainBeforeQuit=[&](auto done){++drains;finish=std::move(done);};
  bool foreignAccepted=true;std::thread foreign([&]{foreignAccepted=close.requestMaintenanceDetach();});foreign.join();
  require(!foreignAccepted&&drains==0&&commands==0,"foreign thread cannot mutate maintenance state or start drain");
  require(close.requestMaintenanceDetach()&&drains==1&&commands==0&&!close.readyToQuit(),"maintenance admits observer drain, never normal CloseReady or physical command");
  require(close.requestMaintenanceDetach()&&drains==1,"duplicate maintenance request cannot duplicate drain or STOP");
  QCloseEvent blocked;QCoreApplication::sendEvent(&window,&blocked);close.requestClose();
  require(!blocked.isAccepted()&&commands==0&&!close.readyToQuit(),"normal Quit/Close cannot interleave STOP into developer observer drain");
  const auto done=finish;done();done();closePhaseLoop(5);
  require(!close.readyToQuit()&&close.status().contains("not physical CloseReady")&&commands==0,"duplicate drain completion permits only maintenance quit, no physical gate claim");
  require(!QFileInfo::exists(db),"maintenance never opens/creates/settles booking SQLite or a checkpoint");
 }
 {
  QTemporaryDir temp;QWidget window;NativeBridge bridge;QObject lifetime;int commands=0,drains=0;
  require(bridge.attachNativeRuntime(&lifetime,[&](const auto&){++commands;return true;}),"own normal-close refusal sink");
  ApplicationMiningClose close(&window,QCoreApplication::instance(),&bridge,temp.filePath("must-not-create.sqlite"),Label);
  require(!close.requestMaintenanceDetach()&&commands==0,"missing observer hook refuses maintenance without STOP");
  close.onDrainBeforeQuit=[&](auto){++drains;};close.requestClose();wait([&]{return commands>0;});const auto normalCommands=commands;
  require(normalCommands>0&&!close.requestMaintenanceDetach()&&drains==0&&commands==normalCommands,"maintenance cannot bypass/interfere with a normal close already in flight");
 }
 {
  QTemporaryDir temp;QWidget window;NativeBridge bridge;QObject lifetime;int commands=0;
  require(bridge.attachNativeRuntime(&lifetime,[&](const auto&){++commands;return true;}),"own failed maintenance drain sink");
  ApplicationMiningClose close(&window,QCoreApplication::instance(),&bridge,temp.filePath("must-not-create.sqlite"),Label);
  close.onDrainBeforeQuit=[](auto){throw std::runtime_error("PUBLIC_TEST_DRAIN_FAILURE");};
  require(!close.requestMaintenanceDetach()&&!close.readyToQuit()&&commands==0&&close.status().contains("Maintenance detach held"),"failed maintenance drain cannot fall through to normal STOP or Ready");
 }
}
void nativeCloseInventoryPhases(){
 for(const QString rawPhase:{QString("lineage"),QString("source_read")}){
 for(const bool ownerLoss:{false,true}){
  QTemporaryDir temp;require(temp.isValid(),"own phase-close temporary directory");const auto db=temp.filePath("booking.sqlite");{pgn::booking::BookingStore store(db);require(store.valid(),"own native store, no userDB");}
  QList<QJsonObject> commands;Window window;NativeBridge bridge;auto lifetime=std::make_unique<QObject>();QObject foreign;
  require(bridge.attachNativeRuntime(lifetime.get(),[&](const auto& p){commands.append(p);return true;}),"native phase owner, no QProcess/SSH");
  ApplicationMiningClose close(&window,QCoreApplication::instance(),&bridge,db,Label);close.requestClose();wait([&]{return !commands.isEmpty();});
  const auto request=commands.last().value("request_id").toString();
  require(bridge.acceptNativeRuntimePacket(lifetime.get(),{{"type","application_close_frozen"},{"request_id",request},{"accepted",true}}),"actual local freeze reply, not physical stop");
  wait([&]{return commands.last().value("action")=="application_close_inventory";});const auto inventory=commands.last().value("inventory_id").toString();
  auto* budget=close.findChild<QTimer*>("application-close-deadline");require(budget&&budget->isActive(),"actual close UI budget timer found");
  const auto packet=[&](int n,const QString& phase,bool active){return QJsonObject{{"type","application_inventory_phase"},{"request_id",request},{"inventory_id",inventory},{"purpose","close"},{"phase",phase},{"phase_id",closePhaseUuid(n)},{"active",active}};};
  const auto lineage=packet(1,rawPhase,true);budget->setInterval(25);
  NativeBridgeTestAccess::receive(bridge,lineage);require(budget->isActive(),"Node packet cannot suspend close budget");
  require(!bridge.acceptNativeRuntimePacket(&foreign,lineage),"foreign lifetime cannot suspend deadline");
  require(bridge.acceptNativeRuntimePacket(lifetime.get(),lineage)&&!budget->isActive(),"native admitted lineage suspends only global UI timer");
  closePhaseLoop(60);require(!close.readyToQuit()&&!close.status().contains("deadline expired")&&!budget->isActive(),"longer RAW phase stays pending, never Ready");
  require(!bridge.acceptNativeRuntimePacket(lifetime.get(),lineage),"lineage replay does not create another lease");
  if(ownerLoss){
   lifetime.reset();require(close.status().startsWith("Close held:")&&!budget->isActive()&&!close.readyToQuit(),"ownerloss terminal error holds suspended close without fake phase end");
  }else{
   require(!bridge.acceptNativeRuntimePacket(lifetime.get(),packet(2,rawPhase,false)),"wrong phase end cannot resume timer");
   require(bridge.acceptNativeRuntimePacket(lifetime.get(),packet(1,rawPhase,false))&&budget->isActive()&&budget->interval()<=25,"paired end restores only remaining UI budget");
   const auto snapshot=packet(3,"snapshot",true);require(bridge.acceptNativeRuntimePacket(lifetime.get(),snapshot)&&budget->interval()==45000,"new actual acquisition phase resumes45s, not physical freshness");
   budget->setInterval(25);require(!bridge.acceptNativeRuntimePacket(lifetime.get(),snapshot)&&budget->interval()==25,"duplicate profile/snapshot cannot renew timer");
   closePhaseLoop(60);require(close.status().contains("deadline expired")&&!close.readyToQuit()&&!budget->isActive(),"real short timer enforces Held after legitimate resume");
   require(!bridge.acceptNativeRuntimePacket(lifetime.get(),packet(4,"profile",true)),"Held retires phase lease, late packet cannot resurrect timeout");
  }
  require(window.closeEvents==0,"new hidden test window never closed before Ready");
 }
 }
}
}
int main(int argc,char** argv){
 if(argc==3&&QString::fromLocal8Bit(argv[1])=="--self-mining-close"){QCoreApplication app(argc,argv);return self(QString::fromLocal8Bit(argv[2]));}
 const bool finiteOnly=argc==3&&QString::fromLocal8Bit(argv[2])=="--finite-prestop";
 const bool maintenanceOnly=argc==3&&QString::fromLocal8Bit(argv[2])=="--maintenance-only";
 if(argc!=2&&!finiteOnly&&!maintenanceOnly)return 2;const auto plugin=QString::fromLocal8Bit(argv[1]);if(!QFileInfo(plugin).isFile())return 3;QPluginLoader preflight(plugin);if(!preflight.load()){std::cerr<<preflight.errorString().toStdString()<<'\n';return 4;}
 qputenv("QT_QPA_PLATFORM","offscreen");qputenv("QT_QPA_PLATFORM_PLUGIN_PATH",QFileInfo(plugin).absolutePath().toUtf8());QApplication app(argc,argv);if(QGuiApplication::platformName()!="offscreen")return 5;
 try{
  if(maintenanceOnly){maintenanceDetach();std::cout<<"PASS maintenance checks="<<checks<<" failures=0; own offscreen/native sink, no actual UI/SSH/GPU/SQL\n";return 0;}
  if(finiteOnly){
   for(const auto& name:QStringList{"finite-partial-clean","finite-partial-malformed","finite-partial-owned","finite-partial-changed","finite-partial-contradictory-unknown","finite-partial-wrong-device"})scenario(name,name=="finite-partial-clean");
   std::cout<<"PASS finite-prestop checks="<<checks<<" failures=0; partial finite proof before first STOP only, no historical artifact completion\n";return 0;
  }
  scenario("source-only-idle",true);scenario("unmanaged-spoof",false);
  nativeCloseInventoryPhases();
  for(const auto& name:QStringList{"driver-idle-nodriver","driver-partial","driver-active-nodriver","driver-idle-unknown"})scenario(name,name=="driver-idle-nodriver"||name=="driver-partial");
  for(const auto& name:QStringList{"partial","freeze-refused","unknown","malformed","bad-proof","commit-fail","finite-active","run-switch","not-conserved","excess-offset","idle","completion","missing-dispatch","prepared","prepared-cold","prepared-final","finite-unknown-retry"})scenario(name,name=="partial"||name=="idle");
  for(const auto& name:QStringList{"completion-queued","completion-queued-commit-fail","completion-queued-rebound","completion-queued-missing","completion-queued-poison","completion-queued-lineage-missing","completion-queued-lineage-held-a73"})scenario(name,name=="completion-queued");
  scenario("idle",true,"delayed");scenario("idle",false,"throw");
  std::cout<<"PASS checks="<<checks<<" failures=0; OFFSCREEN new hidden windows, own QProcess SELF + same temporary SQLite; no user UI/GPU/SSH/miner\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL checks="<<checks<<" "<<e.what()<<'\n';return 1;}
}

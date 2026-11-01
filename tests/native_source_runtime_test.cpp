#include "mining/native_source_runtime.h"
#include "mining/source_session.h"
#include "mining/source_age.h"
#include "booking/booking_store.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <sqlite3.h>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace pgn::mining;
namespace {
int checks{};
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void check(bool ok,const char* why){++checks;need(ok,why);}
template<class F>void rejected(F f,const char* why){bool threw=false;try{f();}catch(const std::exception&){threw=true;}check(threw,why);}
const QString Run(32,'a'),Other(32,'b'),Device="GPU-11111111-2222-3333-4444-555555555555";
QString uuid(int n){return "20000000-0000-4000-8000-"+QString::number(n,16).rightJustified(12,'0');}
qint64 monotonicMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
const QByteArray& corpus(){static const QByteArray data=[]{QByteArray out="[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=2 original_start=100000 length=10000 proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n";for(int i=0;i<1200;++i)out+="[phase0-gpu] GAP merit=1.9000 gap=6 lower="+QByteArray::number(100000+8*i)+" upper="+QByteArray::number(100006+8*i)+" verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown unix_ts=1791982800 time_basis=source_batch_publication\n";out+="[pgn-counters] time_ns=9007199254740993 positions=10000 gaps=1200 tests=9007199254740993 batches=19 processed_gaps=1200\n[phase0-gpu] gaps reported=1200 verification_failures=0 records_new=0\n[phase0-gpu] --- end ---\n";return out;}();
 if(!qEnvironmentVariableIsSet("PGN_SOURCE_FIXTURE_LONG"))return data;
 // About64MiB of real conserved bounded rows; no additional GAP/counter
 // credit. Large fixture mode only changes TESTONLY journal/SELF framing.
 static const QByteArray large=[&]{const auto first=data.indexOf('\n')+1;const auto row=QByteArray("TESTONLY padding ")+QByteArray(15982,'x')+'\n';return data.left(first)+row.repeated(4096)+data.mid(first);}();return large;
}
QJsonObject packet(const QString& run,qint64 offset,const QByteArray& bytes,qint64 pending){
    QJsonObject value={{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},{"operator_stop",true},{"pending_bytes",pending},{"offset",offset},{"next_offset",offset+bytes.size()},{"chunk",QString::fromLatin1(bytes.toBase64())},
        {"observed_utc","2026-10-14T18:00:00.000000001Z"},{"source_counter_age_ms",qint64(9876543210LL)},{"fixture_stdin_eof",true},
        {"binding",QJsonObject{{"schema","PUBLIC-TESTONLY"},{"device",Device},{"sample","unchanged"}}},
        {"manifest",QJsonObject{{"schema","pgn-native-process-1"},{"run",run},{"uuid",Device},{"pid",qint64(1234)},{"birth","2026-10-14T17:00:00.1234567Z"},{"start","100000"},{"end","110000"},{"stdout","C:/PUBLIC-TESTONLY/"+run+".stdout"}}},
        {"last_exit",QJsonObject{{"run",run},{"exit_code",0}}}};
    const auto booking=qEnvironmentVariable("PGN_SOURCE_FIXTURE_BOOKING");if(!booking.isEmpty()){auto m=value["manifest"].toObject();m["queued_intent"]=booking;value["manifest"]=m;}
    if(pending==0&&offset+bytes.size()==corpus().size())value["source_completion_observation"]=QJsonObject{{"schema","pgn-source-range-completion-1"},{"run",run},{"queued_intent",booking},{"start","100000"},{"end","110000"},{"source","C:/PUBLIC-TESTONLY/"+run+".stdout"},{"source_positions","10000"},{"source_session_gaps","1200"},{"source_tests","9007199254740993"},{"source_exit_code",0},{"source_range_complete",true},{"independent_coverage",false},{"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"},{"source_tests_complete",true},{"source_position_evidence","final_counter"}};
    if(qEnvironmentVariable("PGN_SOURCE_FIXTURE_MODE")=="telemetry-emitted")
        value["telemetry_lines"]=QJsonArray{"[pgn-counters] time_ns=9007199254740993 positions=10000 gaps=1200 tests=9007199254740993 batches=19 processed_gaps=1200 emitted_gaps=1200"};
    return value;
}
void output(const QJsonObject& value,bool stream){const auto bytes=QJsonDocument(value).toJson(stream?QJsonDocument::Compact:QJsonDocument::Indented)+(stream?"\n":"");const qsizetype step=qEnvironmentVariableIsSet("PGN_SOURCE_FIXTURE_LONG")?65536:113;for(qsizetype at=0;at<bytes.size();at+=step){const auto n=std::min<qsizetype>(step,bytes.size()-at);need(std::fwrite(bytes.constData()+at,1,size_t(n),stdout)==size_t(n)&&std::fflush(stdout)==0,"SELF fake stdout");}}
int fake(const QStringList& args){
    const auto encoded=args.last().section(' ',-1).toLatin1(),wide=QByteArray::fromBase64(encoded);const auto script=QString::fromUtf16(reinterpret_cast<const char16_t*>(wide.constData()),wide.size()/2);
    const auto match=QRegularExpression("-Action (snapshot|stream) -Offset ([0-9]+) -ExpectedRun '([0-9a-f]*)'").match(script);need(match.hasMatch(),"SELF exact encoded request");
    const auto actions=qEnvironmentVariable("PGN_SOURCE_FIXTURE_ACTIONS");if(!actions.isEmpty()){QFile log(actions);need(log.open(QIODevice::WriteOnly|QIODevice::Append)&&log.write(match.captured(1).toUtf8()+'\n')>0&&log.flush(),"SELF readonly action receipt");}
    need(std::fgetc(stdin)==EOF,"SELF null stdin");
    const auto pidFile=qEnvironmentVariable("PGN_SOURCE_FIXTURE_PID");if(!pidFile.isEmpty()){QFile f(pidFile);need(f.open(QIODevice::WriteOnly|QIODevice::Truncate)&&f.write(QByteArray::number(QCoreApplication::applicationPid()))>0&&f.flush(),"SELF own PID receipt");}
    const auto mode=qEnvironmentVariable("PGN_SOURCE_FIXTURE_MODE");const auto stream=match.captured(1)=="stream";bool ok=false;const auto requested=match.captured(2).toLongLong(&ok);need(ok,"SELF request exact offset");auto selected=match.captured(3);if(selected.isEmpty())selected=Run;const auto all=corpus();
    if(mode=="malformed"){std::fputs("{bad JSON}\n",stdout);std::fflush(stdout);return 0;}
    if(mode=="progress-large-offset"){output(packet(selected,9007199254740993LL,{},0),true);return 0;}
    if(mode=="progress-unknown-pending"){auto p=packet(selected,requested,all.mid(requested,7000),0);p.remove("pending_bytes");output(p,true);return 0;}
    if(!stream){
        if(mode=="worker-starvation"){
            // TESTONLY endpoint waits for a private fixture gate. It performs
            // no storage/control operation and exits normally with one snapshot.
            const auto gate=qEnvironmentVariable("PGN_SOURCE_FIXTURE_GATE");need(!gate.isEmpty(),"SELF private snapshot gate required");QElapsedTimer wait;wait.start();
            while(!QFileInfo::exists(gate)&&wait.elapsed()<6000)QThread::msleep(2);
            need(QFileInfo::exists(gate),"SELF bounded snapshot gate deadline");output(packet(selected,requested,{},0),false);return 0;
        }
        if(mode=="cold"){auto p=packet(selected,0,{},0);p["manifest"]=QJsonValue::Null;output(p,false);return 0;}
        const auto tail=mode=="hold"?QByteArray("inventory-not-consumed\n"):all.mid(requested,mode=="completed-pages"?7000:65536);output(packet(selected,requested,tail,mode=="completed-pages"?all.size()-requested-tail.size():0),false);return mode=="exit-fail"?7:0;
    }
    if(mode=="lease"||mode=="lease-failure"||mode=="lease-off"){
        need(script.endsWith(" -StreamCount 150"),"SELF finite remote lease argv required");
        QFile log(actions);need(log.open(QIODevice::ReadOnly),"SELF lease receipt");const auto lease=log.readAll().count('\n');
        auto offset=requested;
        for(int frame=0;frame<SshSnapshotTransport::StreamFrameLease;++frame){
            const auto bytes=all.mid(offset,7000);auto p=packet(selected,offset,bytes,all.size()-offset-bytes.size());
            p["owned"]=mode!="lease-off";p["state"]=mode=="lease-off"?"OFF":"RUNNING";p["operator_stop"]=false;
            p["last_exit"]=QJsonValue::Null;p.remove("source_completion_observation");p["fixture_read_lease"]=lease;
            output(p,true);offset+=bytes.size();
            if(lease==2){for(int n=0;n<60;++n)QThread::msleep(1000);return 0;}
        }
        return mode=="lease-failure"?7:0;
    }
    if(mode=="wrong-run")selected=Other;
    qint64 offset=mode=="replay"?requested+5:requested;
    if(mode=="hold"||mode=="replay"||mode=="wrong-run"){
        const auto bytes=all.mid(offset,7000);output(packet(selected,offset,bytes,all.size()-offset-bytes.size()),true);
        if(mode=="hold"){for(int i=0;i<60;++i)QThread::msleep(1000);}return 0;
    }
    if(offset==all.size())output(packet(selected,offset,{},0),true);
    // Keep complete-line enrollment <=64KiB even with a <16KiB carry row.
    // This fixture must pay the CURRENT verifier contract, not widen it.
    const auto limit=mode=="completed-pages"?QRegularExpression(" -StreamCount ([0-9]+)$").match(script).captured(1).toInt():std::numeric_limits<int>::max();int sent=0;
    while(offset<all.size()&&sent++<limit){const auto bytes=all.mid(offset,qEnvironmentVariableIsSet("PGN_SOURCE_FIXTURE_LONG")?65536:7000);output(packet(selected,offset,bytes,all.size()-offset-bytes.size()),true);offset+=bytes.size();}
    return 0;
}
QByteArray contents(const QString& file){QFile f(file);need(f.open(QIODevice::ReadOnly),"own fixture raw read");return f.readAll();}
std::optional<qint64> scalar(const QString& file,const char* sql){sqlite3* db{};if(sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK){sqlite3_close(db);return {};}sqlite3_stmt* st{};std::optional<qint64> value;if(sqlite3_prepare_v2(db,sql,-1,&st,nullptr)==SQLITE_OK&&sqlite3_step(st)==SQLITE_ROW)value=sqlite3_column_int64(st,0);sqlite3_finalize(st);sqlite3_close(db);return value;}
void mutate(const QString& path,const char* sql,const QStringList& args){sqlite3* db{};need(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"own booking fixture open");sqlite3_stmt* q{};need(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK,"own booking fixture SQL");for(int i=0;i<args.size();++i){const auto bytes=args[i].toUtf8();sqlite3_bind_text(q,i+1,bytes.constData(),bytes.size(),SQLITE_TRANSIENT);}const auto code=sqlite3_step(q);sqlite3_finalize(q);sqlite3_close(db);need(code==SQLITE_DONE,"own booking public DTO setup");}
bool until(const std::function<bool()>& done,int timeout=6000){if(done())return true;QEventLoop loop;QTimer poll,deadline;poll.setInterval(5);deadline.setSingleShot(true);QObject::connect(&poll,&QTimer::timeout,&loop,[&]{if(done())loop.quit();});QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);poll.start();deadline.start(timeout);loop.exec();return done();}
struct Fixture {
    QTemporaryDir temp;QString db,key;NativeSourceRuntimeConfig config;QList<NativeSourcePublication> pubs;QList<QJsonObject> indices;QList<NativeSourceAttempt> done;QList<NativeSourceInventory> inventories;QList<NativeSourceLineage> lineage;QList<NativeSourceProgress> progress;QList<NativeSourceTrace> traces;std::function<void(const NativeSourceProgress&)> onProgress;std::function<void(const NativeSourceTrace&)> onTrace;std::function<void(const NativeSourceInventory&)> onInventory;std::function<void(const NativeSourceAttempt&)> onDone;std::function<void(const QJsonObject&)> onIndex;bool stopped{};std::unique_ptr<NativeSourceRuntime> runtime;
    Fixture(bool diagnostics=false,int snapshotDeadline=3000,bool graphSink=true){need(temp.isValid(),"own temporary directory");db=temp.filePath("history.sqlite");key=temp.filePath("dummy-public-TESTONLY.key");QFile f(key);need(f.open(QIODevice::WriteOnly)&&f.write("PUBLIC-TESTONLY\n")>0,"own fixture public endpoint placeholder");f.close();config={{QCoreApplication::applicationFilePath(),key,"PUBLIC-TESTONLY.invalid","fixture",22},Device,db,temp.path(),1000,3000,snapshotDeadline};open(diagnostics,graphSink);
    }
    void open(bool diagnostics,bool graphSink=true){stopped=false;NativeSourceRuntimeCallbacks callbacks{[this](const auto& p){pubs.append(p);},[this](const auto& d){done.append(d);if(onDone)onDone(d);},[this](const auto& i){inventories.append(i);if(onInventory)onInventory(i);},[this]{stopped=true;},[this](const auto& l){lineage.append(l);}};
        if(diagnostics)callbacks.progress=[this](const auto& p){need(QThread::currentThread()==QCoreApplication::instance()->thread(),"progress creating-thread callback");progress.append(p);if(onProgress)onProgress(p);};
        if(diagnostics)callbacks.trace=[this](const auto& p){need(QThread::currentThread()==QCoreApplication::instance()->thread(),"trace creating-thread callback");traces.append(p);if(onTrace)onTrace(p);};
        if(graphSink)callbacks.nativeIndex=[this](const auto& p){need(QThread::currentThread()==QCoreApplication::instance()->thread(),"index creating-thread callback");indices.append(p);if(onIndex)onIndex(p);};
        runtime=std::make_unique<NativeSourceRuntime>(config,std::move(callbacks));
    }
    void stop(){runtime->stop();need(until([&]{return stopped;}),"own runtime clean stopped");runtime.reset();}
    ~Fixture(){if(runtime){runtime->stop();until([&]{return stopped;});runtime.reset();}}
};
void privateGate(const QString& path){QFile file(path);need(file.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&file.write("TESTONLY release\n")>0&&file.flush(),"private gate created exactly once");}
struct StorageLock {
    sqlite3* db{};
    explicit StorageLock(const QString& file){need(sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"own storage contention connection");if(sqlite3_exec(db,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)!=SQLITE_OK){sqlite3_close(db);db=nullptr;need(false,"real private SQLite writer lock");}}
    void release(){need(db&&sqlite3_exec(db,"COMMIT",nullptr,nullptr,nullptr)==SQLITE_OK,"private storage lock committed without data mutation");sqlite3_close(db);db=nullptr;}
    ~StorageLock(){if(db){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);sqlite3_close(db);}}
};
std::optional<NativeSourceTrace> event(const Fixture& f,NativeSourceTraceStage stage,const QString& id,std::optional<SshObservationTraceStage> transport={}){
    for(const auto& p:f.traces)if(p.stage==stage&&p.requestId==id&&(!transport||(p.transport&&p.transport->stage==*transport)))return p;return {};
}
void boundedTrace(const Fixture& f,const QString& inventoryId){
    int count=0;qint64 previous=-1;
    for(const auto& p:f.traces){const auto b=QJsonDocument(p.json()).toJson(QJsonDocument::Compact);check(b.size()<4096&&!b.contains(f.key.toUtf8())&&!b.contains(f.config.endpoint.host.toUtf8())&&!p.json().contains("snapshot")&&!p.json().contains("chunk"),"trace fixed metadata excludes endpoint/key/RAW/snapshot");
        if(p.requestId!=inventoryId)continue;++count;check(p.run==Run&&p.epoch==1&&p.monotonicMs>=previous,"trace retains original inventory identity and monotonic production order");previous=p.monotonicMs;
        if(p.transport)check(p.transport->action==SshObservationAction::Snapshot&&p.transport->expectedRun==Run&&p.transport->offset==0&&p.transport->elapsedMs>=0,"transport trace exact request, not a refreshed/relabelled observation");
    }
    check(count>=7&&count<=9,"fixed bounded inventory lifecycle, never per-frame/chunk logging");
}
void workerEventStarvation(){
    const NativeSourceTrace exact{NativeSourceTraceStage::IndexDone,uuid(99),Run,{},1,1,monotonicMs(),{},9007199254740993LL,9007199254740995LL};check(exact.json().value("offset").toInteger()==9007199254740993LL&&exact.json().value("next_offset").toInteger()==9007199254740995LL,"trace preserves exact offsets beyond binary64 without defaults");
    qputenv("PGN_SOURCE_FIXTURE_MODE","worker-starvation");
    // Control: same endpoint/frame/request without storage contention is Known.
    {Fixture f(true,1000);const auto gate=f.temp.filePath("snapshot.release");qputenv("PGN_SOURCE_FIXTURE_GATE",gate.toUtf8());privateGate(gate);check(f.runtime->inspectInventory(uuid(100),Run),"control inventory admitted");check(until([&]{return event(f,NativeSourceTraceStage::InventoryDelivered,uuid(100)).has_value();}),"control lifecycle delivered");check(f.inventories.size()==1&&f.inventories[0].transport.status==SshObservationStatus::Complete&&f.inventories[0].transport.exitCode==0&&f.inventories[0].frame.has_value(),"unblocked exact SELF snapshot reaches known clean EOF0");check(!QFileInfo::exists(f.db)&&f.pubs.isEmpty()&&f.done.isEmpty(),"readonly inventory does not initialize SOURCE storage");boundedTrace(f,uuid(100));f.stop();}
#ifdef Q_OS_WIN
    // Active transport FIRST; only then block the SAME worker in real SQLite.
    // The owner independently observes the exact own child's physical exit0.
    {Fixture f(true,1000);{SourceHistory seed(f.db);}StorageLock lock(f.db);const auto gate=f.temp.filePath("snapshot.release"),actions=f.temp.filePath("actions.txt");qputenv("PGN_SOURCE_FIXTURE_GATE",gate.toUtf8());qputenv("PGN_SOURCE_FIXTURE_ACTIONS",actions.toUtf8());
        check(f.runtime->inspectInventory(uuid(101),Run)&&until([&]{return event(f,NativeSourceTraceStage::InventoryTransport,uuid(101),SshObservationTraceStage::ProcessStarted).has_value();}),"snapshot child active before blocking SOURCE initialization");
        const auto started=*event(f,NativeSourceTraceStage::InventoryTransport,uuid(101),SshObservationTraceStage::ProcessStarted),request=*event(f,NativeSourceTraceStage::InventoryTransport,uuid(101),SshObservationTraceStage::RequestStarted);check(started.transport->processId&&*started.transport->processId>0,"actual exact OWN client PID observed, not inferred");
        const auto handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(*started.transport->processId));check(handle!=nullptr,"readonly handle to exact SELF child acquired while live");struct OwnHandle{HANDLE value;~OwnHandle(){CloseHandle(value);}} held{handle};
        check(f.runtime->startSource(uuid(102),Run)&&until([&]{return event(f,NativeSourceTraceStage::InitializeBegin,uuid(102)).has_value();}),"real SOURCE constructor entered storage while inventory active");privateGate(gate);
        check(until([&]{return WaitForSingleObject(handle,0)==WAIT_OBJECT_0;},2000),"SELF snapshot physically exits during blocked worker");const auto physicalExit=monotonicMs();DWORD code=STILL_ACTIVE;check(GetExitCodeProcess(handle,&code)&&code==0,"independent physical SELF exit0 retained before worker callbacks");
        check(physicalExit-request.monotonicMs<1000,"physical endpoint completes before unchanged FIXTURE snapshot budget");check(f.inventories.isEmpty()&&!event(f,NativeSourceTraceStage::InitializeDone,uuid(102)),"owner remains responsive while worker storage suppresses completion delivery");
        check(until([&]{return event(f,NativeSourceTraceStage::InventoryTransport,uuid(101),SshObservationTraceStage::Settled).has_value();}),"isolated inventory transport settles while SOURCE storage is still blocked");const auto settled=*event(f,NativeSourceTraceStage::InventoryTransport,uuid(101),SshObservationTraceStage::Settled);check(settled.transport->status==SshObservationStatus::Complete&&settled.transport->exitCode==0&&settled.transport->elapsedMs<1000&&!event(f,NativeSourceTraceStage::InitializeDone,uuid(102)),"own EOF0 observed by acquisition actor before storage release/deadline");check(!f.runtime->inspectInventory(uuid(105),Run),"one inventory remains in flight through queued conservation, no hidden acquisition queue");
        check(f.runtime->cancelSource(),"cancel SOURCE before storage release prevents any future stream launch");check(until([&]{return monotonicMs()-request.monotonicMs>=1400;},2500),"bounded controlled storage hold exceeds fixture deadline");const auto released=monotonicMs();lock.release();
        check(until([&]{return event(f,NativeSourceTraceStage::InventoryDelivered,uuid(101)).has_value()&&!f.done.isEmpty();}),"normal correlated callback settles after storage releases");
        const auto init=*event(f,NativeSourceTraceStage::InitializeBegin,uuid(102)),done=*event(f,NativeSourceTraceStage::InitializeDone,uuid(102)),delivered=*event(f,NativeSourceTraceStage::InventoryDelivered,uuid(101));
        check(done.monotonicMs>=released&&done.monotonicMs-init.monotonicMs>=1000&&delivered.monotonicMs>=released,"measured synchronous storage span and delayed event delivery overlap exact child lifetime");
        const auto& result=f.inventories.first();const auto deadline=event(f,NativeSourceTraceStage::InventoryTransport,uuid(101),SshObservationTraceStage::SnapshotDeadline);
        check(result.transport.status==SshObservationStatus::Complete&&result.transport.exitCode==0&&!deadline&&!result.frame&&result.error=="SOURCE epoch/cancellation changed during inventory acquisition"&&result.sourceEpoch==0,"transport completes independently but source epoch switch/cancel remains explicit Held, never relabelled Ready");
        QJsonObject receipt{{"schema","pgn-closed-worker-starvation-discriminator-1"},{"scope","TESTONLY_ISOLATED_TRANSPORT_NOT_GUI3452_CAUSE_OR_READY"},{"request_monotonic_ms",request.monotonicMs},{"physical_self_exit0_monotonic_ms",physicalExit},{"transport_settled_monotonic_ms",settled.monotonicMs},{"storage_begin_monotonic_ms",init.monotonicMs},{"storage_release_monotonic_ms",released},{"storage_done_monotonic_ms",done.monotonicMs},{"owner_delivery_monotonic_ms",delivered.monotonicMs},{"snapshot_deadline_observed",bool(deadline)},{"transport_known",result.transport.status==SshObservationStatus::Complete},{"frame_admitted",bool(result.frame)},{"fixture_snapshot_budget_ms",1000}};
        std::cout<<QJsonDocument(receipt).toJson(QJsonDocument::Compact).constData()<<'\n'<<std::flush;
        check(f.done.last().status==NativeSourceAttemptStatus::Cancelled&&f.pubs.isEmpty()&&!QFileInfo::exists(f.temp.filePath(Run+".stdout"))&&scalar(f.db,"SELECT COUNT(*) FROM cursors")==0&&contents(actions)=="snapshot\n","storage/observer test launches no stream/control and mutates no SOURCE prefix");boundedTrace(f,uuid(101));check(f.runtime->inspectInventory(uuid(105),Run)&&until([&]{return f.inventories.size()==2;}),"fresh explicit inventory permitted after previous Held settles");check(f.inventories.last().requestId==uuid(105)&&f.inventories.last().epoch==2&&f.inventories.last().sourceEpoch==1&&f.inventories.last().frame&&f.inventories.last().transport.status==SshObservationStatus::Complete&&contents(actions)=="snapshot\nsnapshot\n","fresh request has new physical acquisition, not stale packet relabel or implicit retry");f.stop();qunsetenv("PGN_SOURCE_FIXTURE_ACTIONS");
    }
#else
    check(false,"physical exit discriminator requires actual Win32 OWN child handle");
#endif
    // SOURCE storage already blocked at admission: transport still starts and
    // finishes now; only readonly conservation must wait on the storage actor.
    {Fixture f(true,1000);{SourceHistory seed(f.db);}StorageLock lock(f.db);const auto gate=f.temp.filePath("snapshot.release");qputenv("PGN_SOURCE_FIXTURE_GATE",gate.toUtf8());privateGate(gate);
        check(f.runtime->startSource(uuid(103),Run)&&until([&]{return event(f,NativeSourceTraceStage::InitializeBegin,uuid(103)).has_value();}),"counterexample blocks storage before inventory entry");check(f.runtime->cancelSource(),"cancel unlaunched SOURCE before inventory admission pins cancelled state");check(f.runtime->inspectInventory(uuid(104),Run),"readonly inventory independent of already blocked storage actor");const auto queued=monotonicMs();check(until([&]{return event(f,NativeSourceTraceStage::InventoryTransport,uuid(104),SshObservationTraceStage::Settled).has_value();}),"independent actor completes while source init remains held");const auto settled=*event(f,NativeSourceTraceStage::InventoryTransport,uuid(104),SshObservationTraceStage::Settled);check(settled.transport->status==SshObservationStatus::Complete&&settled.transport->elapsedMs<1000&&f.inventories.isEmpty(),"transport deadline and result are independent; conservation is NOT fabricated");check(until([&]{return monotonicMs()-queued>=1400;},2500),"storage remains held longer than snapshot fixture budget");const auto released=monotonicMs();lock.release();check(until([&]{return event(f,NativeSourceTraceStage::InventoryDelivered,uuid(104)).has_value()&&!f.done.isEmpty();}),"inventory delivery waits for real source init/conservation serialization");const auto request=*event(f,NativeSourceTraceStage::InventoryTransport,uuid(104),SshObservationTraceStage::RequestStarted);check(request.monotonicMs<released&&f.inventories.last().transport.status==SshObservationStatus::Complete&&f.inventories.last().frame.has_value()&&f.inventories.last().sourceEpoch==1&&f.inventories.last().conservation.value("ready")!=true&&!event(f,NativeSourceTraceStage::InventoryTransport,uuid(104),SshObservationTraceStage::SnapshotDeadline),"same epoch snapshot retained, no synthetic conservation/Ready or delayed deadline");check(f.inventories.last().frame->packet.value("observed_utc")=="2026-10-14T18:00:00.000000001Z"&&f.inventories.last().frame->packet.value("source_counter_age_ms").toInteger()==9876543210LL,"delayed delivery preserves original walltime and age exactly");boundedTrace(f,uuid(104));f.stop();}
    qunsetenv("PGN_SOURCE_FIXTURE_GATE");qunsetenv("PGN_SOURCE_FIXTURE_MODE");
}
void isolatedInventoryLifetime(){
    qputenv("PGN_SOURCE_FIXTURE_MODE","worker-starvation");
#ifdef Q_OS_WIN
    {Fixture f(true,1000);{SourceHistory seed(f.db);}StorageLock lock(f.db);const auto gate=f.temp.filePath("snapshot.release");qputenv("PGN_SOURCE_FIXTURE_GATE",gate.toUtf8());
        check(f.runtime->startSource(uuid(110),Run)&&until([&]{return event(f,NativeSourceTraceStage::InitializeBegin,uuid(110)).has_value();}),"shutdown fixture storage actually blocked");check(f.runtime->inspectInventory(uuid(111),Run)&&until([&]{return event(f,NativeSourceTraceStage::InventoryTransport,uuid(111),SshObservationTraceStage::ProcessStarted).has_value();}),"inventory client starts while storage blocked");const auto started=*event(f,NativeSourceTraceStage::InventoryTransport,uuid(111),SshObservationTraceStage::ProcessStarted);const auto handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(*started.transport->processId));check(handle!=nullptr,"exact own shutdown child readonly handle");struct Held{HANDLE value;~Held(){CloseHandle(value);}} held{handle};
        f.runtime->stop();check(!f.runtime->inspectInventory(uuid(112),Run)&&!f.runtime->startSource(uuid(113),Run),"shutdown rejects new admissions on both actors");check(until([&]{return WaitForSingleObject(handle,0)==WAIT_OBJECT_0;}),"inventory OWN client cancelled independently of blocked storage actor");check(!f.stopped&&f.pubs.isEmpty()&&!QFileInfo::exists(f.temp.filePath(Run+".stdout")),"stopped waits BOTH actors and cannot claim unfinished storage cleanup");lock.release();check(until([&]{return f.stopped;}),"both owned threads finish asynchronously after actual storage release");for(const auto& value:f.inventories)check(!value.frame&&!value.error.isEmpty(),"shutdown/late inventory cannot publish physical Ready");f.runtime.reset();}
    {Fixture f(true,1000);const auto gate=f.temp.filePath("snapshot.release");qputenv("PGN_SOURCE_FIXTURE_GATE",gate.toUtf8());check(f.runtime->inspectInventory(uuid(114),Run)&&until([&]{return event(f,NativeSourceTraceStage::InventoryTransport,uuid(114),SshObservationTraceStage::ProcessStarted).has_value();}),"owner-loss exact inventory child admitted");const auto started=*event(f,NativeSourceTraceStage::InventoryTransport,uuid(114),SshObservationTraceStage::ProcessStarted);const auto handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(*started.transport->processId));check(handle!=nullptr,"owner-loss keeps exact own process handle, no PID scan");struct Held{HANDLE value;~Held(){CloseHandle(value);}} held{handle};f.runtime.reset();check(until([&]{return WaitForSingleObject(handle,0)==WAIT_OBJECT_0;}),"owner destruction asynchronously cleans its inventory child");check(f.inventories.isEmpty()&&f.pubs.isEmpty()&&f.done.isEmpty()&&!QFileInfo::exists(f.db),"late callbacks do not target destroyed owner or initialize storage");}
#endif
    {Fixture f(true,1000);const auto gate=f.temp.filePath("snapshot.release");qputenv("PGN_SOURCE_FIXTURE_GATE",gate.toUtf8());check(f.runtime->inspectInventory(uuid(115),Run),"pre-entry inventory admitted");f.runtime->stop();check(until([&]{return f.stopped;}),"stop before actor entry settles both threads without event-loop crash");for(const auto& value:f.inventories)check(!value.frame&&!value.error.isEmpty(),"pre-entry/late shutdown never fabricates a snapshot");f.runtime.reset();}
    qunsetenv("PGN_SOURCE_FIXTURE_GATE");qunsetenv("PGN_SOURCE_FIXTURE_MODE");
}
void reentrantTraceDelivery(){
    qputenv("PGN_SOURCE_FIXTURE_MODE","worker-starvation");Fixture f(true,1000);const auto gate=f.temp.filePath("snapshot.release");qputenv("PGN_SOURCE_FIXTURE_GATE",gate.toUtf8());privateGate(gate);bool admittedSecond=false;
    f.onInventory=[&](const auto& value){if(value.requestId==uuid(116))admittedSecond=f.runtime->inspectInventory(uuid(117),Run);};
    check(f.runtime->inspectInventory(uuid(116),Run)&&until([&]{return event(f,NativeSourceTraceStage::InventoryDelivered,uuid(117)).has_value();}),"reentrant inventory consumer admits its explicit successor");
    check(admittedSecond&&f.inventories.size()==2&&f.inventories[0].requestId==uuid(116)&&f.inventories[0].epoch==1&&f.inventories[1].requestId==uuid(117)&&f.inventories[1].epoch==2,"two real acquisitions keep distinct operational correlation");
    const auto first=event(f,NativeSourceTraceStage::InventoryDelivered,uuid(116)),second=event(f,NativeSourceTraceStage::InventoryDelivered,uuid(117));check(first&&second&&first->epoch==1&&second->epoch==2&&first->monotonicMs<=second->monotonicMs,"old diagnostic terminal span survives successor admission without retargeting");
    for(const auto& id:{uuid(116),uuid(117)}){int count=0;for(const auto& value:f.traces)if(value.requestId==id)++count;check(count==8,"each explicit nonce has exactly its bounded lifecycle, no growing per-frame log");}
    for(const auto& value:f.inventories)check(value.transport.status==SshObservationStatus::Complete&&value.transport.exitCode==0&&value.frame&&value.sourceEpoch==0,"diagnostic filter removal changes no physical/proof guard");check(!QFileInfo::exists(f.db)&&f.pubs.isEmpty()&&f.done.isEmpty(),"reentrant diagnostics never attach storage or launch SOURCE/control");f.stop();qunsetenv("PGN_SOURCE_FIXTURE_GATE");qunsetenv("PGN_SOURCE_FIXTURE_MODE");
}
void progressTuple(const QList<NativeSourceProgress>& values,const QString& id,quint64 epoch,qint64 offset,qint64 next,std::optional<qint64> pending,const QList<NativeSourceProgressStage>& stages){
    check(values.size()==stages.size(),"bounded first-frame stage count, no per-chunk progress stream");qint64 previous=0;
    const QStringList names{"frame_received","source_admitted","index_begin","index_done","index_held"};
    for(qsizetype at=0;at<values.size();++at){const auto& p=values[at];check(p.stage==stages[at]&&p.requestId==id&&p.run==Run&&p.sourceEpoch==epoch,"progress original stage/request/run/sourceepoch order");check(p.offset==offset&&p.nextOffset==next&&p.pendingBytes==pending&&p.elapsedMs>=previous,"exact first-frame offsets/unknown and monotonic elapsed duration");previous=p.elapsedMs;const auto j=p.json();const auto b=QJsonDocument(j).toJson(QJsonDocument::Compact);check(b.size()<=4096&&j.value("schema")=="pgn-native-source-progress-1"&&j.value("stage")==names[int(p.stage)]&&j.value("source_epoch")==QString::number(epoch)&&j.value("offset")==QJsonValue(offset)&&j.value("next_offset")==QJsonValue(next)&&j.value("pending_bytes")== (pending?QJsonValue(*pending):QJsonValue(QJsonValue::Null))&&!j.contains("chunk")&&!j.contains("snapshot")&&!j.contains("observed_utc"),"bounded structured diagnostics, exact JSON integers/unknown, no RAW/new wallclock/coverage payload");}
}
const QList<NativeSourceProgressStage> ProgressComplete{NativeSourceProgressStage::FrameReceived,NativeSourceProgressStage::SourceAdmitted,NativeSourceProgressStage::IndexBegin,NativeSourceProgressStage::IndexDone};
void firstFrameProgress(){
    qputenv("PGN_SOURCE_FIXTURE_MODE","bulk");Fixture f(true);const auto actions=f.temp.filePath("actions.txt");qputenv("PGN_SOURCE_FIXTURE_ACTIONS",actions.toUtf8());check(f.runtime->startSource(uuid(81),Run),"first-frame diagnostic attempt admitted");
    // The real worker receives MANY frames while its owner/event queue is not
    // pumped. Rich publications coalesce; all first-frame stages must survive.
    bool complete=false;for(int n=0;n<1000;++n){if(scalar(f.db,"SELECT MAX(offset) FROM cursors")==corpus().size()){complete=true;break;}QThread::msleep(5);}check(complete,"actual SOURCE fully conserved while progress delivery is queued");QThread::msleep(30);
    check(until([&]{return !f.done.isEmpty()&&!f.indices.isEmpty();}),"SOURCE and dedicated graph snapshots settle independently");progressTuple(f.progress.mid(0,2),uuid(81),1,0,7000,corpus().size()-7000,{NativeSourceProgressStage::FrameReceived,NativeSourceProgressStage::SourceAdmitted});check(f.progress.size()==4,"one first SOURCE pair and one first graph audit pair");progressTuple(f.progress.mid(2),uuid(81),1,f.progress[2].offset,f.progress[2].nextOffset,f.progress[2].pendingBytes,{NativeSourceProgressStage::IndexBegin,NativeSourceProgressStage::IndexDone});check(f.indices.size()==1&&f.indices[0].value("snapshot_committed_offset").toInteger()==corpus().size(),"render requests coalesced to ONE latest coherent cursor while RAW all persisted");
    check(f.traces.size()==4,"optional lifecycle has initialization and first ACTUAL graph audit pair");qint64 priorTrace=-1;const QList<NativeSourceTraceStage> stages{NativeSourceTraceStage::InitializeBegin,NativeSourceTraceStage::InitializeDone,NativeSourceTraceStage::IndexBegin,NativeSourceTraceStage::IndexDone};for(qsizetype n=0;n<f.traces.size();++n){const auto& trace=f.traces[n];check(trace.stage==stages[n]&&trace.requestId==uuid(81)&&trace.run==Run&&trace.epoch==1&&trace.sourceEpoch==1&&trace.monotonicMs>=priorTrace,"source and asynchronous render stages retain original epoch/monotonic time");priorTrace=trace.monotonicMs;check(n<2?(!trace.offset&&!trace.nextOffset):(trace.offset==f.progress[2].offset&&trace.nextOffset==f.progress[2].nextOffset),"asynchronous graph audit names its ACTUAL coalesced request, never relabels first frame");}
    check(f.done[0].status==NativeSourceAttemptStatus::Complete&&f.pubs.size()<=2&&f.pubs.last().coalescedFrames>1,"progress instrumentation preserves rich coalescence and clean SOURCE completion");
    check(contents(f.temp.filePath(Run+".stdout"))==corpus()&&scalar(f.db,"SELECT COUNT(*) FROM gaps")==1200,"instrumentation cannot duplicate/skip GAPs or raw bytes");
    const auto first=f.progress;check(f.runtime->startSource(uuid(82),Run)&&until([&]{return f.done.size()==2&&f.indices.size()==2;}),"new explicit EOF attempt has independent graph callback");progressTuple(f.progress.mid(first.size()),uuid(82),2,corpus().size(),corpus().size(),0,ProgressComplete);
    check(f.traces.size()==6&&f.traces[4].stage==NativeSourceTraceStage::IndexBegin&&f.traces[5].stage==NativeSourceTraceStage::IndexDone&&f.traces[4].requestId==uuid(82)&&f.traces[4].epoch==2&&f.traces[4].offset==corpus().size(),"warm successor trace does not invent initialization or retarget old events");
    check(f.progress.mid(0,first.size()).first().requestId==uuid(81)&&scalar(f.db,"SELECT COUNT(*) FROM gaps")==1200,"old progress is not retargeted to successor and replay adds no GAP");
    check(contents(actions)=="stream\nstream\n","diagnostics sends only existing readonly stream actions, no extra read or GPU control");f.stop();qunsetenv("PGN_SOURCE_FIXTURE_ACTIONS");
    Fixture absent;check(absent.runtime->startSource(uuid(83),Run)&&until([&]{return !absent.done.isEmpty()&&!absent.indices.isEmpty();}),"optional progress callback absent retains ordinary SOURCE path");check(absent.progress.isEmpty()&&absent.traces.isEmpty()&&contents(absent.temp.filePath(Run+".stdout"))==corpus()&&absent.pubs.last().nativeIndex.isEmpty()&&absent.indices.last().value("complete")==true,"SOURCE frame has no fake/synchronous index; exact graph arrives independently");absent.stop();
    Fixture throwing(true);throwing.onProgress=[](const auto&){throw std::runtime_error("TESTONLY diagnostics sink error");};throwing.onTrace=[](const auto&){throw std::runtime_error("TESTONLY lifecycle sink error");};check(throwing.runtime->startSource(uuid(90),Run)&&until([&]{return !throwing.done.isEmpty()&&!throwing.indices.isEmpty();}),"throwing optional diagnostics sink settles normally");check(throwing.done.last().status==NativeSourceAttemptStatus::Complete&&throwing.progress.size()==4&&throwing.traces.size()==4&&contents(throwing.temp.filePath(Run+".stdout"))==corpus(),"diagnostic callback failure cannot alter SOURCE bytes/admission/index");throwing.stop();
}
void progressHeldAndCancellation(){
    qputenv("PGN_SOURCE_FIXTURE_MODE","bulk");
    {Fixture f(true);check(f.runtime->startSource(uuid(84),Run)&&f.runtime->cancelSource(),"cancel before worker entry remains SOURCE cancellation");check(until([&]{return !f.done.isEmpty();}),"pre-frame cancelled operation settles");check(f.progress.isEmpty()&&f.done.last().status==NativeSourceAttemptStatus::Cancelled,"no invented first frame/stages when cancelled before receipt");check(f.runtime->startSource(uuid(85),Run)&&until([&]{return f.done.size()==2&&!f.indices.isEmpty();}),"cancelled attempt never poisons next diagnostic attempt");progressTuple(f.progress.mid(0,2),uuid(85),2,0,7000,corpus().size()-7000,{NativeSourceProgressStage::FrameReceived,NativeSourceProgressStage::SourceAdmitted});check(f.progress.size()==4&&f.progress[2].stage==NativeSourceProgressStage::IndexBegin&&f.progress[3].stage==NativeSourceProgressStage::IndexDone,"successor pays separate actual graph audit");f.stop();}
    {qputenv("PGN_SOURCE_FIXTURE_MODE","hold");Fixture f(true);bool cancelled=false;f.onProgress=[&](const auto& p){if(p.stage==NativeSourceProgressStage::FrameReceived)cancelled=f.runtime->cancelSource();};check(f.runtime->startSource(uuid(86),Run)&&until([&]{return !f.done.isEmpty();}),"cancel from actual first-frame creating-thread callback settles");check(cancelled&&f.done.last().status==NativeSourceAttemptStatus::Cancelled&&f.progress.size()>=1&&f.progress.size()<=4,"diagnostic cancellation is bounded and does not invent index completion");for(qsizetype n=0;n<f.progress.size();++n)check(f.progress[n].requestId==uuid(86)&&f.progress[n].sourceEpoch==1&&f.progress[n].stage==ProgressComplete[n],"cancelled callback stages are a prefix of paid work, not a new attempt");f.stop();}
    {qputenv("PGN_SOURCE_FIXTURE_MODE","wrong-run");Fixture f(true);check(f.runtime->startSource(uuid(87),Run)&&until([&]{return !f.done.isEmpty();}),"actual foreign-manifest frame rejected");progressTuple(f.progress,uuid(87),1,0,7000,corpus().size()-7000,{NativeSourceProgressStage::FrameReceived});check(f.done.last().status==NativeSourceAttemptStatus::Failed&&f.pubs.isEmpty(),"admission error is NOT mislabeled index_held/source_admitted");f.stop();}
    {qputenv("PGN_SOURCE_FIXTURE_MODE","progress-large-offset");Fixture f(true);check(f.runtime->startSource(uuid(91),Run)&&until([&]{return !f.done.isEmpty();}),"real encoded frame above2^53 stays exact in diagnostics");progressTuple(f.progress,uuid(91),1,9007199254740993LL,9007199254740993LL,0,{NativeSourceProgressStage::FrameReceived});check(f.done.last().status==NativeSourceAttemptStatus::ReplayRequired&&f.pubs.isEmpty(),"large foreign offset remains replay Held, never fabricated admission");f.stop();}
    {qputenv("PGN_SOURCE_FIXTURE_MODE","progress-unknown-pending");Fixture f(true);check(f.runtime->startSource(uuid(92),Run)&&until([&]{return !f.done.isEmpty()&&!f.indices.isEmpty();}),"actual optional pending bytes remains unknown");progressTuple(f.progress,uuid(92),1,0,7000,{},ProgressComplete);check(f.pubs.last().conservation.value("ready")==false,"diagnostics never turns unknown EOF into conservation proof");f.stop();}
    {qputenv("PGN_SOURCE_FIXTURE_MODE","bulk");Fixture f;check(f.runtime->startSource(uuid(88),Run)&&until([&]{return !f.done.isEmpty()&&!f.indices.isEmpty();}),"real private source indexed before index failure poison");f.stop();const auto before=contents(f.temp.filePath(Run+".stdout"));mutate(f.db,"DELETE FROM native_plot_coordinates WHERE scope=''",{});f.pubs.clear();f.done.clear();f.indices.clear();f.open(true);check(f.runtime->startSource(uuid(89),Run)&&until([&]{return !f.done.isEmpty()&&!f.indices.isEmpty();}),"real reopened index cardinality poison settles independently");progressTuple(f.progress,uuid(89),1,corpus().size(),corpus().size(),0,{NativeSourceProgressStage::FrameReceived,NativeSourceProgressStage::SourceAdmitted,NativeSourceProgressStage::IndexBegin,NativeSourceProgressStage::IndexHeld});check(f.progress.last().error.contains("cardinality")&&f.done.last().status==NativeSourceAttemptStatus::Complete&&f.done.last().error.isEmpty()&&!f.pubs.isEmpty()&&f.indices.last().value("complete")==false&&f.indices.last().value("error")==f.progress.last().error,"graph audit Held remains explicit but cannot suppress committed SOURCE/conservation");check(contents(f.temp.filePath(Run+".stdout"))==before&&scalar(f.db,"SELECT COUNT(*) FROM gaps")==1200,"failed audit keeps exact scientific raw/history, no fake index complete");f.stop();}
}
void asynchronousIndexEpochs(){
    qputenv("PGN_SOURCE_FIXTURE_MODE","bulk");
    {Fixture f;bool switched=false;qsizetype atSwitch=0;f.onDone=[&](const auto& value){if(value.requestId==uuid(130)){atSwitch=f.indices.size();switched=f.runtime->startSource(uuid(131),Other);}};
        check(f.runtime->startSource(uuid(130),Run)&&until([&]{return f.done.size()==2&&!f.indices.isEmpty()&&f.indices.last().value("source_epoch")=="2";}),"new SOURCE epoch can overtake pending render without waiting for its SQL");check(switched,"successor SOURCE admitted explicitly, not by graph callback");for(qsizetype n=atSwitch;n<f.indices.size();++n)check(f.indices[n].value("source_epoch")=="2"&&f.indices[n].value("source_run")==Other,"late old graph cannot relabel or publish into successor SOURCE epoch");
        check(contents(f.temp.filePath(Run+".stdout"))==corpus()&&contents(f.temp.filePath(Other+".stdout"))==corpus()&&scalar(f.db,"SELECT COUNT(*) FROM gaps")==1200,"both exact RAW epochs conserved; graph coalescing duplicates no GAP");f.stop();}
    {Fixture f(true,3000,false);check(f.runtime->startSource(uuid(132),Run)&&until([&]{return !f.done.isEmpty();}),"SOURCE-only caller needs no graph callback/audit");check(f.indices.isEmpty()&&f.progress.size()==2&&f.pubs.last().conservation.value("ready")==true&&contents(f.temp.filePath(Run+".stdout"))==corpus(),"default absent render sink preserves SOURCE/commit/conservation, not fake graphzero");f.stop();}
    {Fixture f(true);bool cancelled=false;f.onTrace=[&](const auto& value){if(value.stage==NativeSourceTraceStage::IndexBegin&&!cancelled){cancelled=true;f.runtime->stop();}};check(f.runtime->startSource(uuid(133),Run)&&until([&]{return f.stopped;}),"stop interrupts/reaps graph actor asynchronously through normal stopped path");check(cancelled&&f.indices.isEmpty(),"no complete display snapshot published after cancellation/stop");f.runtime.reset();}
    {Fixture f;bool deleted=false;f.onIndex=[&](const auto&){f.runtime.reset();deleted=true;};check(f.runtime->startSource(uuid(134),Run)&&until([&]{return deleted;}),"nativeIndex sink may destroy runtime without accessing freed Impl afterward");check(!f.runtime&&f.indices.size()==1,"no late callback targets destroyed graph owner");QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();}
    {Fixture f;f.onIndex=[](const auto&){throw std::runtime_error("closed display sink poison");};check(f.runtime->startSource(uuid(135),Run)&&until([&]{return !f.done.isEmpty()&&!f.indices.isEmpty();}),"throwing nativeIndex sink cannot reject the independent SOURCE attempt");check(f.done.last().status==NativeSourceAttemptStatus::Complete&&f.pubs.last().conservation.value("ready")==true,"display callback exception changes neither conservation nor transport result");f.stop();}
    qunsetenv("PGN_SOURCE_FIXTURE_MODE");
}
void finiteReadLease(){
    for(const auto mode:{"lease","lease-failure","lease-off"}){
        qputenv("PGN_SOURCE_FIXTURE_MODE",mode);Fixture f;const auto actions=f.temp.filePath("leases.txt");qputenv("PGN_SOURCE_FIXTURE_ACTIONS",actions.toUtf8());
        check(f.runtime->startSource(uuid(830),Run),"Own finite read lease starts");
        if(QString::fromLatin1(mode)=="lease"){
            check(until([&]{return !f.pubs.isEmpty()&&f.pubs.last().snapshot.value("fixture_read_lease")==2;}),"Successful owned lease renews at exact persisted cursor without ending source epoch");
            check(f.done.isEmpty()&&f.pubs.last().epoch==1&&f.pubs.last().requestId==uuid(830),"Read renewal preserves original source identity, no terminal/fake control");
            check(contents(actions)=="stream\nstream\n","Exactly two readonly leases, no snapshot/control restart");
            check(f.runtime->cancelSource()&&until([&]{return f.done.size()==1;}),"Cancellation terminates the active renewed reader");
            check(f.done.first().status==NativeSourceAttemptStatus::Cancelled,"Renewed read cancellation is not successful physical completion");
        }else{
            check(until([&]{return f.done.size()==1;}),"Failed or no-longer-owned finite lease settles");
            check(contents(actions)=="stream\n","Failed/closed lease cannot renew or retry a control");
            check(f.done.first().status==(QString::fromLatin1(mode)=="lease-off"?NativeSourceAttemptStatus::Complete:NativeSourceAttemptStatus::Failed),"Original finite outcome retained");
        }
        check(contents(f.temp.filePath(Run+".stdout"))==corpus()&&scalar(f.db,"SELECT COUNT(*) FROM gaps")==1200,"Lease boundary preserves complete RAW and exact once-only scientific archive");
        f.stop();qunsetenv("PGN_SOURCE_FIXTURE_ACTIONS");qunsetenv("PGN_SOURCE_FIXTURE_MODE");
    }
}
void streamAndMailbox(){
    qputenv("PGN_SOURCE_FIXTURE_MODE","bulk");Fixture f;check(f.runtime->startSource(uuid(1),Run),"explicit SOURCE Stream accepted");check(!f.runtime->startSource(uuid(2),Other),"second source attempt refused without retarget");
    // Intentionally do NOT pump the UI thread while the real worker persists
    // all records. Its mailbox must remain one event+latest state, not one
    // queued callback for every source frame. Only own small SQLite is read.
    bool complete=false;for(int i=0;i<1000;++i){if(scalar(f.db,"SELECT MAX(offset) FROM cursors")==corpus().size()){complete=true;break;}QThread::msleep(5);}check(complete,"worker independently persists all source while UI receives no events");QThread::msleep(30);
    check(f.pubs.isEmpty(),"no reentrant cross-thread UI publication");check(until([&]{return !f.done.isEmpty();}),"stream client completion callback");check(f.done[0].status==NativeSourceAttemptStatus::Complete&&f.done[0].requestId==uuid(1)&&f.done[0].run==Run,"typed attempt/run/nonce complete");
    check(f.pubs.size()==1&&f.pubs[0].coalescedFrames>0&&f.pubs[0].snapshot.value("native_display_coalesced")==true,"bounded latest mailbox, coalescing explicitly reported");const auto p=f.pubs[0];
    check(p.snapshot.value("binding")==packet(Run,0,{},0).value("binding"),"original binding preserved, unlike old CLI removal");check(p.snapshot.value("observed_utc")=="2026-10-14T18:00:00.000000001Z"&&p.snapshot.value("source_counter_age_ms")==QJsonValue(qint64(9876543210LL)),"raw observation age/date preserved, no invented freshness");
    check(p.snapshot.value("fixture_stdin_eof")==true&&!p.snapshot.contains("chunk"),"owned fake child stdin null and SOURCE chunk not republished");check(p.nativeIndex.isEmpty()&&p.conservation.value("ready")==true,"SOURCE conservation never waits for graph or invents an empty index");check(until([&]{return !f.indices.isEmpty();})&&f.indices.last().value("complete")==true,"actual all/current graph is a separate coherent snapshot");
    check(scalar(f.db,"SELECT COUNT(*) FROM gaps")==1200&&scalar(f.db,"SELECT COUNT(*) FROM origins")==1200,"all records beyond rich512/coalescing retained in actual SQLite");check(contents(f.temp.filePath(Run+".stdout"))==corpus(),"entire raw source retained byte-exact");
    check(f.runtime->startSource(uuid(3),Other),"explicit successor epoch only after old completion");check(until([&]{return f.done.size()==2;}),"successor stream completion");check(f.done.last().run==Other&&f.done.last().epoch>f.done.first().epoch,"successor distinct epoch/run correlation");check(scalar(f.db,"SELECT COUNT(*) FROM gaps")==1200&&scalar(f.db,"SELECT COUNT(*) FROM origins")==2400,"successor scientific observations deduplicate by p/q, origins per run conserved");
    check(f.runtime->startSource(uuid(4),Run),"explicit A-B-A restoration");check(until([&]{return f.done.size()==3;}),"A-B-A exact EOF replay");check(scalar(f.db,"SELECT COUNT(*) FROM origins")==2400&&f.done.last().nextOffset==corpus().size(),"no duplicate origins or offset reset on restored run");f.stop();check(!f.runtime,"cleanup finished before fixture DB/source deletion");
}
void inventoryAndReplay(){
    {
        qputenv("PGN_SOURCE_FIXTURE_MODE","cold");Fixture f;check(f.runtime->inspectInventory(uuid(10)),"fresh inventory before SOURCE");check(!f.runtime->inspectInventory(uuid(11)),"one inventory acquisition in flight");check(until([&]{return !f.inventories.isEmpty();}),"cold inventory response");const auto i=f.inventories[0];check(i.requestId==uuid(10)&&i.frame&&i.frame->packet.value("manifest").isNull(),"cold raw observation kept, no run invented");check(i.conservation.value("ready")==false&&i.conservation.value("committed_offset").isNull(),"inventory cannot fake conservation0");check(!QFile::exists(f.db)&&!QFile::exists(f.temp.filePath(Run+".stdout")),"inventory never creates SOURCE DB/file");check(!i.frame->packet.contains("finite_device"),"unknown finite3060 not fabricated idle");f.stop();
    }
    {
        qputenv("PGN_SOURCE_FIXTURE_MODE","hold");Fixture f;check(f.runtime->startSource(uuid(12),Run),"holding SOURCE stream accepted");check(until([&]{return !f.pubs.isEmpty();}),"first source prefix committed");const auto raw=contents(f.temp.filePath(Run+".stdout"));const auto cursor=scalar(f.db,"SELECT MAX(offset) FROM cursors");check(f.runtime->inspectInventory(uuid(13),Run),"independent readonly inventory concurrent with SOURCE");check(until([&]{return !f.inventories.isEmpty();}),"fresh concurrent snapshot callback");const auto i=f.inventories[0];check(i.frame&&i.frame->chunk=="inventory-not-consumed\n"&&i.conservation.value("ready")==false,"inventory chunk remains opaque, not consumed");check(contents(f.temp.filePath(Run+".stdout"))==raw&&scalar(f.db,"SELECT MAX(offset) FROM cursors")==cursor,"inventory cannot advance raw/SQLite while SOURCE stream held");check(f.runtime->cancelSource(),"cancel own stream only");check(until([&]{return !f.done.isEmpty();}),"cancelled stream settles");check(f.done[0].status==NativeSourceAttemptStatus::Cancelled&&f.inventories[0].requestId==uuid(13),"source cancel and inventory nonce independent");f.stop();
    }
    {
        qputenv("PGN_SOURCE_FIXTURE_MODE","replay");Fixture f;check(f.runtime->startSource(uuid(14),Run),"mismatched source offset attempt");check(until([&]{return !f.done.isEmpty();}),"replay status returned without automatic loop");check(f.done[0].status==NativeSourceAttemptStatus::ReplayRequired&&f.done[0].nextOffset==0&&!f.done[0].committedOffset,"explicit native replay origin, stored cursor still absent");QThread::msleep(30);check(f.done.size()==1&&!QFile::exists(f.temp.filePath(Run+".stdout")),"no auto reconnect/create after replay request");qputenv("PGN_SOURCE_FIXTURE_MODE","bulk");check(f.runtime->startSource(uuid(15),Run),"caller explicitly retries exact replay");check(until([&]{return f.done.size()==2;}),"replay eventually completes");check(f.done.last().status==NativeSourceAttemptStatus::Complete&&scalar(f.db,"SELECT COUNT(*) FROM gaps")==1200,"replay conserves source once, not skipped");f.stop();
    }
}
void failuresAndOwnerLoss(){
    for(const auto mode:{"malformed","wrong-run"}){qputenv("PGN_SOURCE_FIXTURE_MODE",mode);Fixture f;check(f.runtime->startSource(uuid(20),Run),"negative source attempt accepted, not claimed valid");check(until([&]{return !f.done.isEmpty();}),"negative source result");check(f.done[0].status==NativeSourceAttemptStatus::Failed&&!f.done[0].committedOffset,"decoder/identity error has no fake cursor zero");check(!QFile::exists(f.temp.filePath(Run+".stdout")),"bad frame causes no source file");f.stop();}
    qputenv("PGN_SOURCE_FIXTURE_MODE","exit-fail");{Fixture f;check(f.runtime->inspectInventory(uuid(21),Run),"nonzero snapshot exit attempted");check(until([&]{return !f.inventories.isEmpty();}),"nonzero snapshot status");check(!f.inventories[0].frame&&f.inventories[0].transport.status==SshObservationStatus::Failed,"prefix/multiline document before exit7 is not a valid inventory");f.stop();}
    qputenv("PGN_SOURCE_FIXTURE_MODE","hold");QTemporaryDir temp;check(temp.isValid(),"own ownerloss temp");const auto pidPath=temp.filePath("own-child.pid");qputenv("PGN_SOURCE_FIXTURE_PID",pidPath.toUtf8());
    {
        Fixture f;check(f.runtime->startSource(uuid(22),Run),"ownerloss stream started");check(until([&]{return QFile::exists(pidPath)&&!f.pubs.isEmpty();}),"own fake child identity receipt before deletion");
#ifdef Q_OS_WIN
        bool exact=false;const auto pid=contents(pidPath).toUInt(&exact);check(exact&&pid>0,"own child PID exact");const auto child=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);check(child!=nullptr,"own child handle held before cleanup");wchar_t path[32768];DWORD length=32768;FILETIME birth{},exit{},kernel{},user{};const bool identity=QueryFullProcessImageNameW(child,0,path,&length)&&GetProcessTimes(child,&birth,&exit,&kernel,&user);check(identity&&QDir::fromNativeSeparators(QString::fromWCharArray(path,int(length))).compare(QCoreApplication::applicationFilePath(),Qt::CaseInsensitive)==0,"own SELF image and actual process birth captured, not a user process");
        f.runtime.reset();check(until([&]{return WaitForSingleObject(child,0)==WAIT_OBJECT_0;}),"owner destruction cleans up only own SSH SELF client");CloseHandle(child);check(!f.stopped,"no dangling callback after QObject owner deletion");
#endif
    }
    qunsetenv("PGN_SOURCE_FIXTURE_PID");
}
void cancelledPrefix(){
    QTemporaryDir temp;check(temp.isValid(),"own prefix temp");const auto path=temp.filePath("history.sqlite");const QByteArray batch=QByteArray("status public TESTONLY\n").repeated(2800);check(batch.size()==64400&&batch.size()<=SourceMirror::BatchLimit,"prefix fixture respects actual64KiB packet bound");qint64 at=0;
    {SourceHistory history(path);SourceMirror mirror(temp.path());SourceSession initial(history,mirror);for(int n=0;n<8;++n){const auto r=initial.consume(Run,at,at+batch.size(),batch);need(!r.needsReplay,"prefix setup source admission");at=r.nextOffset;}}
    const auto sourcePath=temp.filePath(Run+".stdout");const auto before=contents(sourcePath);SourceHistory history(path);SourceMirror mirror(temp.path());SourceSession retry(history,mirror);int polls=0;
    rejected([&]{retry.consume(Run,at,at,{},[&]{return ++polls>=5;});},"real mid-prefix64KiB cancellation refuses without admission");check(polls>=5&&history.cursorRecord(Run)->offset==at&&contents(sourcePath)==before,"cancelled prefix preserves exact DB cursor and bytes");const auto r=retry.consume(Run,at,at,{});check(!r.needsReplay&&r.committedOffset==at&&retry.conservation(Run,at,0).ready,"later explicit reattachment/replay exact, no repair or skipped prefix");
}
void lineageBoundary(){
    using namespace pgn::booking;
    qputenv("PGN_SOURCE_FIXTURE_MODE","bulk");Fixture f;const auto bookingPath=f.temp.filePath("booking.sqlite");BookingStore store(bookingPath);SourceRegistry registry;registry.url="https://PUBLIC-TESTONLY.invalid/source";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString("2026-10-14T00:00:00Z",Qt::ISODate);registry.ranges={{1,"100000","110000","Open",{}}};
    check(store.valid()&&store.queue(registry,"100000","110000",Device).isEmpty(),"same temporary native BookingStore manual intent");const auto initial=store.prepareInitialStart(uuid(40),Device,{"100000","110000","100000","110000","100000","10000"},registry,{WorkBoundary::Ready,uuid(41)});check(initial.status==InitialStartStatus::Prepared&&initial.dispatch.has_value(),"real native initial admission, no physical launcher");const auto booking=initial.dispatch->bookingId;qputenv("PGN_SOURCE_FIXTURE_BOOKING",booking.toUtf8());
    check(f.runtime->startSource(uuid(42),Run)&&until([&]{return !f.done.isEmpty();}),"real SOURCE worker conserves declared public fixture");check(f.done[0].status==NativeSourceAttemptStatus::Complete&&!f.pubs.isEmpty(),"lineage input source completed, not a physical claim");const auto snapshot=f.pubs.last().snapshot;
    // Explicit public metadata fixture, NOT a physical acknowledgement.
    mutate(bookingPath,"UPDATE dispatch_requests SET state='STARTED',run=? WHERE id=?",{Run,booking});mutate(bookingPath,"UPDATE bookings SET state='DISPATCHED' WHERE id=?",{booking});check(store.observeRuntime(Run,"100000","110000",Device,false).isEmpty()&&store.saveSourceRunManifest(Device,snapshot.value("manifest").toObject()).isEmpty(),"native immutable actual-shaped manifest and paused public observation stored");
    check(!f.runtime->cancelLineage(),"no-operation lineage cancel does not poison future epoch");
    check(f.runtime->verifyLineage(uuid(43),bookingPath,snapshot),"explicit same-worker lineage boundary accepted");check(!f.runtime->verifyLineage(uuid(44),bookingPath,snapshot)&&!f.runtime->inspectInventory(uuid(45),Run)&&!f.runtime->startSource(uuid(46),Other),"one boundary, fresh acquisition/new epoch explicitly Busy until result");check(until([&]{return !f.lineage.isEmpty();}),"same-worker held Win32 verifier returns");const auto proof=f.lineage[0];check(proof.requestId==uuid(43)&&proof.run==Run&&proof.sourceEpoch==f.done[0].epoch&&proof.epoch>0,"boundary nonce/run/source epoch correlation exact");check(proof.proof.value("ready")==true,proof.proof.value("error").toString().toUtf8().constData());check(proof.proof.value("epochs")==1&&proof.proof.value("terminal")=="complete"&&proof.proof.value("scope")=="LOCAL_RAW_LINEAGE_COUNTER_DELTAS_NOT_INDEPENDENT_PRIME_COVERAGE","real logical proof, no physical Ready or invented prime coverage");
    check(f.runtime->inspectInventory(uuid(47),Run)&&until([&]{return !f.inventories.isEmpty();}),"boundary RAII released before fresh acquisition is allowed");check(f.inventories.last().frame&&f.inventories.last().conservation.value("ready")==true,"source same held epoch remains coherent after verifier boundary");
    check(!f.runtime->cancelLineage(),"settled operation cancel remains no-op");
    check(f.runtime->verifyLineage(uuid(52),bookingPath,snapshot)&&f.runtime->cancelLineage()&&f.runtime->cancelLineage(),"admitted/queued lineage cancel idempotent, no worker message needed");
    check(until([&]{return f.lineage.size()==2;}),"cancelled lineage releases normal completion path");const auto cancelled=f.lineage.last();check(cancelled.requestId==uuid(52)&&cancelled.sourceEpoch==proof.sourceEpoch&&cancelled.epoch>proof.epoch&&cancelled.proof.value("ready")==false&&cancelled.proof.value("error")=="SOURCE lineage cancelled"&&!cancelled.proof.contains("chain_sha256"),"cancelled epoch has exact Held/error correlation, no positive proof");
    check(!f.runtime->cancelLineage()&&f.runtime->verifyLineage(uuid(53),bookingPath,snapshot),"no-op cancel after completion cannot target successor");check(until([&]{return f.lineage.size()==3;}),"successor verification admitted explicitly");check(f.lineage.last().requestId==uuid(53)&&f.lineage.last().epoch>cancelled.epoch&&f.lineage.last().sourceEpoch==proof.sourceEpoch&&f.lineage.last().proof.value("ready")==true,"old cancellation never poisons later source or verifier epoch");
    auto bad=snapshot;auto manifest=bad.value("manifest").toObject();manifest["run"]=Other;bad["manifest"]=manifest;check(f.runtime->verifyLineage(uuid(48),bookingPath,bad)&&until([&]{return f.lineage.size()==4;}),"contradictory run boundary settles explicitly");check(f.lineage.last().proof.value("ready")==false&&f.lineage.last().requestId==uuid(48),"contradictory snapshot never retargets source epoch");
    check(f.runtime->verifyLineage(uuid(49),bookingPath,snapshot),"final boundary before stop");f.runtime->stop();check(!f.runtime->cancelLineage()&&!f.runtime->inspectInventory(uuid(50),Run)&&!f.runtime->startSource(uuid(51),Run),"stopping never starts fresh clients/epochs");check(until([&]{return f.stopped;}),"stop cancels/unwinds boundary and all owned clients before stopped");check(f.lineage.size()==5&&contents(f.temp.filePath(Run+".stdout"))==corpus(),"result path released before stopped, raw unchanged even if valid proof won stop race");f.runtime.reset();qunsetenv("PGN_SOURCE_FIXTURE_BOOKING");
}
void longLineageCancellation(){
    using namespace pgn::booking;
    qputenv("PGN_SOURCE_FIXTURE_MODE","bulk");qputenv("PGN_SOURCE_FIXTURE_LONG","1");Fixture f;
    check(corpus().size()>64000000&&corpus().size()<68000000,"bounded real long journal fixture");
    const auto bookingPath=f.temp.filePath("long-booking.sqlite");BookingStore store(bookingPath);SourceRegistry r;r.url="https://PUBLIC-TESTONLY.invalid/source";r.sha256=QString(64,'1');r.observedUtc=QDateTime::fromString("2026-10-14T00:00:00Z",Qt::ISODate);r.ranges={{1,"100000","110000","Open",{}}};
    check(store.valid()&&store.queue(r,"100000","110000",Device).isEmpty(),"private long fixture native intent");const auto p=store.prepareInitialStart(uuid(70),Device,{"100000","110000","100000","110000","100000","10000"},r,{WorkBoundary::Ready,uuid(71)});check(p.status==InitialStartStatus::Prepared&&p.dispatch,"real long fixture initial tuple");const auto booking=p.dispatch->bookingId;qputenv("PGN_SOURCE_FIXTURE_BOOKING",booking.toUtf8());
    check(f.runtime->startSource(uuid(72),Run)&&until([&]{return !f.done.isEmpty();},60000),"actual worker conserves bounded64MiB journal");check(f.done.last().status==NativeSourceAttemptStatus::Complete&&!f.pubs.isEmpty(),"long real source ingestion completed");const auto snapshot=f.pubs.last().snapshot;const auto epoch=f.done.last().epoch;
    mutate(bookingPath,"UPDATE dispatch_requests SET state='STARTED',run=? WHERE id=?",{Run,booking});mutate(bookingPath,"UPDATE bookings SET state='DISPATCHED' WHERE id=?",{booking});check(store.observeRuntime(Run,"100000","110000",Device,false).isEmpty()&&store.saveSourceRunManifest(Device,snapshot.value("manifest").toObject()).isEmpty(),"long fixture actual immutable manifest");
    const auto rawPath=f.temp.filePath(Run+".stdout");const auto size=QFileInfo(rawPath).size();const auto cursor=scalar(f.db,"SELECT MAX(offset) FROM cursors");check(size==corpus().size()&&cursor==size,"long raw and cursor truly conserved before verification");
#ifdef Q_OS_WIN
    IO_COUNTERS before{};check(GetProcessIoCounters(GetCurrentProcess(),&before),"own process IO baseline");
    check(f.runtime->verifyLineage(uuid(73),bookingPath,snapshot),"long real held-reader verification admitted");bool witnessed=false,cancelled=false;quint64 readDelta=0;
    check(until([&]{IO_COUNTERS now{};need(GetProcessIoCounters(GetCurrentProcess(),&now),"own read-only IO witness");readDelta=now.ReadTransferCount-before.ReadTransferCount;if(readDelta>=4*1024*1024&&readDelta<quint64(size/2)){witnessed=true;cancelled=f.runtime->cancelLineage();return true;}return !f.lineage.isEmpty();},15000),"long read prefix or terminal callback observed");
    if(!witnessed||!cancelled)std::cerr<<"TESTONLY long read witness delta="<<readDelta<<" size="<<size<<" callbacks="<<f.lineage.size()<<" proof="<<(f.lineage.isEmpty()?QByteArray("not delivered"):QJsonDocument(f.lineage.last().proof).toJson(QJsonDocument::Compact)).constData()<<'\n';
    check(witnessed&&cancelled,"cancel paid after real IO prefix, before whole long journal read");check(until([&]{return f.lineage.size()==1;},15000),"mid-read cancellation returns callback and releases held readers");
    const auto result=f.lineage.last();check(result.requestId==uuid(73)&&result.sourceEpoch==epoch&&result.proof.value("ready")==false&&result.proof.value("error")=="SOURCE lineage cancelled"&&!result.proof.contains("chain_sha256"),"long cancelled result is Held not coverage/Ready");
    std::cout<<"LINEAGE_CANCEL_READ_WITNESS bytes="<<readDelta<<" source_bytes="<<size<<" source_epoch="<<epoch<<'\n';
#else
    check(false,"long cancellation IO witness requires actual Win32 target");
#endif
    check(!f.runtime->cancelLineage()&&f.runtime->inspectInventory(uuid(74),Run)&&until([&]{return !f.inventories.isEmpty();}),"fresh readonly inventory after individual cancel");check(f.inventories.last().sourceEpoch==epoch&&f.inventories.last().conservation.value("ready")==true&&QFileInfo(rawPath).size()==size&&scalar(f.db,"SELECT MAX(offset) FROM cursors")==cursor,"source epoch/session/cursor/bytes unaffected by lineage cancel");
    check(f.runtime->verifyLineage(uuid(75),bookingPath,snapshot)&&until([&]{return f.lineage.size()==2;},30000),"subsequent explicit long verification succeeds without restart");check(f.lineage.last().proof.value("ready")==true&&f.lineage.last().sourceEpoch==epoch&&f.lineage.last().epoch>f.lineage.first().epoch,"cancellation does not poison next long proof");check(QCryptographicHash::hash(contents(rawPath),QCryptographicHash::Sha256)==QCryptographicHash::hash(corpus(),QCryptographicHash::Sha256),"long actual raw bytes unmodified");f.stop();qunsetenv("PGN_SOURCE_FIXTURE_BOOKING");qunsetenv("PGN_SOURCE_FIXTURE_LONG");
}
void runSwitchPoison(){
    qputenv("PGN_SOURCE_FIXTURE_MODE","bulk");Fixture f;check(f.runtime->startSource(uuid(60),Run)&&until([&]{return !f.done.isEmpty();}),"A real prefix known before B poison");const auto original=contents(f.temp.filePath(Run+".stdout"));
    mutate(f.db,"INSERT INTO cursors(source,offset,run) VALUES(?,CAST('123' AS BLOB),?)",{"native/"+Other+".stdout",Other});
    check(f.runtime->startSource(uuid(61),Other)&&until([&]{return f.done.size()==2;}),"B exact stored type poison reaches native lookup");const auto bad=f.done.last();check(bad.status==NativeSourceAttemptStatus::Failed&&bad.run==Other&&bad.requestId==uuid(61),"B error keeps requested identity/status");
    check(!bad.nextOffset&&!bad.committedOffset,"B metadata failure must NOT relabel A offsets or invent zero");check(contents(f.temp.filePath(Run+".stdout"))==original&&!QFile::exists(f.temp.filePath(Other+".stdout")),"failed B admission preserves A bytes/no B raw created");f.stop();
}
}
void completedSnapshotRead(){
    qputenv("PGN_SOURCE_FIXTURE_MODE","bulk");Fixture f(true);
    const auto actions=f.temp.filePath("completed-actions.txt");qputenv("PGN_SOURCE_FIXTURE_ACTIONS",actions.toUtf8());
    check(f.runtime->startSource(uuid(201),Run)&&until([&]{return f.done.size()==1&&!f.indices.isEmpty();}),"SELF source fixture conserved before testing explicit completed read");
    const auto before=contents(f.temp.filePath(Run+".stdout"));
    const auto firstReceipt=f.pubs.last().resourceReceiptNs;
    check(firstReceipt&&*firstReceipt>=0&&*firstReceipt<=receiptMonotonicNs(),"actual worker carries pre-admission LOCAL receipt across its typed publication mailbox");
    check(scalar(f.db,"SELECT COUNT(*) FROM gaps")==1200,"own fixture complete archive, never production first preparation");
    check(f.runtime->startSource(uuid(202),Run,{},NativeSourceReadKind::CompletedSnapshot)&&until([&]{return f.done.size()==2&&f.indices.size()>=2;}),"completed source uses finite snapshot and clean EOF");
    check(f.done.last().status==NativeSourceAttemptStatus::Complete&&f.done.last().transport.completeFrames==1,"one actual finite source frame, not larger live stream timeout or retry");
    check(!f.pubs.isEmpty()&&f.pubs.last().resourceReceiptNs&&*f.pubs.last().resourceReceiptNs>=*firstReceipt&&*f.pubs.last().resourceReceiptNs<=receiptMonotonicNs(),"new completed read carries its own advancing local receipt, never the previous sample timestamp");
    check(contents(actions)=="stream\nsnapshot\n","existing stream contract unchanged; explicit completed read is snapshot only");
    check(contents(f.temp.filePath(Run+".stdout"))==before&&scalar(f.db,"SELECT COUNT(*) FROM gaps")==1200,"completed read neither replays mining work nor duplicates/loses archived gaps");
    check(!f.runtime->startSource(uuid(203),Run,{},static_cast<NativeSourceReadKind>(99)),"unknown source read kind refused");
    f.stop();qunsetenv("PGN_SOURCE_FIXTURE_ACTIONS");qunsetenv("PGN_SOURCE_FIXTURE_MODE");
}
void completedPrefixRead(){
    qputenv("PGN_SOURCE_FIXTURE_MODE","completed-pages");Fixture f;
    const auto actions=f.temp.filePath("completed-actions.txt");qputenv("PGN_SOURCE_FIXTURE_ACTIONS",actions.toUtf8());
    check(f.runtime->startSource(uuid(204),Run,{},NativeSourceReadKind::CompletedSnapshot)&&until([&]{return !f.done.isEmpty();}),"cold completed source drains bounded conserved pages");
    check(f.done.last().status==NativeSourceAttemptStatus::Complete&&f.done.last().committedOffset==corpus().size(),"completion cannot stop at first successful partial page");
    check(contents(f.temp.filePath(Run+".stdout"))==corpus()&&scalar(f.db,"SELECT COUNT(*) FROM gaps")==1200,"offline completed RAW and every GAP conserved exactly once");
    check(contents(actions).startsWith("snapshot\nstream\n")&&contents(actions).count("snapshot\n")==1,"one initial snapshot then finite batched READ leases, no physical control");
    f.stop();qunsetenv("PGN_SOURCE_FIXTURE_ACTIONS");qunsetenv("PGN_SOURCE_FIXTURE_MODE");
}
void emittedCounterTelemetry(){
    qputenv("PGN_SOURCE_FIXTURE_MODE","telemetry-emitted");Fixture f(true);
    check(f.runtime->startSource(uuid(301),Run)&&until([&]{return !f.done.isEmpty();}),"native emitted-counter display projection settles");
    check(f.done.last().status==NativeSourceAttemptStatus::Complete&&contents(f.temp.filePath(Run+".stdout"))==corpus(),"native display suffix does not alter raw conservation");
    bool retained=false;for(const auto& p:f.pubs)for(const auto& line:p.snapshot.value("lines").toArray())if(line.toString().endsWith(" emitted_gaps=1200"))retained=true;
    check(retained,"emitted_gaps reaches display without entering the GAP channel");f.stop();qunsetenv("PGN_SOURCE_FIXTURE_MODE");
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    if(app.arguments().contains("-F"))return fake(app.arguments());
    if(app.arguments().contains("--emitted-telemetry-only")){emittedCounterTelemetry();std::cout<<"NATIVE_EMITTED_TELEMETRY "<<checks<<" checks / 0 failures; own SELF/SQLite raw conservation, no GPU/network\n";return 0;}
    if(app.arguments().contains("--finite-lease-only")){finiteReadLease();std::cout<<"FINITE_SOURCE_READ_LEASE "<<checks<<" checks / 0 failures; own SELF only, no physical GPU/control\n";return 0;}
    if(app.arguments().contains("--completed-snapshot-only")){completedSnapshotRead();completedPrefixRead();std::cout<<"COMPLETED_SOURCE_SNAPSHOT "<<checks<<" checks / 0 failures; own SELF/SQLite fixture, no real SSH/GPU/UI or production data\n";return 0;}
    if(app.arguments().contains("--graph-reader-only")){streamAndMailbox();firstFrameProgress();progressHeldAndCancellation();asynchronousIndexEpochs();std::cout<<"NATIVE_SOURCE_ASYNC_INDEX "<<checks<<" checks / 0 failures; own SELF/SQLite only, no64MiB/realSSH/GPU/UI\n";return 0;}
    if(app.arguments().contains("--trace-reentrant-only")){reentrantTraceDelivery();std::cout<<"NATIVE_SOURCE_RUNTIME_REENTRANT_TRACE "<<checks<<" checks / 0 failures; two OWN SELF readonly acquisitions, no real SSH/GPU/UI/user data\n";return 0;}
    if(app.arguments().contains("--worker-starvation-only")||app.arguments().contains("--inventory-isolation-only")){workerEventStarvation();if(app.arguments().contains("--inventory-isolation-only")){isolatedInventoryLifetime();inventoryAndReplay();failuresAndOwnerLoss();firstFrameProgress();progressHeldAndCancellation();reentrantTraceDelivery();}std::cout<<"NATIVE_SOURCE_RUNTIME_INVENTORY_ISOLATION "<<checks<<" checks / 0 failures; true private SQLite contention and OWN SELF physical exit only; no real SSH/GPU/UI/user data\n";return 0;}
    if(!app.arguments().contains("--progress-only")){if(!app.arguments().contains("--lineage-cancel-only")){streamAndMailbox();inventoryAndReplay();failuresAndOwnerLoss();cancelledPrefix();lineageBoundary();}longLineageCancellation();if(!app.arguments().contains("--lineage-cancel-only"))runSwitchPoison();}
    if(!app.arguments().contains("--lineage-cancel-only")){firstFrameProgress();progressHeldAndCancellation();}
    qunsetenv("PGN_SOURCE_FIXTURE_MODE");std::cout<<"NATIVE_SOURCE_RUNTIME "<<checks<<" checks / 0 failures; true worker/source Win32+SQLite and OWN SELF fake SSH only, stream mailbox/replay/readonly inventory/cancel/lifetime/held lineage/first-frame progress; no real SSH/GPU/UI/Node/user data\n";return 0;
}catch(const std::exception& e){std::cerr<<"NATIVE_SOURCE_RUNTIME failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}

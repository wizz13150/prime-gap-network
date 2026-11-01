#include "mining/native_application_inventory.h"
#include "mining/native_observation_session.h"
#include "mining/native_bridge.h"
#include "booking/booking_store.h"
#include <sqlite3.h>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <cstdio>
#include <iostream>
#include <stdexcept>
using namespace pgn::mining;
namespace {
int checks{},failures{};
void check(bool yes,const char* why){++checks;if(!yes){++failures;std::cerr<<"FAIL "<<why<<'\n';}}
void need(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
QString id(int n){return "30000000-0000-4000-8000-"+QString::number(n,16).rightJustified(12,'0');}
const QString Constant="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",Run(32,'a'),Time="2026-10-15T00:00:00.1234567Z";
const QString FreshTime="2026-10-15T00:00:01.1234567Z";
const QByteArray Source="[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=2 original_start=100000 length=10000 proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n[pgn-counters] time_ns=9007199254740993 positions=10000 gaps=0 tests=123 batches=1 processed_gaps=0\n[phase0-gpu] gaps reported=0 verification_failures=0 records_new=0\n[phase0-gpu] --- end ---\n";
template<class F>bool waitFor(F f,int ms=30000){QElapsedTimer age;age.start();while(!f()&&age.elapsed()<ms){QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}return f();}
QJsonObject constant(){return {{"schema","pgn-runtime-snapshot-1"},{"owned",false},{"driver_valid",true},{"state","OFF"},{"operator_stop",true},{"has_checkpoint",false},
    {"observed_utc",Time},{"offset",0},{"next_offset",0},{"pending_bytes",0},{"chunk",""},{"manifest",QJsonValue::Null},{"last_exit",QJsonValue::Null},
    {"binding",QJsonObject{{"uuid",Constant},{"start","199999990000000000000"},{"end","200000000000000000000"}}}};}
QJsonObject finite(){return {{"schema","pgn-device-control-observation-1"},{"device",Finite3060Device},{"scope","BOUNDED_SCIENCE_TEST"},{"observed_utc",Time},
    {"owned",false},{"desired_enabled",false},{"stop_requested",false},{"manifest",QJsonValue::Null},{"last_exit",QJsonValue::Null},{"last_error",QJsonValue::Null},{"source_credit",false},{"auto_advance",false}};}
void output(const QJsonObject& object){const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);need(std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout)==size_t(bytes.size())&&std::fflush(stdout)==0,"own fake output");}
int fake(const QStringList& args){
    const auto wide=QByteArray::fromBase64(args.last().section(' ',-1).toLatin1());
    const auto script=QString::fromUtf16(reinterpret_cast<const char16_t*>(wide.constData()),wide.size()/2);
    const bool isFinite=script.contains("test-3060/native-test-control.ps1");
    const bool stream=script.contains("-Action stream");
    need(script.contains("-Action snapshot")||script.contains("-Action 'snapshot'")||stream,"own fixture only observation, no control/mining");
    need(std::fgetc(stdin)==EOF,"own fake SSH stdin null");
    QFile marker(qEnvironmentVariable("PGN_APP_INVENTORY_MARKER"));need(marker.open(QIODevice::WriteOnly|QIODevice::Append),"own marker");
    need(marker.write(isFinite?"finite\n":stream?"stream\n":"constant\n")>0&&marker.flush(),"own marker write");marker.close();
    const auto mode=qEnvironmentVariable("PGN_APP_INVENTORY_MODE");
    if(mode=="slow")QThread::msleep(15000);
    auto sample=isFinite?finite():constant();
    if(!isFinite&&mode.startsWith("driver-false"))sample["driver_valid"]=false;
    if(!isFinite&&mode=="driver-unknown-idle")sample["driver_valid"]=QJsonValue::Null;
    if(!isFinite&&mode=="driver-false-owned"){sample["owned"]=true;sample["state"]="RUNNING";}
    if(mode=="constant-bad"&&!isFinite)sample.remove("owned");
    if(mode=="finite-bad"&&isFinite)sample.remove("desired_enabled");
    if(mode=="finite-launch-uncertain"&&isFinite)sample["desired_enabled"]=true;
    if((mode=="running"||mode=="stopped-old")&&!isFinite){
        const auto b=sample["binding"].toObject();sample["manifest"]=QJsonObject{{"schema","pgn-native-process-1"},{"uuid",Constant},{"run",Run},{"pid",1234},{"birth",Time},{"start",b["start"]},{"end",b["end"]}};
        if(mode=="running"){sample["state"]="RUNNING";sample["owned"]=true;}
    }
    if(mode.startsWith("source-complete")&&!isFinite){
        if(mode=="source-complete-race")QThread::msleep(stream?300:700);
        const auto match=QRegularExpression("-Offset ([0-9]+) -ExpectedRun '([0-9a-f]*)'").match(script);need(match.hasMatch(),"own source exact request");
        const auto offset=match.captured(1).toLongLong();need(offset>=0&&offset<=Source.size(),"own source offset");
        if(stream&&mode=="source-complete-stream-fail"){need(offset==Source.size(),"failed SOURCE really requests the existing complete prefix EOF");return 255;}
        const auto tail=Source.mid(offset);sample["offset"]=offset;sample["next_offset"]=qint64(Source.size());sample["chunk"]=QString::fromLatin1(tail.toBase64());
        sample["binding"]=QJsonObject{{"uuid",Constant},{"start","100000"},{"end","110000"}};
        sample["manifest"]=QJsonObject{{"schema","pgn-native-process-1"},{"uuid",Constant},{"run",Run},{"pid",1234},{"birth",Time},{"start","100000"},{"end","110000"},{"stdout","C:/PUBLIC-TESTONLY/"+Run+".stdout"}};
        sample["last_exit"]=QJsonObject{{"run",Run},{"exit_code",0}};
        if(stream&&mode=="source-complete-two-stage"){
            need(offset==0,"new own stream begins at exact absent cursor");const auto prefix=Source.indexOf('\n')+1;
            auto first=sample;first["next_offset"]=qint64(prefix);first["chunk"]=QString::fromLatin1(Source.left(prefix).toBase64());
            output(first);need(std::fputs("\n",stdout)>=0&&std::fflush(stdout)==0,"first genuine partial prefix frame");
            const auto release=qEnvironmentVariable("PGN_APP_INVENTORY_RELEASE");QElapsedTimer wait;wait.start();while(!QFileInfo::exists(release)&&wait.elapsed()<10000)QThread::msleep(1);
            need(QFileInfo::exists(release),"test explicitly releases genuine second SOURCE frame");
            sample["offset"]=qint64(prefix);sample["chunk"]=QString::fromLatin1(Source.mid(prefix).toBase64());output(sample);need(std::fputs("\n",stdout)>=0&&std::fflush(stdout)==0,"second genuine frame delimiter");return 0;
        }
        if(stream&&mode=="source-complete-late-footer"&&offset==0){
            // The live transport ends successfully at the bytes then present.
            // A later stopped inventory observes the producer's final suffix.
            const auto prefix=Source.indexOf("[phase0-gpu] gaps reported=");need(prefix>0,"own terminal suffix exists");
            sample["next_offset"]=qint64(prefix);sample["chunk"]=QString::fromLatin1(Source.left(prefix).toBase64());
            sample["owned"]=true;sample["state"]="RUNNING";sample["last_exit"]=QJsonValue::Null;
            output(sample);need(std::fputs("\n",stdout)>=0&&std::fflush(stdout)==0,"successful live prefix delimiter");return 0;
        }
        const auto booking=qEnvironmentVariable("PGN_APP_INVENTORY_BOOKING");
        if(!booking.isEmpty()){
            auto manifest=sample["manifest"].toObject();manifest["queued_intent"]=booking;sample["manifest"]=manifest;sample["operator_stop"]=false;
            sample["source_completion_observation"]=QJsonObject{{"schema","pgn-source-range-completion-1"},{"run",Run},{"queued_intent",booking},{"start","100000"},{"end","110000"},
                {"source","C:/PUBLIC-TESTONLY/"+Run+".stdout"},{"source_positions","10000"},{"source_session_gaps","0"},{"source_tests","123"},{"source_exit_code",0},
                {"source_range_complete",true},{"independent_coverage",false},{"proof_status","SOURCE_PRP_INDEPENDENT_PROOF_PENDING"},{"source_tests_complete",true},{"source_position_evidence","final_counter"}};
        }
        QFile seen(qEnvironmentVariable("PGN_APP_INVENTORY_MARKER"));need(seen.open(QIODevice::ReadOnly),"own acquisition marker");
        if(!stream&&seen.readAll().count("constant\n")==2){
            sample["observed_utc"]=FreshTime;
            if(mode=="source-complete-identity-drift"){auto manifest=sample["manifest"].toObject();manifest["pid"]=1235;sample["manifest"]=manifest;}
            if(mode=="source-complete-receipt-drift"){auto receipt=sample["source_completion_observation"].toObject();receipt["source_tests"]="124";sample["source_completion_observation"]=receipt;}
            if(mode=="source-complete-bytes-drift")sample["pending_bytes"]=1;
            if(mode=="source-complete-stop")sample["operator_stop"]=true;
        }
    }
    if(stream&&(mode=="source-complete-progress"||mode=="source-complete-stall")){
        qint64 at=sample.value("offset").toInteger();unsigned frames{};
        do{
            if(frames)QThread::msleep(1100);
            const auto end=(mode=="source-complete-stall"&&frames)?at:qint64(Source.indexOf('\n',at)+1);
            need(end>=at&&end<=Source.size(),"bounded true source line");
            auto part=sample;part["offset"]=at;part["next_offset"]=end;part["pending_bytes"]=qint64(Source.size())-end;part["chunk"]=QString::fromLatin1(Source.mid(at,end-at).toBase64());
            output(part);need(std::fputs("\n",stdout)>=0&&std::fflush(stdout)==0,"own progress frame");at=end;++frames;
        }while(at<Source.size()&&frames<3);
        return 0;
    }
    output(sample);if(stream)need(std::fputs("\n",stdout)>=0&&std::fflush(stdout)==0,"own stream delimiter");return 0;
}
struct Fixture {
    QTemporaryDir temp;NativeApplicationWriter writer;NativeApplicationInventoryConfig config;
    QList<QJsonObject> packets,phases;QStringList events;int fresh{},publications{},finished{},boundaries{},refreshes{};bool stopped{};
    QJsonObject lastSource,boundarySnapshot;bool expectBoundaryConserved{true};
    NativeSourceAttemptStatus expectedSourceStatus{NativeSourceAttemptStatus::Complete};std::optional<NativeSourceAttempt> lastAttempt;
    std::unique_ptr<NativeApplicationInventory> inventory;
    std::function<void(const QJsonObject&)> additional;
    std::function<void(const QJsonObject&)> phaseAction;
    std::function<void()> freshAction;
    std::function<void(std::function<void(QString)>)> refreshAction;
    std::function<void(const NativeCloseInitialObservation&)> initialAction;
    Fixture(const char* mode="cold",bool progressRepair=false){
        need(temp.isValid(),"own temporary directory");const auto key=temp.filePath("PUBLIC-TESTONLY.key");QFile f(key);
        need(f.open(QIODevice::WriteOnly)&&f.write("PUBLIC-TESTONLY\n")>0,"own nonsecret fake endpoint file");f.close();
        config.source={{QCoreApplication::applicationFilePath(),key,"PUBLIC-TESTONLY.invalid","fixture",22},Constant,temp.filePath("history.sqlite"),temp.path(),10000,20000,20000};
        if(progressRepair)config.source.snapshotTimeoutMs=2000;
        config.bookingDatabase=temp.filePath("booking.sqlite");config.finiteTimeouts={10000,20000};
        qputenv("PGN_APP_INVENTORY_MODE",mode);qputenv("PGN_APP_INVENTORY_MARKER",temp.filePath("marker.txt").toUtf8());qunsetenv("PGN_APP_INVENTORY_BOOKING");
        NativeApplicationInventoryCallbacks callbacks;
        callbacks.phase=[this](const auto& p){phases.append(p);if(phaseAction)phaseAction(p);};
        callbacks.packet=[this](const auto& p){packets.append(p);events.append(p.value("type").toString());if(additional)additional(p);};
        callbacks.startObserved=[this](const auto&,const auto&,const auto& frame){++fresh;events.append("fresh");check(frame.packet.value("observed_utc")==Time||frame.packet.value("observed_utc")==FreshTime,"actual source UTC preserved at fresh acquisition");if(freshAction)freshAction();};
        callbacks.closeInitialObserved=[this](const auto& value){if(initialAction)initialAction(value);};
        callbacks.boundary=[this](const auto&,const auto&,const auto& frame,const auto& conservation){++boundaries;boundarySnapshot=frame.packet;check(conservation.value("ready")==expectBoundaryConserved,"boundary preserves expected actual conservation, never cold Ready");};
        callbacks.beforeRefresh=[this](const auto&,const auto&,auto done){++refreshes;events.append("profile-before-refresh");if(refreshAction)refreshAction(std::move(done));else done({});};
        callbacks.stopped=[this]{stopped=true;};
        callbacks.publication=[this](const auto& value){++publications;lastSource=value.snapshot;check(value.run==Run,"source publication keeps true admitted run");};
        callbacks.sourceFinished=[this](const auto& value){++finished;lastAttempt=value;if(value.status!=expectedSourceStatus)std::cerr<<"SOURCE_OUTCOME mode="<<qEnvironmentVariable("PGN_APP_INVENTORY_MODE").toStdString()<<" actual="<<int(value.status)<<" error="<<value.error.toStdString()<<'\n';check(value.status==expectedSourceStatus,"own true source attempt has expected typed outcome");};
        if(progressRepair)callbacks.sourceBoundaryRead=[this](const auto&,const auto&,const auto& frame){const auto request=id(990);return inventory->source().startSource(request,Run,frame.packet.value("manifest").toObject())?request:QString{};};
        inventory=std::make_unique<NativeApplicationInventory>(writer,config,std::move(callbacks));
    }
    ~Fixture(){if(inventory){inventory->stop();check(waitFor([this]{return stopped;}),"own SOURCE worker shutdown observed");inventory.reset();}}
    int count(const QByteArray& word){QFile f(temp.filePath("marker.txt"));if(!f.open(QIODevice::ReadOnly))return 0;return f.readAll().count(word+'\n');}
    QJsonObject byType(const char* type){for(auto it=packets.crbegin();it!=packets.crend();++it)if(it->value("type")==type)return *it;return {};}
    void freeze(int n){check(inventory->prepareClose(id(n)),"one close freeze accepted");check(writer.frozen(),"admission freezes synchronously");check(waitFor([this]{return !byType("application_close_frozen").isEmpty();}),"barrier delivered");}
};
void initialAndBridge(){
    Fixture f;NativeBridge bridge;QJsonObject reported;
    f.additional=[&](const auto& packet){check(bridge.acceptNativeRuntimePacket(f.inventory.get(),packet),"bridge accepts own runtime only");};
    check(bridge.attachNativeRuntime(f.inventory.get(),[&](const auto& command){
        if(command.value("action")=="application_start_inventory")return f.inventory->inspectStart(command.value("request_id").toString(),command.value("inventory_id").toString());
        return false;
    }),"real bridge in-process attachment");
    bridge.onApplicationStartInventory=[&](const auto&,const auto& packet){reported=packet;};
    check(bridge.inspectApplicationStart(id(1),id(2)),"bridge queued start inventory");check(reported.isEmpty(),"no reentrant reply");
    check(waitFor([&]{return !reported.isEmpty();}),"actual SELF snapshot returns through source-worker/writer/bridge");
    check(reported.value("accepted")==true,"cold fresh acquisition accepted, not launch permission");
    check(f.fresh==1&&f.events.indexOf("fresh")<f.events.indexOf("application_start_inventory"),"TTL acquisition hook before presentation");
    check(reported.value("snapshot").toObject().value("manifest").isNull(),"cold manifest stays explicit null");
    check(reported.value("snapshot").toObject().value("observed_utc")==Time,"transport never re-ages observation");
    check(reported.value("source_conservation").toObject().value("ready")==false,"no source attachment/conservation invented by inventory");
    check(f.count("constant")==1&&f.count("finite")==0,"start has exactly one own readonly client");
    check(!f.inventory->inspectStart(id(3),id(2)),"consumed inventory nonce cannot rebind to new start");
    check(f.count("constant")==1,"nonce refusal sends no second request");
}
void neverAttachedLineage(){
    for(const auto& purpose:QStringList{"start","close","boundary"}){
        Fixture f("stopped-old");f.expectBoundaryConserved=false;
        if(purpose=="close")f.freeze(80);
        const bool admitted=purpose=="start"?f.inventory->inspectStart(id(80),id(81)):
            purpose=="close"?f.inventory->inspectClose(id(80),id(81)):f.inventory->inspectBoundary(id(80),id(81));
        check(admitted,"never-attached stopped-run inventory admitted for typed observation only");
        const auto type=purpose=="start"?"application_start_inventory":purpose=="close"?"application_close_inventory":"native_work_boundary_inventory";
        check(waitFor([&]{return f.byType(type).value("inventory_id")==id(81);}),"cold epoch inventory settles without SOURCE initialization");
        const auto envelope=f.byType(type),snapshot=envelope.value("snapshot").toObject(),proof=snapshot.value("resume_lineage").toObject(),conservation=envelope.value("source_conservation").toObject();
        check(envelope.value("accepted")==true&&!envelope.contains("ready")&&!envelope.contains("applied"),"physical observation is not logical Ready/application");
        check(f.count("constant")==1&&f.count("finite")==int(purpose=="close")&&f.count("stream")==0,"epoch0 has no redundant snapshot or SOURCE child");
        check(f.refreshes==0,"epoch0 admission refusal cannot invoke post-lineage profile refresh");
        if(purpose=="close"){
            check(!snapshot.contains("resume_lineage"),"Initial close has no final lineage evidence");
            check(f.inventory->inspectClose(id(80),id(83))&&waitFor([&]{return f.byType(type).value("inventory_id")==id(83);}),"separate Final cold observation explicitly pays lineage admission check");
            const auto finalProof=f.byType(type).value("snapshot").toObject().value("resume_lineage").toObject();
            check(finalProof.value("ready")==false&&!finalProof.value("error").toString().isEmpty()&&f.count("constant")==2&&f.count("finite")==2,"Final epoch0 still has explicit unavailable proof, no duplicate post-proof refresh");
        }else check(proof.value("ready")==false&&!proof.value("error").toString().isEmpty(),"epoch0 preserves explicit unavailable lineage proof");
        check(conservation.value("ready")==false&&conservation.value("committed_offset").isNull(),"no prefix/cursor0 conservation invented");
        check(snapshot.value("observed_utc")==Time&&f.fresh==int(purpose=="start"),"original acquired UTC and start TTL hook preserved without re-age");
        check(!QFile::exists(f.config.source.historyDatabase)&&!QFile::exists(f.temp.filePath(Run+".stdout")),"cold lineage refusal creates no SQL/mirror data");
        check(!f.inventory->source().verifyLineage(id(82),f.config.bookingDatabase,snapshot),"public epoch0 verifyLineage is synchronously not enqueued");
        check(!f.inventory->source().cancelLineage(),"rejected epoch0 verification leaves no admitted cancellation epoch");
    }
}
void closeCases(){
    for(const auto mode:{"cold","constant-bad","finite-bad","finite-launch-uncertain","running","stopped-old"}){
        Fixture f(mode);f.freeze(10);
        check(!f.inventory->inspectStart(id(11),id(12)),"start inventory refused after close freeze");
        check(!f.inventory->inspectClose(id(13),id(14)),"different close cannot take over frozen identity");
        check(f.inventory->inspectClose(id(10),id(15)),"correlated close inventory admitted");
        check(waitFor([&]{return f.count("finite")==1&&!f.byType("application_close_inventory").value("snapshot").isUndefined()&&f.byType("application_close_inventory").value("inventory_id")==id(15);}),"both actual device acquisitions settle");
        const auto packet=f.byType("application_close_inventory");const bool constantBad=QByteArray(mode)=="constant-bad",finiteBad=QByteArray(mode)=="finite-bad";
        check(packet.value("constant_known")==!constantBad,"constant known pays physical types, no absent-owned default");
        check(packet.value("accepted")==(!constantBad&&!finiteBad),"envelope requires both true acquired observations");
        const auto unknown=packet.value("unknown_devices").toArray();check(unknown.size()==int(constantBad||finiteBad),"only actually unknown device listed");
        check(unknown.contains(Constant)==constantBad&&unknown.contains(Finite3060Device)==finiteBad,"unknown inventory retains exact card identity");
        check(f.count("constant")==1&&f.count("finite")==1,"never-attached close has one actual acquisition per device, no lineage refresh");
        check(f.fresh==0,"close cannot mint a new start TTL ticket");
        check(!packet.contains("ready")&&!packet.contains("applied"),"even complete inventory is not physical Ready/ACK");
        if(QByteArray(mode)=="finite-launch-uncertain")check(packet.value("finite_device").toObject().value("snapshot").toObject().value("desired_enabled")==true,"pending finite launch with null manifest remains explicitly enabled, not idle");
        if(QByteArray(mode)=="running")check(!packet.value("snapshot").toObject().contains("resume_lineage"),"running initial inventory does not scan history before STOP");
        if(QByteArray(mode)=="stopped-old")check(!packet.value("snapshot").toObject().contains("resume_lineage"),"stopped old Initial also has no final proof; separate Final cold check remains Held");
    }
}
void admissionAndLifetime(){
    {Fixture f;for(int i=0;i<NativeApplicationWriter::PendingLimit;++i)check(f.writer.enqueue(NativeApplicationOperation::Read,[](auto done){done({});},[](const auto&){}),"fill command admission queue");
        check(!f.inventory->inspectStart(id(20),id(21)),"writer overload refuses immediately, not stuck pending");
        check(waitFor([&]{return !f.packets.isEmpty();}),"overload refusal packet delivered");
        bool drained=false;check(waitFor([&]{return f.writer.enqueue(NativeApplicationOperation::Read,[&](auto done){drained=true;done({});},[](const auto&){});}),"writer queue drains");
        check(waitFor([&]{return drained;}),"writer usable after overload");
        check(f.inventory->inspectStart(id(22),id(23)),"new nonce after overload can acquire");check(waitFor([&]{return f.fresh==1;}),"no orphan pending slot after refused enqueue");}
    {Fixture f;f.freshAction=[&]{f.inventory->stop();};check(f.inventory->inspectStart(id(24),id(25)),"start before cleanup");
        check(waitFor([&]{return !f.packets.isEmpty();}),"cleanup in acquisition hook settles attempt");check(f.packets.value(0).value("accepted")==false,"stop during callback cannot publish accepted old envelope");check(f.count("finite")==0,"no finite client after local cleanup");}
    {Fixture f("slow");check(f.inventory->inspectStart(id(30),id(31)),"own slow client admitted");check(waitFor([&]{return f.count("constant")==1;}),"own slow client actually entered");
        f.inventory.reset();bool next=false;check(f.writer.enqueue(NativeApplicationOperation::Read,[&](auto done){next=true;done({});},[](const auto&){}),"writer read after inventory destruction queued");
        check(waitFor([&]{return next;}),"destruction releases own local writer attempt");check(f.packets.isEmpty(),"no callback after destroyed inventory");}
    {Fixture f;check(f.inventory->inspectStart(id(32),id(33)),"queued inventory accepted");f.inventory->stop();
        check(waitFor([&]{return !f.packets.isEmpty()&&f.packets.value(0).value("accepted")==false;}),"queued cleanup error returns");}
}
void conservedSource(){
    Fixture f("source-complete");check(f.inventory->source().startSource(id(40),Run),"actual integrated SOURCE client admitted");
    check(waitFor([&]{return f.finished==1&&f.publications>0;}),"real raw stream committed on same worker before boundary");
    f.freeze(41);check(f.inventory->inspectClose(id(41),id(42)),"post-SOURCE actual close inventory");
    check(waitFor([&]{return f.byType("application_close_inventory").value("inventory_id")==id(42);}),"post-SOURCE envelope delivered");
    const auto p=f.byType("application_close_inventory"),c=p.value("source_conservation").toObject();
    check(p.value("accepted")==true,"both actual snapshots accepted after SOURCE");
    check(c.value("ready")==true&&c.value("run")==Run,"inventory uses admitted run, not empty selected epoch");
    check(c.value("committed_offset").toInteger(-1)==Source.size()&&c.value("required_end").toInteger(-1)==Source.size(),"same committed raw bytes cover exact observed end");
    check(!p.value("snapshot").toObject().contains("resume_lineage")&&f.count("constant")==1&&f.count("finite")==1,"Initial remains physical only despite existing conservation");
    check(f.inventory->inspectClose(id(41),id(43))&&waitFor([&]{return f.byType("application_close_inventory").value("inventory_id")==id(43);}),"separate Final pays all logical proof checks");
    check(f.byType("application_close_inventory").value("snapshot").toObject().value("resume_lineage").toObject().value("ready")==false,"Final conservation alone still cannot forge missing booking lineage");
    check(f.count("stream")==1&&f.count("constant")==3&&f.count("finite")==2,"Final lineage pays a new real acquisition, no polling retry");
}
void stoppedSuccessfulReadNeedsSuffix(bool boundary){
    Fixture f("source-complete-late-footer",true);
    check(f.inventory->source().startSource(id(920),Run),"successful finite live prefix starts");
    check(waitFor([&]{return f.finished==1&&f.publications>0;}),"live EOF really completed before terminal write");
    const auto prefix=Source.indexOf("[phase0-gpu] gaps reported=");
    check(f.lastAttempt&&f.lastAttempt->nextOffset==prefix&&f.count("stream")==1,"successful EOF has only its paid prefix");
    const auto type=boundary?"native_work_boundary_inventory":"application_start_inventory";
    const auto inspect=[&](int request,int inventory){return boundary?f.inventory->inspectBoundary(id(request),id(inventory)):f.inventory->inspectStart(id(request),id(inventory));};
    check(inspect(921,922),"fresh stopped start or boundary observes later EOF");
    check(waitFor([&]{return f.byType(type).value("inventory_id")==id(922);}),"observed terminal suffix is enrolled before inventory settles");
    const auto packet=f.byType(type),conservation=packet.value("source_conservation").toObject();
    check(packet.value("accepted")==true&&conservation.value("ready")==true&&conservation.value("committed_offset").toInteger(-1)==Source.size(),"stopped boundary pays full terminal bytes, not transport success alone");
    check(f.finished==2&&f.count("stream")==2,"one suffix read, no blind retry or duplicate client");
    check(packet.value("snapshot").toObject().value("resume_lineage").toObject().value("ready")==false,"missing booking lineage still holds; suffix read grants no mining authority");
    QFile raw(f.temp.filePath(Run+".stdout"));check(raw.open(QIODevice::ReadOnly)&&raw.readAll()==Source,"original prefix plus actual suffix retained exactly once");raw.close();
    check(inspect(923,924)&&waitFor([&]{return f.byType(type).value("inventory_id")==id(924);}),"next independent inventory still pays fresh proof");
    check(f.count("stream")==2,"already conserved EOF does not restart SOURCE");
}
void coldStoppedObservationCannotContinueSource(){
    Fixture f("source-complete-late-footer",true);
    check(f.inventory->inspectStart(id(926),id(927)),"cold stopped observation enters ordinary initial acquisition");
    check(waitFor([&]{return f.byType("application_start_inventory").value("inventory_id")==id(927);}),"cold observation does not wait for a nonexistent admitted reader");
    check(f.count("stream")==0&&!QFileInfo::exists(f.config.source.historyDatabase),"source-boundary callback cannot create a cold continuation or storage");
    check(f.byType("application_start_inventory").value("source_conservation").toObject().value("ready")!=true,"cold observation grants no raw conservation");
}
void failedSourceThenGenuineEof(){
    Fixture f("source-complete-stream-fail");
    auto packet=constant();packet["binding"]=QJsonObject{{"uuid",Constant},{"start","100000"},{"end","110000"}};
    packet["manifest"]=QJsonObject{{"schema","pgn-native-process-1"},{"uuid",Constant},{"run",Run},{"pid",1234},{"birth",Time},{"start","100000"},{"end","110000"},{"stdout","C:/PUBLIC-TESTONLY/"+Run+".stdout"}};
    packet["next_offset"]=qint64(Source.size());packet["chunk"]=QString::fromLatin1(Source.toBase64());
    {
        SourceHistory history(f.config.source.historyDatabase);SourceMirror mirror(f.config.source.mirrorDirectory);NativeObservationSession seed(history,mirror,Constant);
        const auto frame=decodeSshSnapshot(QJsonDocument(packet).toJson(QJsonDocument::Compact));
        check(seed.consume(frame,Run,0,false,true).state==NativeObservationState::Source,"seed real enrolled full-prefix SQLite/mirror, not fixture counters");
        check(seed.conservation(frame,Run).ready,"seed bytes genuinely conserved before losing session objects");
    }
    f.expectedSourceStatus=NativeSourceAttemptStatus::Failed;
    check(f.inventory->source().startSource(id(180),Run),"failed read-only SOURCE admitted with known existing cursor");
    check(waitFor([&]{return f.finished==1;}),"own SELF stream exit255 observed");
    check(f.lastAttempt&&f.lastAttempt->transport.exitCode==255&&f.lastAttempt->transport.completeFrames==0&&f.publications==0,"failure has no source publication/admitted prefix in new session");
    f.freeze(181);check(f.inventory->inspectClose(id(181),id(182)),"fresh close observation after failed SOURCE admitted");
    check(waitFor([&]{return f.byType("application_close_inventory").value("inventory_id")==id(182);}),"failed-SOURCE close envelope settles");
    const auto unknown=f.byType("application_close_inventory");const auto held=unknown.value("source_conservation").toObject();
    check(unknown.value("accepted")==true&&held.value("ready")==false&&held.value("run")==Run,"worker run is only a selected verification parameter: failed stream still cannot supply conservation");
    std::cout<<"EOF_BEFORE_REPAIR accepted="<<unknown.value("accepted").toBool()<<" sourceRunNull="<<held.value("run").isNull()<<" error="<<held.value("error").toString().toStdString()<<'\n';

    // An explicit SECOND source read, not a control retry and not inventory
    // ingestion. It receives the real empty EOF frame and pays normal attach.
    qputenv("PGN_APP_INVENTORY_MODE","source-complete");f.expectedSourceStatus=NativeSourceAttemptStatus::Complete;
    check(f.inventory->source().startSource(id(183),Run),"explicit same-run SOURCE-only retry uses existing normal stream API");
    check(waitFor([&]{return f.finished==2&&f.publications==1;}),"genuine empty EOF frame committed/published after normal prefix verification");
    check(f.lastAttempt&&f.lastAttempt->nextOffset==Source.size()&&f.lastAttempt->committedOffset==Source.size()&&f.lastAttempt->transport.completeFrames==1,"real EOF frame has exact old full-prefix offsets, not new bytes");
    check(f.inventory->inspectClose(id(181),id(184)),"NEW fresh inventory after SOURCE proof, never reuse the old frame");
    check(waitFor([&]{return f.byType("application_close_inventory").value("inventory_id")==id(184);}),"post-repair fresh close envelope settles");
    const auto current=f.byType("application_close_inventory"),conserved=current.value("source_conservation").toObject();
    check(current.value("accepted")==true&&conserved.value("ready")==true&&conserved.value("run")==Run&&conserved.value("source")=="native/"+Run+".stdout","standard SOURCE repair yields the exact genuinely conserved source tuple");
    check(conserved.value("required_end").toInteger(-1)==Source.size()&&conserved.value("committed_offset").toInteger(-1)==Source.size()&&conserved.value("mirror_size").toInteger(-1)==Source.size()&&conserved.value("transport_offset").toInteger(-1)==Source.size(),"all four actual EOF frontiers agree");
    QFile raw(f.temp.filePath(Run+".stdout"));check(raw.open(QIODevice::ReadOnly)&&raw.readAll()==Source,"repair preserved old raw bytes exactly");
    check(current.value("snapshot").toObject().value("resume_lineage").toObject().value("ready")==false,"conservation repair does not invent missing booking lineage or CloseReady");
    std::cout<<"EOF_AFTER_REPAIR ready="<<conserved.value("ready").toBool()<<" bytes="<<Source.size()<<" freshInventory="<<current.value("inventory_id").toString().toStdString()<<'\n';
}
void mutate(const QString& path,const char* sql,const QStringList& args){
    sqlite3* db{};need(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"own booking fixture open");sqlite3_stmt* q{};
    need(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK,"own booking fixture SQL");
    for(int i=0;i<args.size();++i){const auto bytes=args[i].toUtf8();sqlite3_bind_text(q,i+1,bytes.constData(),bytes.size(),SQLITE_TRANSIENT);}
    const auto code=sqlite3_step(q);sqlite3_finalize(q);sqlite3_close(db);need(code==SQLITE_DONE,"PUBLIC TEST ONLY metadata, no physical ACK");
}
void publicationDuringEmptyExpectedRead(){
    QTemporaryDir temp;need(temp.isValid(),"own race directory");const auto key=temp.filePath("PUBLIC-TESTONLY.key");QFile file(key);need(file.open(QIODevice::WriteOnly)&&file.write("PUBLIC-TESTONLY")>0,"own fake key");file.close();
    qputenv("PGN_APP_INVENTORY_MODE","source-complete-race");qputenv("PGN_APP_INVENTORY_MARKER",temp.filePath("marker.txt").toUtf8());qunsetenv("PGN_APP_INVENTORY_BOOKING");
    NativeSourceRuntimeConfig config{{QCoreApplication::applicationFilePath(),key,"PUBLIC-TESTONLY.invalid","fixture",22},Constant,temp.filePath("history.sqlite"),temp.path(),10000,20000,20000};
    int publications{},finished{};bool stopped{};std::optional<NativeSourceInventory> observed;
    NativeSourceRuntimeCallbacks callbacks;callbacks.publication=[&](const auto&){++publications;};callbacks.sourceFinished=[&](const auto& value){++finished;check(value.status==NativeSourceAttemptStatus::Complete,"actual normal stream completes");};callbacks.inventory=[&](const auto& value){observed=value;};callbacks.stopped=[&]{stopped=true;};
    NativeSourceRuntime runtime(config,std::move(callbacks));check(runtime.startSource(id(700),Run),"start genuine SOURCE, not manifest admission");
    check(runtime.inspectInventory(id(701),{}),"capture empty expected BEFORE any source publication");check(publications==0,"read really launched before publication, not relabeled afterward");
    check(waitFor([&]{return observed.has_value()&&finished==1;}),"separate snapshot and normal SOURCE complete");
    check(publications==1&&observed->sourceEpoch==1&&observed->transport.status==SshObservationStatus::Complete,"real publication occurred during the originally empty-selected acquisition");
    check(observed->conservation.value("ready")==true&&observed->conservation.value("run")==Run&&observed->conservation.value("source")=="native/"+Run+".stdout","actual admitted worker/session verifies empty selection, never creates it from snapshot");
    observed.reset();check(runtime.inspectInventory(id(702),QString(32,'b')),"explicit wrong nonempty expected remains a valid negative request");check(waitFor([&]{return observed.has_value();}),"wrong expected settles");
    check(observed->conservation.value("ready")!=true,"worker run never overrides wrong nonempty expected");
    QFile raw(temp.filePath(Run+".stdout"));check(raw.open(QIODevice::ReadOnly)&&raw.readAll()==Source,"race cannot change exact RAW corpus");raw.close();
    runtime.stop();check(waitFor([&]{return stopped;}),"all own worker/read handles released");
}
void initialTicketOwnerLoss(){
    Fixture f;f.freeze(900);bool deleted{},next{};f.initialAction=[&](const auto& value){check(value.closeId==id(900)&&value.inventoryId==id(901)&&value.epoch>0&&value.beganMonotonicMs>0,"callback carries real acquisition key and conservative origin");f.inventory.reset();deleted=true;};
    check(f.inventory->inspectClose(id(900),id(901)),"Initial owner-loss acquisition admitted");check(waitFor([&]{return deleted;}),"consumer may destroy Initial owner without a false packet");
    check(f.writer.enqueue(NativeApplicationOperation::Read,[&](auto done){next=true;done({});},[](const auto&){} )&&waitFor([&]{return next;}),"new callback owner loss settles external ONE writer");check(f.byType("application_close_inventory").isEmpty(),"owner loss never publishes a close permission");
}
void firstCloseBeforeSourceSettled(bool retry=false){
    Fixture f("source-complete-two-stage");const auto release=f.temp.filePath("release-source");qputenv("PGN_APP_INVENTORY_RELEASE",release.toUtf8());
    auto releaseSource=[&]{QFile marker(release);need(marker.open(QIODevice::WriteOnly)&&marker.write("OWN-TEST-RELEASE")>0&&marker.flush(),"own test synchronization only");};
    check(f.inventory->source().startSource(id(800),Run),"normal Stream starts before immediate close");check(waitFor([&]{return f.publications==1;}),"first genuine partial-prefix publication arrives; Stream remains active");
    f.phaseAction=[&](const auto& phase){if(phase.value("inventory_id")==id(802)&&phase.value("phase")=="lineage"&&phase.value("active")==true){releaseSource();check(waitFor([&]{return f.publications==2;}),"baseline genuine SOURCE progresses while initial lineage phase is entered");}};
    f.freeze(801);check(f.inventory->inspectClose(id(801),id(802)),"first pre-STOP physical close inventory admitted");check(waitFor([&]{return f.byType("application_close_inventory").value("inventory_id")==id(802);}),"first inventory settles without assuming SOURCE finished");
    const auto first=f.byType("application_close_inventory");int initialLineage{};for(const auto& phase:f.phases)if(phase.value("inventory_id")==id(802)&&phase.value("phase")=="lineage"&&phase.value("active")==true)++initialLineage;
    std::cout<<"INITIAL_CLOSE accepted="<<first.value("accepted").toBool()<<" lineage="<<initialLineage<<" error="<<first.value("error").toString().toStdString()<<'\n';
    check(first.value("accepted")==true&&initialLineage==0&&first.value("finite_device").toObject().value("status")=="observed","Initial obtains real physical+finite state without pre-STOP lineage/refresh");
    check(!first.value("snapshot").toObject().contains("resume_lineage"),"Initial cannot masquerade as final lineage evidence");
    if(!QFileInfo::exists(release))releaseSource();check(waitFor([&]{return f.publications==2&&f.finished==1;}),"normal SOURCE settles from real second frame before final read");
    check(f.inventory->inspectClose(id(801),id(803)),"separate final fresh inventory admitted after SOURCE settles");check(waitFor([&]{return f.byType("application_close_inventory").value("inventory_id")==id(803);}),"Final normal lineage plus post-lineage snapshot settles");
    const auto final=f.byType("application_close_inventory");int finalLineage{};for(const auto& phase:f.phases)if(phase.value("inventory_id")==id(803)&&phase.value("phase")=="lineage"&&phase.value("active")==true)++finalLineage;
    check(final.value("accepted")==true&&finalLineage==1&&final.value("source_conservation").toObject().value("ready")==true,"Final still pays genuine lineage and conservation; no guard weakened");
    check(final.value("snapshot").toObject().value("resume_lineage").toObject().value("ready")==false,"missing booking lineage stays Held: physical accepted is never CloseReady");
    QFile raw(f.temp.filePath(Run+".stdout"));check(raw.open(QIODevice::ReadOnly)&&raw.readAll()==Source&&f.count("stream")==1,"all RAW retained exactly once; no restart or control");
    if(retry){
        int frozen{};for(const auto& packet:f.packets)if(packet.value("type")=="application_close_frozen")++frozen;
        const auto previous=frozen;f.additional=[&](const auto& packet){if(packet.value("type")=="application_close_frozen"&&++frozen==previous+1){check(packet.value("accepted")==true,"retry freeze actually accepted");check(f.inventory->inspectClose(id(801),id(804)),"retry Initial can enter directly from frozen callback");}};
        check(f.inventory->prepareClose(id(801)),"same persistent closeId retry admitted after logical missing-proof Held");
        check(waitFor([&]{return f.byType("application_close_inventory").value("inventory_id")==id(804);}),"retry Initial completes");f.additional={};
        const auto initial=f.byType("application_close_inventory");int retryLineage{};for(const auto& phase:f.phases)if(phase.value("inventory_id")==id(804)&&phase.value("phase")=="lineage"&&phase.value("active")==true)++retryLineage;
        check(initial.value("accepted")==true&&retryLineage==0&&!initial.value("snapshot").toObject().contains("resume_lineage"),"accepted freeze rearms Initial BEFORE publishing its callback, no final proof reused");
        check(f.inventory->inspectClose(id(801),id(805))&&waitFor([&]{return f.byType("application_close_inventory").value("inventory_id")==id(805);}),"retry Final still independently acquired");
        int finalPhases{};for(const auto& phase:f.phases)if(phase.value("inventory_id")==id(805)&&phase.value("phase")=="lineage"&&phase.value("active")==true)++finalPhases;
        check(finalPhases==1&&f.byType("application_close_inventory").value("snapshot").toObject().value("resume_lineage").toObject().value("ready")==false,"retry Final retains real lineage and missing-proof Held, not CloseReady");
        check(!f.inventory->prepareClose(id(899)),"contradictory freeze is refused, not another Initial grant");
        check(f.inventory->inspectClose(id(801),id(806))&&waitFor([&]{return f.byType("application_close_inventory").value("inventory_id")==id(806);}),"after refused freeze normal Final remains readable");
        int deniedPhases{};for(const auto& phase:f.phases)if(phase.value("inventory_id")==id(806)&&phase.value("phase")=="lineage"&&phase.value("active")==true)++deniedPhases;
        check(deniedPhases==1&&f.count("stream")==1,"refused freeze does not rearm phase or restart SOURCE");
    }
}
void refreshedLineage(){
    using namespace pgn::booking;
    for(const auto mode:{"source-complete-valid","source-complete-identity-drift","source-complete-receipt-drift","source-complete-bytes-drift","source-complete-stop","source-complete-guard-fail","source-complete-cancel"}){
        Fixture f(mode);BookingStore store(f.config.bookingDatabase);SourceRegistry registry;
        registry.url="https://PUBLIC-TESTONLY.invalid/source";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString(Time,Qt::ISODate);
        registry.ranges={{1,"100000","110000","Open",{}}};check(store.valid()&&store.queue(registry,"100000","110000",Constant).isEmpty(),"own manual booking fixture");
        const auto prepared=store.prepareInitialStart(id(60),Constant,{"100000","110000","100000","110000","100000","10000"},registry,{WorkBoundary::Ready,id(61)});
        need(prepared.status==InitialStartStatus::Prepared&&prepared.dispatch.has_value(),"own real admission fixture");const auto booking=prepared.dispatch->bookingId;
        qputenv("PGN_APP_INVENTORY_BOOKING",booking.toUtf8());
        check(f.inventory->source().startSource(id(62),Run)&&waitFor([&]{return f.finished==1&&f.publications>0;}),"actual same-worker fixture bytes conserved");
        mutate(f.config.bookingDatabase,"UPDATE dispatch_requests SET state='STARTED',run=? WHERE id=?",{Run,booking});
        mutate(f.config.bookingDatabase,"UPDATE bookings SET state='DISPATCHED' WHERE id=?",{booking});
        check(store.observeRuntime(Run,"100000","110000",Constant,false).isEmpty()&&store.saveSourceRunManifest(Constant,f.lastSource.value("manifest").toObject()).isEmpty(),"PUBLIC TEST ONLY process metadata is not physical ownership");
        std::function<void(QString)> profileDone;
        const bool delayed=QByteArray(mode)=="source-complete-valid",denied=QByteArray(mode)=="source-complete-guard-fail",cancel=QByteArray(mode)=="source-complete-cancel";
        bool cancelled{};
        if(cancel)f.phaseAction=[&](const auto& phase){
            if(phase.value("phase")=="lineage"&&phase.value("active")==true)
                QMetaObject::invokeMethod(f.inventory.get(),[&]{cancelled=f.inventory->cancelLineage();},Qt::QueuedConnection);
        };
        if(delayed)f.refreshAction=[&](auto done){profileDone=std::move(done);};
        if(denied)f.refreshAction=[](auto done){done("PUBLIC-TESTONLY profile qualification denied");};
        check(f.inventory->inspectBoundary(id(63),id(64)),"dedicated continuation acquisition admitted");
        if(delayed){
            check(waitFor([&]{return bool(profileDone);}),"profile hook reached AFTER actual held-Win32 lineage scan");
            check(f.count("constant")==1&&f.boundaries==0,"profile work does not relabel old snapshot as fresh");
            profileDone({});profileDone("late duplicate error must not cancel the new acquisition");
        }
        check(waitFor([&]{return f.byType("native_work_boundary_inventory").value("inventory_id")==id(64);}),"post-proof actual reacquisition settled");
        const bool drift=QByteArray(mode).contains("drift")||denied||cancel;const auto envelope=f.byType("native_work_boundary_inventory");
        check(envelope.value("accepted")==!drift,"changed proof input fails closed, no stale proof reuse");
        check(f.count("constant")==int(denied||cancel?1:2)&&f.count("stream")==1&&f.count("finite")==0,"only actual final acquisition can follow successful profile hook");
        check(f.refreshes==int(!cancel),"read-only qualification hook runs once after uncancelled lineage");
        if(cancel){
            check(cancelled&&envelope.value("error").toString().contains("cancelled"),"individual lineage cancellation settles Held, no proof refresh");
            bool entered{};f.writer.enqueue(NativeApplicationOperation::Read,[&](auto done){entered=true;done({});},[](const auto&){});
            check(waitFor([&]{return entered;}),"cancelled lineage releases ONE writer for operator command");
        }
        QSet<QString> begun;QString active;
        for(const auto& phase:f.phases){
            check(phase.size()==7&&phase.value("type")=="application_inventory_phase"&&phase.value("request_id")==id(63)&&phase.value("inventory_id")==id(64)&&phase.value("purpose")=="boundary","phase exact schema and acquisition correlation");
            const auto token=phase.value("phase_id").toString();
            if(phase.value("active")==true){check(active.isEmpty()&&!begun.contains(token)&&!QUuid::fromString(token).isNull(),"real phase begins once, no overlap or replay");begun.insert(token);active=token;}
            else{check(active==token,"phase completion pairs with the actually started operation");active.clear();}
        }
        check(active.isEmpty(),"no suspended UI phase after terminal inventory");
        check(f.fresh==0&&f.byType("application_start_inventory").isEmpty(),"automatic boundary cannot hijack operator start ticket");
        check(f.boundaries==int(!drift),"no automatic continuation callback on changed boundary");
        if(!drift){
            const auto proof=f.boundarySnapshot.value("resume_lineage").toObject();
            if(proof.value("ready")!=true)std::cerr<<"LINEAGE_FIXTURE "<<QJsonDocument(proof).toJson(QJsonDocument::Compact).constData()<<'\n';
            check(proof.value("ready")==true,"real held-Win32 verifier positive lineage retained");
            check(f.boundarySnapshot.value("observed_utc")==FreshTime,"final actual timestamp, not relabelled first observation");
            check(f.boundarySnapshot.value("operator_stop")==bool(QByteArray(mode)=="source-complete-stop"),"newly observed operator STOP preserved as advance veto");
        }
    }
    qunsetenv("PGN_APP_INVENTORY_BOOKING");
}
}
void sourceRepairIdleProgress(){
    for(const char* mode:{"source-complete-progress","source-complete-stall"}){
        Fixture f(mode,true);const auto prefix=Source.left(Source.indexOf('\n')+1);auto packet=constant();
        packet["binding"]=QJsonObject{{"uuid",Constant},{"start","100000"},{"end","110000"}};
        packet["manifest"]=QJsonObject{{"schema","pgn-native-process-1"},{"uuid",Constant},{"run",Run},{"pid",1234},{"birth",Time},{"start","100000"},{"end","110000"},{"stdout","C:/PUBLIC-TESTONLY/"+Run+".stdout"}};
        packet["last_exit"]=QJsonObject{{"run",Run},{"exit_code",0}};packet["next_offset"]=qint64(prefix.size());packet["pending_bytes"]=qint64(Source.size()-prefix.size());packet["chunk"]=QString::fromLatin1(prefix.toBase64());
        {SourceHistory history(f.config.source.historyDatabase);SourceMirror mirror(f.config.source.mirrorDirectory);NativeObservationSession session(history,mirror,Constant);const auto frame=decodeSshSnapshot(QJsonDocument(packet).toJson(QJsonDocument::Compact));check(session.consume(frame,Run,0,false,true).state==NativeObservationState::Source,"own strict committed prefix for idle deadline");}
        // Initial close still prioritizes STOP; only the distinct final read
        // may pay the SOURCE remainder before granting conservation.
        f.freeze(970);check(f.inventory->inspectClose(id(970),id(971)),"initial known close inventory");
        check(waitFor([&]{return f.byType("application_close_inventory").value("inventory_id")==id(971);}),"initial close settles without source repair");
        QElapsedTimer total;total.start();check(f.inventory->inspectClose(id(970),id(972)),"final genuine SOURCE repair");
        check(waitFor([&]{return f.byType("application_close_inventory").value("inventory_id")==id(972);},8000),"bounded idle-progress final close settles");
        const auto result=f.byType("application_close_inventory");const bool progressing=QByteArray(mode).endsWith("progress");
        if(progressing){
            check(total.elapsed()>2000&&result.value("accepted")==true&&result.value("source_conservation").toObject().value("ready")==true,"real committed-byte progress outlives total timeout while exact final conservation is still required");
            QFile raw(f.temp.filePath(Run+".stdout"));check(raw.open(QIODevice::ReadOnly)&&raw.readAll()==Source,"slow progressive repair retains exact full RAW");
        }else{
            check(result.value("accepted")==false&&result.value("error").toString().contains("wait expired"),"empty duplicate heartbeats do not renew committed-byte idle deadline");
            check(waitFor([&]{return f.finished==1;}),"incomplete SELF reader exits before fixture cleanup; transport EOF is not conservation");
            check(f.byType("application_close_inventory").value("accepted")==false,"late incomplete reader cannot resurrect expired close inventory");
        }
        check(std::any_of(f.phases.begin(),f.phases.end(),[](const auto& p){return p.value("phase")=="source_read"&&p.value("active")==true;}),"typed SOURCE-read phase exposed, never a fake lineage phase");
    }
}
void driverlessInactiveClose(){
    for(const auto mode:{"driver-false-idle","driver-false-owned","driver-unknown-idle"})for(const auto purpose:{"start","boundary","close"}){
        Fixture f(mode);const bool close=QByteArray(purpose)=="close";
        if(close)f.freeze(901);
        const bool entered=close?f.inventory->inspectClose(id(901),id(902)):
            QByteArray(purpose)=="start"?f.inventory->inspectStart(id(901),id(902)):f.inventory->inspectBoundary(id(901),id(902));
        check(entered,"own driver-purpose inventory read entered");const char* type=close?"application_close_inventory":QByteArray(purpose)=="start"?"application_start_inventory":"native_work_boundary_inventory";
        check(waitFor([&]{return !f.byType(type).isEmpty();}),"own driver-purpose inventory has terminal outcome");
        const auto result=f.byType(type);const bool permitted=close&&QByteArray(mode)=="driver-false-idle";
        check(result.value("accepted")==permitted,"ONLY closing explicit OFF/unowned is driver-independent; start/boundary/active/unknown remain refused");
        check(f.fresh==0,"driver-free close never grants start admission callback");
    }
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    if(app.arguments().size()>1&&app.arguments().at(1)=="-F")return fake(app.arguments());
    if(app.arguments().contains("--driverless-close"))driverlessInactiveClose();
    else if(app.arguments().contains("--stopped-source-suffix")){stoppedSuccessfulReadNeedsSuffix(false);stoppedSuccessfulReadNeedsSuffix(true);coldStoppedObservationCannotContinueSource();}
    else if(app.arguments().contains("--source-progress-idle"))sourceRepairIdleProgress();
    else if(app.arguments().contains("--initial-ticket-owner-loss"))initialTicketOwnerLoss();
    else if(app.arguments().contains("--close-retry-initial"))firstCloseBeforeSourceSettled(true);
    else if(app.arguments().contains("--initial-close-source-active"))firstCloseBeforeSourceSettled();
    else if(app.arguments().contains("--source-publication-race"))publicationDuringEmptyExpectedRead();
    else if(app.arguments().contains("--failed-source-eof"))failedSourceThenGenuineEof();
    else if(app.arguments().contains("--epoch-zero-admission")){neverAttachedLineage();conservedSource();refreshedLineage();}
    else{initialAndBridge();closeCases();conservedSource();refreshedLineage();admissionAndLifetime();stoppedSuccessfulReadNeedsSuffix(false);stoppedSuccessfulReadNeedsSuffix(true);}
}catch(const std::exception& e){++failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
std::cout<<"NATIVE_APPLICATION_INVENTORY "<<checks<<" checks / "<<failures<<" failures\n";return failures?1:0;}

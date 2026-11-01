#include "mining/native_application_runtime.h"
#include "mining/native_bridge.h"
#include "mining/native_observation_session.h"
#include "booking/booking_store.h"
#include "booking/decimal_range.h"
#include <QCryptographicHash>
#include <sqlite3.h>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <cstdio>
#include <stdexcept>
using namespace pgn::mining;
namespace {
int checks{},failures{};
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<why<<'\n';}}
QString id(int n){return "70000000-0000-4000-8000-"+QString::number(n,16).rightJustified(12,'0');}
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",Time="2026-10-15T00:00:00.1234567Z";
const QString OwnedRun(32,'e');
const QString QualifiedSdk="C:/PGN/native/20261012-gpu-owned-source-v1",ZoneStart="199900000000000000000",ZoneEnd="200000000000000000000",Width="1000000000000000";
const QString EngineSha="A73A6E383308560EDC36CE351B09EB449FA099275361BE74BE89020B03864B43",QualificationSha="99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684";
// ONLY public synthetic declarations in own SELF argv/JSON. No SDK/engine/
// runtime library is opened or hashed; this tests typed routing, not hardware.
QJsonObject qualifiedCapacity(){return {{"schema","pgn-qualified-source-capacity-1"},{"policy","GPU_OWNED_HIGH10_SOURCE_V1"},{"sdk",QualifiedSdk},{"scope_start","184467440737095516160"},{"scope_end","202914184810805067776"},{"max_length",Width},{"guard","Assert-PgnQualifiedSourceRange"},{"qualification_sha256",QualificationSha}};}
QByteArray ownBindingBytes(){
    QFile file(qEnvironmentVariable("PGN_RUNTIME_TEST_BINDING"));need(file.open(QIODevice::ReadOnly)&&file.size()>0&&file.size()<=32768,"own bounded PUBLIC_TEST_ONLY binding");return file.readAll();
}
QJsonObject qualifiedProfile(const QByteArray& bytes,bool missingCapacity){
    const auto binding=QJsonDocument::fromJson(bytes).object();
    return {{"schema","pgn-configured-profile-observation-1"},{"protocol","pgn-configured-stop-1"},{"status","CONFIGURED_PROFILE_OBSERVED"},
        {"profile",QJsonObject{{"device",Device},{"executable",QualifiedSdk+"/bin/pgn-continuous-gpu.exe"},{"image_sha256",EngineSha},{"binding_sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toUpper())}}},
        {"binding",binding},{"source_capacity",missingCapacity?QJsonValue(QJsonValue::Null):QJsonValue(qualifiedCapacity())}};
}
const QByteArray Source="[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=2 original_start=100000 length=10000 proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n[pgn-counters] time_ns=100 positions=10000 gaps=0 tests=123 batches=1 processed_gaps=0\n[phase0-gpu] gaps reported=0 verification_failures=0 records_new=0\n[phase0-gpu] --- end ---\n";
template<class F>bool waitFor(F f,int ms=20000){QElapsedTimer time;time.start();while(!f()&&time.elapsed()<ms){QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}return f();}
bool ownDriverReady(){
    const auto path=qEnvironmentVariable("PGN_RUNTIME_TEST_DRIVER_STATE");if(path.isEmpty())return true;
    QFile state(path);need(state.open(QIODevice::ReadOnly),"Own driver state read");return state.readAll()=="up";
}
int fake(const QStringList& args){
    const auto wide=QByteArray::fromBase64(args.last().section(' ',-1).toLatin1());
    const auto script=QString::fromUtf16(reinterpret_cast<const char16_t*>(wide.constData()),wide.size()/2);
    if(script.contains("pgn-native-hardware-observation-1")){
        need(!script.contains("native-control.ps1")&&!script.contains("-Action "),"Hardware read has no process/control/source inventory");
        const bool ready=ownDriverReady();
        const auto bytes=QJsonDocument(QJsonObject{{"schema","pgn-native-hardware-observation-1"},{"driver_valid",ready},
            {"observed_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"gpus",ready?QJsonArray{"0, GPU-closed-fixture, CLOSED HARDWARE FIXTURE, bus, 0, 0, 100, 1, 2, 30, 100"}:QJsonArray{}}}).toJson(QJsonDocument::Compact);
        need(std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout)==size_t(bytes.size())&&std::fflush(stdout)==0,"Own hardware-only fixture output");return 0;
    }
    const bool finite=script.contains("test-3060/native-test-control.ps1");
    const bool profile=script.contains("-Action 'configured-profile'")||script.contains("-Action configured-profile");
    if(profile&&qEnvironmentVariableIsSet("PGN_RUNTIME_TEST_HARDWARE_DURING_PROFILE"))QThread::msleep(350);
    const bool stream=script.contains("-Action stream");
    const auto sourceMode=qEnvironmentVariable("PGN_RUNTIME_TEST_SOURCE_MODE");
    const auto profileMode=qEnvironmentVariable("PGN_RUNTIME_TEST_PROFILE_MODE");
    const bool configuredLatch=profileMode=="latch-unqualified"&&script.contains("-Action stop-configured");
    const bool readOnly=script.contains("-Action snapshot")||script.contains("-Action 'snapshot'")||stream||profile;
    need(std::fgetc(stdin)==EOF,"Own SSH fixture null stdin");
    QFile marker(qEnvironmentVariable("PGN_RUNTIME_TEST_MARKER"));need(marker.open(QIODevice::WriteOnly|QIODevice::Append),"Own marker");
    need(marker.write(configuredLatch?"configured-latch\n":!readOnly?"FORBIDDEN_CONTROL\n":profile?"profile\n":finite?"finite\n":stream?"stream\n":"constant\n")>0&&marker.flush(),"Own marker write");marker.close();
    need(readOnly||configuredLatch,"Own fixture forbids every start/launch action, including positive routing");
    if(finite&&sourceMode=="finite-wait")QThread::msleep(15000);
    if(configuredLatch){
        const auto request=QRegularExpression("-RequestId '([^']+)'").match(script),reason=QRegularExpression("-ConfiguredReason '(AppClose|CardOff)'").match(script);need(request.hasMatch()&&reason.hasMatch(),"own exact latch-only correlation");
        const auto response=QJsonObject{{"protocol","pgn-configured-stop-1"},{"status","CONFIGURED_OFF_LATCHED"},{"request_id",request.captured(1)},{"reason",reason.captured(1)},{"profile",qualifiedProfile(ownBindingBytes(),true)["profile"]},{"physical_applied",false}};
        const auto raw=QJsonDocument(response).toJson(QJsonDocument::Compact);need(std::fwrite(raw.constData(),1,size_t(raw.size()),stdout)==size_t(raw.size())&&std::fflush(stdout)==0,"own latch report, not a process ACK");return 0;
    }
    if(profile){
        const auto bytes=profileMode.isEmpty()?QByteArray("{}"):QJsonDocument(qualifiedProfile(ownBindingBytes(),profileMode=="capacity-null"||profileMode=="latch-unqualified")).toJson(QJsonDocument::Compact);
        need(std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout)==size_t(bytes.size())&&std::fflush(stdout)==0,"Typed profile response or explicit unsupported fixture");return 0;
    }
    if(!finite&&qEnvironmentVariable("PGN_RUNTIME_TEST_CONSTANT_MODE")=="exit-failure")return 7;
    QJsonObject packet;
    if(finite)packet={{"schema","pgn-device-control-observation-1"},{"device",QString::fromLatin1(Finite3060Device)},{"scope","BOUNDED_SCIENCE_TEST"},{"observed_utc",Time},
        {"owned",false},{"desired_enabled",false},{"stop_requested",false},{"manifest",QJsonValue::Null},{"last_exit",QJsonValue::Null},
        {"last_error",QJsonValue::Null},{"source_credit",false},{"auto_advance",false}};
    else packet={{"schema","pgn-runtime-snapshot-1"},{"observed_utc",Time},{"owned",false},{"driver_valid",ownDriverReady()},{"state","OFF"},{"operator_stop",true},{"has_checkpoint",false},
        {"offset",0},{"next_offset",0},{"pending_bytes",0},{"chunk",""},{"manifest",QJsonValue::Null},{"last_exit",QJsonValue::Null},
        {"binding",QJsonObject{{"uuid",Device},{"start","100000"},{"end","110000"}}}};
    if(!finite&&!profileMode.isEmpty()){
        auto binding=QJsonDocument::fromJson(ownBindingBytes()).object();
        if(profileMode=="binding-changed"){auto extension=binding["unknown_extension"].toObject();extension["original"]="CHANGED AFTER PROFILE READ";binding["unknown_extension"]=extension;}
        packet["binding"]=binding;
        if(profileMode=="latch-unqualified"){QFile trace(qEnvironmentVariable("PGN_RUNTIME_TEST_MARKER"));need(trace.open(QIODevice::ReadOnly),"own latch state read");packet["operator_stop"]=trace.readAll().contains("configured-latch\n");}
    }
    if(!finite&&qEnvironmentVariable("PGN_RUNTIME_TEST_CONSTANT_MODE")=="external-running"){
        packet["owned"]=true;packet["state"]="RUNNING";packet["operator_stop"]=false;
        packet["manifest"]=QJsonObject{{"schema","pgn-native-process-1"},{"uuid",Device},{"run",OwnedRun},{"pid",4242},{"birth",Time},{"start","100000"},{"end","110000"},{"stdout","C:/PUBLIC-TESTONLY/"+OwnedRun+".stdout"}};
    }
    const auto booking=qEnvironmentVariable("PGN_RUNTIME_TEST_BOOKING");
    if(!finite&&!booking.isEmpty()){
        packet["state"]="RUNNING";packet["owned"]=true;packet["operator_stop"]=false;
        packet["manifest"]=QJsonObject{{"schema","pgn-native-process-1"},{"run",OwnedRun},{"uuid",Device},{"pid",4242},{"birth",Time},{"start","100000"},{"end","110000"},{"queued_intent",booking},{"stdout","C:/PUBLIC-TESTONLY/"+OwnedRun+".stdout"}};
        packet["binding"]=QJsonObject{{"uuid",Device},{"start","100000"},{"end","110000"},{"length","10000"},{"queued_intent",booking},
            {"initial_start",QJsonObject{{"origin","INITIAL_START"},{"request_id",id(20)}}},{"test_scope",QJsonObject{{"enabled",true},{"order","ascending"},{"start","100000"},{"end","110000"}}}};
        bool successfulReadEnded=false;
        if((sourceMode=="complete-no-terminal"||sourceMode=="complete-driver-outage"||sourceMode.startsWith("complete-late-footer"))&&!stream){QFile history(qEnvironmentVariable("PGN_RUNTIME_TEST_MARKER"));need(history.open(QIODevice::ReadOnly),"own read-completion marker");successfulReadEnded=history.readAll().contains("stream\n");}
        if(successfulReadEnded||sourceMode=="off"||sourceMode=="fail-again"||sourceMode=="wrong-manifest"||sourceMode.startsWith("completed-")||(stream&&sourceMode=="active-wait")){
            packet["owned"]=false;packet["state"]="OFF";packet["operator_stop"]=true;packet["last_exit"]=QJsonObject{{"run",OwnedRun},{"exit_code",0}};
        }
        if(sourceMode.startsWith("completed-"))packet["source_completion_observation"]=QJsonObject{{"run",sourceMode=="completed-bad-run"?QString(32,'d'):OwnedRun},{"source_range_complete",true}};
        if(!sourceMode.isEmpty()){
            const auto match=QRegularExpression("-Offset ([0-9]+) -ExpectedRun '([0-9a-f]*)'").match(script);need(match.hasMatch(),"own exact SOURCE read arguments");
            const auto offset=match.captured(1).toLongLong();need(offset>=0&&offset<=Source.size(),"own source offset");
            if(stream&&(sourceMode=="fail-first"||sourceMode=="fail-again"))return 255;
            if(stream&&sourceMode=="active-wait")QThread::msleep(700);
            packet["offset"]=offset;packet["next_offset"]=qint64(Source.size());packet["chunk"]=QString::fromLatin1(Source.mid(offset).toBase64());
            if(stream&&sourceMode.startsWith("complete-late-footer")&&offset==0){
                const auto prefix=Source.indexOf("[phase0-gpu] gaps reported=");need(prefix>0,"own later terminal suffix");
                packet["next_offset"]=qint64(prefix);packet["chunk"]=QString::fromLatin1(Source.left(prefix).toBase64());
                const auto bytes=QJsonDocument(packet).toJson(QJsonDocument::Compact)+'\n';need(std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout)==size_t(bytes.size())&&std::fflush(stdout)==0,"own successful live prefix output");
                if(sourceMode.endsWith("-race")){
                    const auto release=qEnvironmentVariable("PGN_RUNTIME_TEST_SUFFIX_RELEASE");QElapsedTimer wait;wait.start();while(!QFileInfo::exists(release)&&wait.elapsed()<10000)QThread::msleep(1);
                    need(QFileInfo::exists(release),"test releases old successful live EOF after stopped inventory waits");
                }
                return 0;
            }
            if(sourceMode=="wrong-manifest"){auto m=packet["manifest"].toObject();m["pid"]=4243;packet["manifest"]=m;}
        }else if(stream){packet["next_offset"]=qint64(Source.size());packet["chunk"]=QString::fromLatin1(Source.toBase64());}
    }
    if(stream&&sourceMode=="complete-driver-outage"){
        QFile state(qEnvironmentVariable("PGN_RUNTIME_TEST_DRIVER_STATE"));need(state.open(QIODevice::WriteOnly|QIODevice::Truncate)&&state.write("down")==4&&state.flush(),"Own post-SOURCE driver outage");
    }
    const auto bytes=QJsonDocument(packet).toJson(QJsonDocument::Compact)+(stream?"\n":"");need(std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout)==size_t(bytes.size())&&std::fflush(stdout)==0,"Own snapshot output");return 0;
}
struct Fixture {
    QTemporaryDir temp;NativeBridge bridge;NativeApplicationRuntimeConfig config;
    QList<QJsonObject> packets,timings;QStringList statuses;int metrics{},controls{};bool stopped{},timingWrongThread{},indexWrongThread{};std::function<void(const QJsonObject&)> timingSink,packetSink;
    std::unique_ptr<NativeApplicationRuntime> runtime;
    Fixture(bool timingAudit=false){
        need(temp.isValid(),"Own directory");const auto key=temp.filePath("PUBLIC-TESTONLY.key");QFile f(key);need(f.open(QIODevice::WriteOnly)&&f.write("PUBLIC-TESTONLY")>0,"Own fake key");f.close();
        need(QDir().mkdir(temp.filePath("source-journals")),"Own mirror directory");
        config.transport.endpoint={QCoreApplication::applicationFilePath(),key,"PUBLIC-TESTONLY.invalid","fixture",22};
        config.transport.paths={temp.filePath("local-bookings.sqlite"),temp.filePath("scientific-history.sqlite"),temp.filePath("source-journals"),temp.filePath("engine-modules.sqlite"),"C:/PUBLIC-TESTONLY/checkpoint"};
        config.sourceRegistryDirectory=temp.path();config.controlTimeouts={10000,20000};config.timingAudit=timingAudit;
        qputenv("PGN_RUNTIME_TEST_MARKER",temp.filePath("marker.txt").toUtf8());qunsetenv("PGN_RUNTIME_TEST_BOOKING");
        qunsetenv("PGN_RUNTIME_TEST_PROFILE_MODE");qunsetenv("PGN_RUNTIME_TEST_BINDING");qunsetenv("PGN_RUNTIME_TEST_CONSTANT_MODE");
        qunsetenv("PGN_RUNTIME_TEST_SOURCE_MODE");qunsetenv("PGN_RUNTIME_TEST_DRIVER_STATE");
        bridge.onDevice=[this](const auto&){++metrics;};bridge.onControl=[this](const auto&,const auto&,bool,const auto&){++controls;};
        runtime=std::make_unique<NativeApplicationRuntime>(config,&bridge);
        runtime->onPacket=[this](const auto& p){if(p.value("type")=="native_index")indexWrongThread|=QThread::currentThread()!=QCoreApplication::instance()->thread();packets.append(p);if(packetSink)packetSink(p);};runtime->onStatus=[this](const auto& s){statuses.append(s);};runtime->onStopped=[this]{stopped=true;};
        runtime->onTiming=[this](const auto& p){timingWrongThread|=QThread::currentThread()!=QCoreApplication::instance()->thread();timings.append(p);if(timingSink)timingSink(p);};
    }
    ~Fixture(){if(runtime){runtime->stop();check(waitFor([this]{return stopped;}),"Actual observer worker stopped before temporary files disappear");runtime.reset();}}
    bool type(const char* type)const{for(const auto& packet:packets)if(packet.value("type")==type)return true;return false;}
    int count(const QByteArray& value)const{QFile f(temp.filePath("marker.txt"));if(!f.open(QIODevice::ReadOnly))return 0;return f.readAll().count(value+'\n');}
    void driver(bool ready){const auto path=temp.filePath("driver-state.txt");QFile state(path);const QByteArray bytes=ready?"up":"down";need(state.open(QIODevice::WriteOnly|QIODevice::Truncate)&&state.write(bytes)==bytes.size()&&state.flush(),"Own driver state write");qputenv("PGN_RUNTIME_TEST_DRIVER_STATE",path.toUtf8());}
    QJsonObject packet(const char* type,const QString& key,const QString& value)const{for(auto i=packets.crbegin();i!=packets.crend();++i)if(i->value("type")==type&&i->value(key)==value)return *i;return {};}
    pgn::booking::SourceRegistry configureProfile(const QByteArray& mode){
        // Zone1000 is CSV row1001 (the header counts in source provenance),
        // not row1000. The preceding 999 rows here are public tiny fixtures.
        // This is NOT a network fetch or an installed source qualification.
        QByteArray csv="Start *1e15,End *1e15,Status,Owner\n";
        for(int row=0;row<999;++row)csv+=QByteArray::number(row)+','+QByteArray::number(row+1)+",Complete,\n";
        csv+="199900,200000,Open,\n";
        QFile source(temp.filePath("community-source.csv"));need(source.open(QIODevice::WriteOnly)&&source.write(csv)==csv.size(),"own exact source registry cache");source.close();
        const auto end=pgn::booking::addUnsigned(ZoneStart,Width);
        QJsonObject binding{{"uuid",Device},{"executable",QualifiedSdk+"/bin/pgn-continuous-gpu.exe"},{"sdk",QualifiedSdk},
            {"pins",QJsonArray{QJsonObject{{"path",QualifiedSdk+"/bin/pgn-continuous-gpu.exe"},{"sha256",EngineSha}},QJsonObject{{"path",QualifiedSdk+"/bin/cudart64_12.dll"},{"sha256","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}}}},
            {"engine_upgrade",QJsonObject{{"policy","GPU_OWNED_HIGH10_SOURCE_V1"},{"qualification_sha256",QualificationSha}}},{"start",ZoneStart},{"end",end},{"length",Width},
            {"test_scope",QJsonObject{{"enabled",true},{"order","ascending"},{"start",ZoneStart},{"end",ZoneEnd}}},
            {"community",QJsonObject{{"source","https://docs.google.com/spreadsheets/d/1y0OWkPbf8dvg6cpNqGNawhIykAlAWAtI8vqUA4KPiQ8/PUBLIC_TEST_ONLY"},{"source_sha256",QString::fromLatin1(QCryptographicHash::hash(csv,QCryptographicHash::Sha256).toHex())},{"observed_utc",Time}}},
            {"unknown_extension",QJsonObject{{"original","PUBLIC_TEST_ONLY_SYNTHETIC_SDK_DECLARATIONS_NOT_INSTALLED_PROOF"}}}};
        auto scope=binding["test_scope"].toObject();
        if(mode=="qualified-legacy")scope.remove("enabled");
        if(mode=="scope-disabled")scope["enabled"]=false;
        if(mode=="scope-wrong-type")scope["enabled"]="true";
        binding["test_scope"]=scope;
        QFile cache(temp.filePath("binding.json"));const auto raw=QJsonDocument(binding).toJson(QJsonDocument::Compact);need(cache.open(QIODevice::WriteOnly)&&cache.write(raw)==raw.size(),"own actual binding DTO cache");cache.close();
        qputenv("PGN_RUNTIME_TEST_PROFILE_MODE",mode);qputenv("PGN_RUNTIME_TEST_BINDING",cache.fileName().toUtf8());
        const auto registry=pgn::booking::loadSourceRegistry(temp.path());need(registry.error.isEmpty()&&registry.ranges.size()==1000&&registry.ranges.last().row==1001,"actual cache loader: display Zone1000 has provenance CSV row1001");return registry;
    }
};
void initialFiniteAfterConstant(){
    for(const bool held:{false,true}){
        Fixture f(true);if(held)qputenv("PGN_RUNTIME_TEST_CONSTANT_MODE","exit-failure");
        bool delivered=false,finiteAbsentAtDelivery=false;
        f.packetSink=[&](const auto& packet){if(packet.value("type")!="native_work_boundary_inventory"||delivered)return;delivered=true;
            finiteAbsentAtDelivery=f.count("finite")==0&&!f.type("device_control_snapshot");
            check(packet.value("accepted")==!held,"Initial constant success/unknown remains the exact terminal qualification");
            QFile marker(f.temp.filePath("marker.txt"));need(marker.open(QIODevice::WriteOnly|QIODevice::Append)&&marker.write("constant-delivered\n")>0&&marker.flush(),"Own terminal delivery marker");};
        check(f.runtime->start()&&waitFor([&]{return delivered&&f.type("device_control_snapshot");}),"Initial constant terminal is delivered, then independent finite observation settles");
        check(finiteAbsentAtDelivery,"First actual constant packet reaches UI sink BEFORE any initial finite command or invented sample");
        check(waitFor([&]{return f.type("resource_snapshot");})&&f.metrics==0&&f.controls==0,"Independent driver observation arrives even OFF, never as producer/control data");
        QFile marker(f.temp.filePath("marker.txt"));need(marker.open(QIODevice::ReadOnly),"Own exact read/delivery ordering");const auto order=marker.readAll();
        check(order.indexOf("profile\n")<order.indexOf("constant\n")&&order.indexOf("constant\n")<order.indexOf("constant-delivered\n")&&order.indexOf("constant-delivered\n")<order.indexOf("finite\n"),"Actual SELF order is profile,constant,terminal delivery,finite (no timing tautology)");
        const auto finite=f.packet("device_control_snapshot","device",QString::fromLatin1(Finite3060Device)).value("snapshot").toObject();
        check(f.count("finite")==1&&finite.value("owned")==false&&finite.value("observed_utc")==Time&&finite.value("desired_enabled")==false,"Exactly one genuine initial finite read preserves original typed OFF/UTC, never default idle");
        check(f.runtime->submit({{"action","application_start_inventory"},{"request_id",id(400)},{"inventory_id",id(401)}})&&waitFor([&]{return !f.packet("application_start_inventory","request_id",id(400)).isEmpty();}),"Subsequent explicit nonclose inventory still settles independently");
        check(f.count("finite")==1&&f.count("FORBIDDEN_CONTROL")==0,"Initial finite one-shot is not repeated by another inventory or converted to a GPU command");
    }
    {Fixture f;bool stopRequested=false;f.packetSink=[&](const auto& packet){if(packet.value("type")=="native_work_boundary_inventory"&&!stopRequested){stopRequested=true;f.runtime->stop();}};
        check(f.runtime->start()&&waitFor([&]{return f.stopped;}),"Stop from first actual inventory delivery drains observer threads");check(stopRequested&&f.count("finite")==0,"Stop revokes deferred initial finite before its queued entry");}
    {Fixture f;bool closeRequested=false;f.packetSink=[&](const auto& packet){if(packet.value("type")=="native_work_boundary_inventory"&&!closeRequested){closeRequested=f.runtime->submit({{"action","application_close_prepare"},{"request_id",id(402)}});}};
        check(f.runtime->start()&&waitFor([&]{return f.type("application_close_frozen");}),"Close from first inventory delivery settles same-writer freeze");check(closeRequested&&f.count("finite")==0,"Close revokes deferred initial finite, no future read leaked past freeze");
        check(f.runtime->submit({{"action","application_close_inventory"},{"request_id",id(402)},{"inventory_id",id(403)}})&&waitFor([&]{return f.type("application_close_inventory");}),"Explicit fresh close inventory still observes both physical profiles");
        check(f.count("finite")==1&&f.packet("application_close_inventory","request_id",id(402)).value("inventory_known")==true,"Fresh close finite is retained, never substituted by suppressed initial sample");}
    {Fixture f(true);bool closedDuringProfile=false;f.timingSink=[&](const auto& event){if(event.value("stage")=="profile_start"&&!closedDuringProfile)closedDuringProfile=f.runtime->submit({{"action","application_close_prepare"},{"request_id",id(404)}});};
        check(f.runtime->start()&&waitFor([&]{return f.type("application_close_frozen");}),"Close while profile is busy reaches writer barrier without initial finite");check(closedDuringProfile&&f.count("finite")==0,"Profile completion cannot resurrect initial finite after close intent");}
    {Fixture f;bool deleted=false;f.packetSink=[&](const auto& packet){if(packet.value("type")=="native_work_boundary_inventory"){f.runtime.reset();deleted=true;}};
        check(f.runtime->start()&&waitFor([&]{return deleted;}),"First inventory sink may destroy runtime before deferred finite scheduling");QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();check(!f.runtime&&f.count("finite")==0,"QPointer/context revokes deferred finite on owner loss");}
}
void unifiedCold(){
    Fixture f;check(!QFileInfo::exists(f.config.transport.paths.sourceHistoryDatabase),"Constructor did not invent scientific attachment");
    check(f.runtime->start(),"Native runtime attaches local capability");check(!f.runtime->start(),"No duplicate runtime/Node attachment");
    check(waitFor([&]{return f.type("native_work_boundary_inventory")&&f.type("device_control_snapshot");}),"Actual constant and finite acquisition paths composed");
    check(f.count("constant")==1&&f.count("finite")==1,"One real read client per discovery, no polling loop or command retry");
    check(f.metrics==0&&!QFileInfo::exists(f.config.transport.paths.sourceHistoryDatabase),"Cold observations invent neither scientific data nor idle GPU metric");
    check(f.runtime->submit({{"action","module_list"},{"request_id",id(10)}}),"Actual native module read routes through same writer");
    check(waitFor([&]{return f.type("module_registry");}),"Native module result delivered through actual bridge channel");
    check(!QFileInfo::exists(f.config.transport.paths.moduleRegistryDatabase),"Read on absent registry creates no DB");
    const auto moduleBefore=f.packets.size();check(f.runtime->submit({{"action","module_list"}}),"Malformed module request reaches explicit refusal, not disappearance");
    check(waitFor([&]{return f.packets.size()>moduleBefore&&f.packets.last().value("type")=="module_registry"&&f.packets.last().contains("error");}),"Missing module correlation yields exact local error packet");
    check(!f.runtime->submit({{"action","not_an_action"}}),"Unknown action refused without legacy fallback");
    check(!f.runtime->submit({{"action","start"},{"request_id",id(1)},{"device",Device}}),"Naked productive start refused, no configured-work fallback");
    // The positive read-only no-op requires a KNOWN empty native dispatch
    // table. Missing schema is intentionally unknown, tested by AppControl.
    pgn::booking::BookingStore sameStore(f.config.transport.paths.bookingDatabase);
    check(!sameStore.hasPreparedDispatches().has_value(),"Fresh fixture has absent dispatch schema, not a fabricated empty queue");
    sqlite3* ownDb{};need(sqlite3_open_v2(f.config.transport.paths.bookingDatabase.toUtf8().constData(),&ownDb,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"Own existing fixture store");
    const auto schema=sqlite3_exec(ownDb,"CREATE TABLE dispatch_requests(id TEXT PRIMARY KEY,start TEXT NOT NULL,end TEXT NOT NULL,device TEXT NOT NULL,state TEXT NOT NULL,predecessor TEXT,run TEXT,created_utc TEXT NOT NULL)",nullptr,nullptr,nullptr);sqlite3_close(ownDb);
    need(schema==SQLITE_OK,"Own explicit native empty dispatch schema");
    check(sameStore.hasPreparedDispatches()==false,"Typed existing empty dispatch table licenses only the positive fixture");
    const auto before=f.controls;f.bridge.control(Device,id(2),false);
    check(waitFor([&]{return f.controls>before&&f.type("native_control_observation");}),"Already-OFF actual persisted STOP resolves native button");
    check(f.count("constant")==2&&f.metrics==0,"OFF observation sent no mutable command or fake metric");
    check(f.runtime->submit({{"action","application_close_prepare"},{"request_id",id(3)}}),"One application close freeze");
    check(!f.runtime->submit({{"action","module_register"},{"request_id",id(4)}}),"Close freezes new mutation admission");
    check(waitFor([&]{return f.type("application_close_frozen");}),"Actual frozen barrier result delivered");
    check(f.runtime->submit({{"action","application_close_inventory"},{"request_id",id(3)},{"inventory_id",id(5)}}),"Frozen inventory still observes both physical profiles");
    check(waitFor([&]{return f.type("application_close_inventory");}),"Both fresh close observations delivered");
    check(f.count("constant")==3&&f.count("finite")==2,"Close acquired actual independent snapshots once each");
    f.runtime->stop();check(!f.runtime->stopped(),"Shutdown is not declared before actual worker completion");
    check(waitFor([&]{return f.stopped&&f.runtime->stopped();}),"Unified runtime observer cleanup asynchronous and complete");
    check(!f.runtime->submit({{"action","module_list"}}),"Stopped runtime refuses new work");
}
void ownerLoss(){Fixture f;check(f.runtime->start(),"Owner-loss acquisition admitted");f.runtime.reset();QCoreApplication::processEvents();check(f.metrics==0,"Owner loss cannot synthesize sample");}
qint64 scalar(const QString& path,const char* sql){
    sqlite3* db{};if(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK){sqlite3_close(db);return -1;}
    sqlite3_stmt* q{};qint64 result=-1;if(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW)result=sqlite3_column_int64(q,0);sqlite3_finalize(q);sqlite3_close(db);return result;
}
void firstOwned(bool timingAudit=false,bool completedRecovery=false,bool explicitStartObservation=false){
    using namespace pgn::booking;
    Fixture f(timingAudit);BookingStore store(f.config.transport.paths.bookingDatabase);SourceRegistry registry;
    registry.url="https://PUBLIC-TESTONLY.invalid/source";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString(Time,Qt::ISODate);
    registry.ranges={{1,"100000","110000","Open",{}}};
    check(store.queue(registry,"100000","110000",Device).isEmpty(),"Only local public fixture intent queued");
    const auto p=store.prepareInitialStart(id(20),Device,{"100000","110000","100000","110000","100000","10000"},registry,{WorkBoundary::Ready,id(21)});
    need(p.status==InitialStartStatus::Prepared&&p.dispatch.has_value(),"Own PREPARED without fake RUNTIME");
    qputenv("PGN_RUNTIME_TEST_BOOKING",p.dispatch->bookingId.toUtf8());
    check(scalar(f.config.transport.paths.bookingDatabase,"SELECT count(*) FROM bookings WHERE kind='RUNTIME'")==0,"No UI observer or fabricated physical run paid the fixture");
    if(explicitStartObservation)qunsetenv("PGN_RUNTIME_TEST_BOOKING");
    check(f.runtime->start(),"One actual owned discovery starts native composition");
    if(explicitStartObservation){
        check(waitFor([&]{return f.type("native_work_boundary_inventory")&&f.type("device_control_snapshot");}),"Actual cold discovery finishes without inventing source");
        check(f.count("stream")==0,"Cold discovery had no source client");
        qputenv("PGN_RUNTIME_TEST_BOOKING",p.dispatch->bookingId.toUtf8());
        check(f.runtime->submit({{"action","application_start_inventory"},{"request_id",id(820)},{"inventory_id",id(821)}}),"Explicit start observes the now-owned epoch");
    }
    check(waitFor([&]{return scalar(f.config.transport.paths.bookingDatabase,"SELECT count(*) FROM dispatch_requests WHERE state='STARTED' AND run IS NOT NULL")==1;}),"First actual owned observation reconciles PREPARED before manifest save");
    check(scalar(f.config.transport.paths.bookingDatabase,"SELECT count(*) FROM bookings WHERE kind='RUNTIME' AND state='ACTIVE'")==1,"Actual initial RUNNING identity created atomically, not by UI model");
    check(waitFor([&]{return f.count("stream")==1&&QFileInfo::exists(f.config.transport.paths.sourceHistoryDatabase);}),"Same first owned epoch attaches exactly one real SOURCE client");
    check(store.savedSourceRunManifest(Device,OwnedRun).status==SourceRunManifestReadStatus::Present,"Actual immutable manifest belongs to reconciled tuple");
    check(f.count("constant")==(explicitStartObservation?2:1),"No extra physical poll rescued wrong native ordering");
    check(waitFor([&]{return f.type("native_index");}),"Independent exact index reader reaches application presentation without another SOURCE frame");
    QJsonObject index;for(const auto& packet:f.packets)if(packet.value("type")=="native_index")index=packet;
    check(index.value("complete")==true&&index.value("all").isObject()&&index.value("current").toObject().value("scope")==OwnedRun,"Asynchronous index retains complete all/current schema and exact source run, not an absent-as-zero fallback");
    check(!f.indexWrongThread&&f.count("stream")==1,"Independent index presentation stays on creating thread without restarting the SOURCE client");
    if(timingAudit){
        const auto stage=[&](const QString& name){for(const auto& p:f.timings)if(p.value("stage")==name)return true;return false;};
        check(waitFor([&]{return stage("index_done");}),"opt-in real SOURCE first frame/index timing reaches application sink");
        for(const auto& name:{"initialize_begin","initialize_done","frame_received","source_admitted","index_begin","index_done"})check(stage(name),"existing source lifecycle/progress forwarded, not fabricated/replaced");
        for(const auto& p:f.timings)if(p.value("component")=="source_runtime"){const auto original=p.value("source").toObject();check(original.value("schema")=="pgn-native-source-trace-1"||original.value("schema")=="pgn-native-source-progress-1","source record keeps original typed schema");if(original.value("run")==OwnedRun)check(original.value("source_epoch")=="1","source epoch is original, never wrapper-refreshed");}
        QFile raw(f.config.transport.paths.sourceMirrorDirectory+'/'+OwnedRun+".stdout");check(raw.open(QIODevice::ReadOnly)&&raw.readAll()==Source,"timing propagation conserves exact SOURCE RAW unchanged");check(!f.timingWrongThread&&f.timings.size()<64&&f.count("FORBIDDEN_CONTROL")==0,"native timing is UI-thread bounded read-only, no launch/control");
    }else check(f.timings.isEmpty(),"default false suppresses trace installation/output despite sink being present");
    if(completedRecovery){
        // This is recovery of the genuine SELF-enrolled run above, never a
        // development archive presented as first preparation or real hardware.
        f.runtime->stop();check(waitFor([&]{return f.stopped;}),"first real SOURCE owner stops before recovery");f.runtime.reset();
        qputenv("PGN_RUNTIME_TEST_SOURCE_MODE","completed-off");
        NativeBridge recoveryBridge;NativeApplicationRuntime recovery(f.config,&recoveryBridge);
        QVector<QJsonObject> recovered;bool recoveryStopped=false;
        recovery.onPacket=[&](const auto& value){recovered.append(value);};recovery.onStopped=[&]{recoveryStopped=true;};
        check(recovery.start(),"completed same-run recovery starts one native runtime");
        check(waitFor([&]{bool complete=false,indexed=false;for(const auto& value:recovered){complete|=value["type"]=="native_source_attempt"&&value["status"]=="complete";indexed|=value["type"]=="native_index"&&value["complete"]==true;}return complete&&indexed;}),"actual completed attachment reaches finite EOF and separate exact index");
        check(f.count("stream")==1&&f.count("constant")==3,"completed attachment pays inventory plus finite snapshot, never a second live stream");
        QFile raw(f.config.transport.paths.sourceMirrorDirectory+'/'+OwnedRun+".stdout");check(raw.open(QIODevice::ReadOnly)&&raw.readAll()==Source,"completed recovery preserves all genuinely enrolled RAW bytes");raw.close();
        check(f.count("FORBIDDEN_CONTROL")==0,"completed observation never launches or stops a GPU");
        recovery.stop();check(waitFor([&]{return recoveryStopped;}),"completed observer cleanup is explicit before fixture removal");
    }
    qunsetenv("PGN_RUNTIME_TEST_BOOKING");
}
int timingIndex(const Fixture& f,const QString& component,const QString& stage){for(qsizetype i=0;i<f.timings.size();++i)if(f.timings[i].value("component")==component&&f.timings[i].value("stage")==stage)return int(i);return -1;}
qint64 dispatchCount(const QString& db);
void timingAudit(){
    {Fixture f;check(f.runtime->start()&&waitFor([&]{return f.type("native_work_boundary_inventory")&&f.type("device_control_snapshot");}),"default-off timing fixture actual read acquisitions finish");check(f.timings.isEmpty()&&f.metrics==0&&!QFileInfo::exists(f.config.transport.paths.sourceHistoryDatabase),"default false creates no timing, RAW or metrics");}
    {Fixture f(true);check(f.runtime->start()&&waitFor([&]{return f.type("native_work_boundary_inventory")&&f.type("device_control_snapshot");}),"opt-in cold timing settles unchanged acquisition");
        const auto profileStart=timingIndex(f,"profile","profile_start"),profileDone=timingIndex(f,"profile","profile_done"),finiteStart=timingIndex(f,"finite","finite_start"),finiteDone=timingIndex(f,"finite","finite_done");
        check(profileStart>=0&&profileDone>profileStart&&finiteStart>profileDone&&finiteDone>finiteStart,"closed actual writer retains serialized profile and finite transport lifecycles");
        int constantStart=-1;for(qsizetype i=0;i<f.timings.size();++i){const auto original=f.timings[i].value("source").toObject();if(original.value("transport").toObject().value("stage")=="request_started"){constantStart=int(i);break;}}
        check(constantStart>profileDone&&finiteStart>constantStart&&finiteStart>timingIndex(f,"inventory","inventory_done"),"initial constant completes before finite read enters; no finite latency in first constant critical path");
        check(timingIndex(f,"writer","inventory_queued")>=0&&timingIndex(f,"writer","inventory_entered")>=0&&timingIndex(f,"inventory","inventory_done")>=0,"writer entry and inventory completion are visible without presentation queue");
        qint64 previous=-1;int sequence=0;for(const auto& p:f.timings){check(p.value("schema")=="pgn-native-application-timing-1"&&p.value("sequence").toInt()==++sequence&&p.value("received_monotonic_ms").toInteger()>=previous,"application timing has original sequence and local monotonic receipt");previous=p.value("received_monotonic_ms").toInteger();const auto encoded=QJsonDocument(p).toJson(QJsonDocument::Compact);check(encoded.size()<4096&&!encoded.contains(f.config.transport.endpoint.keyFile.toUtf8())&&!encoded.contains(f.config.transport.endpoint.host.toUtf8())&&!p.contains("snapshot")&&!p.contains("binding")&&!p.contains("chunk"),"timing metadata excludes endpoint/key/RAW/binding and remains bounded");}
        const auto p=f.timings[profileStart];check(p.value("request_id").isString()&&p.value("inventory_id").isString()&&p.value("request_id")==f.timings[profileDone].value("request_id")&&p.value("inventory_id")==f.timings[profileDone].value("inventory_id"),"profile stages retain the actual inventory correlation");
        check(!f.timingWrongThread&&f.metrics==0&&f.count("FORBIDDEN_CONTROL")==0,"instrumentation is creating-thread and never GPU/physical state");
    }
    firstOwned(true);
    {Fixture f(true);f.timingSink=[](const auto&){throw std::runtime_error("TESTONLY timing sink failure");};check(f.runtime->start()&&waitFor([&]{return f.type("native_work_boundary_inventory")&&f.type("device_control_snapshot");}),"throwing timing sink cannot reject acquisitions or invent failures");check(!f.timings.isEmpty()&&f.count("FORBIDDEN_CONTROL")==0,"sink failure changes no read-only routing");}
    {Fixture f(true);bool deleted=false;f.timingSink=[&](const auto&){f.runtime.reset();deleted=true;};check(f.runtime->start()&&waitFor([&]{return deleted;}),"queued public timing sink may destroy owner without interleaving its pipeline");QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();check(f.timings.size()==1&&!f.runtime&&f.metrics==0,"owner context removes late timing callbacks; no dangling successor/metric");}
    {Fixture f(true);check(f.runtime->start()&&waitFor([&]{return f.type("native_work_boundary_inventory");}),"cap fixture initial real observation");
        for(int n=0;n<8;++n){const auto request=id(100+n),inventory=id(200+n);check(f.runtime->submit({{"action","application_start_inventory"},{"request_id",request},{"inventory_id",inventory}}),"explicit readonly request admitted, no automatic retry for timing");check(waitFor([&]{return !f.packet("application_start_inventory","inventory_id",inventory).isEmpty();}),"each explicit read settles despite timing cap");}
        check(f.timings.size()==64&&f.timings.last().value("sequence")==64&&f.timings.last().value("stage")=="timing_limit"&&f.timings.last().value("suppressed_at_least")==1&&f.timings.last().value("suppressed_count_complete")==false,"global cap63 records plus explicit limit64; no unbounded diagnostics");check(f.count("FORBIDDEN_CONTROL")==0&&dispatchCount(f.config.transport.paths.bookingDatabase)==0&&!QFileInfo::exists(f.config.transport.paths.sourceHistoryDatabase),"cap does not change admission/queue or write SOURCE history");
    }
}
qint64 dispatchCount(const QString& db){
    const auto exists=scalar(db,"SELECT count(*) FROM sqlite_master WHERE type='table' AND name='dispatch_requests'");
    return exists==0?0:scalar(db,"SELECT count(*) FROM dispatch_requests");
}
void closeInitialTicket(){
    for(const bool external:{false,true}){
        Fixture f;sqlite3* db{};need(sqlite3_open_v2(f.config.transport.paths.bookingDatabase.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"own SAME booking DB");
        const auto created=sqlite3_exec(db,"CREATE TABLE dispatch_requests(id TEXT PRIMARY KEY,start TEXT NOT NULL,end TEXT NOT NULL,device TEXT NOT NULL,state TEXT NOT NULL,predecessor TEXT,run TEXT,created_utc TEXT NOT NULL);",nullptr,nullptr,nullptr);sqlite3_close(db);need(created==SQLITE_OK,"own existing-format dispatch table");
        check(f.config.maximumObservationAgeMs==30000&&f.runtime->start()&&waitFor([&]{return f.type("native_work_boundary_inventory")&&f.type("device_control_snapshot");}),"actual Runtime with unchanged production TTL30000 settles startup");
        const auto close=id(700);check(f.runtime->submit({{"action","application_close_prepare"},{"request_id",close}})&&waitFor([&]{return !f.packet("application_close_frozen","request_id",close).isEmpty();}),"normal local freeze");
        check(f.runtime->submit({{"action","application_close_inventory"},{"request_id",close},{"inventory_id",id(701)}})&&waitFor([&]{return !f.packet("application_close_inventory","inventory_id",id(701)).isEmpty();}),"real Initial through Inventory captures typed original no-op ticket");
        const int constant=f.count("constant"),finite=f.count("finite");if(external)qputenv("PGN_RUNTIME_TEST_CONSTANT_MODE","external-running");
        for(const auto& device:{Device,QString::fromLatin1(Finite3060Device)}){const auto request=id(device==Device?702:703);check(f.runtime->submit({{"action","stop"},{"request_id",request},{"device",device}})&&waitFor([&]{return !f.packet("control","request_id",request).isEmpty();}),"normal STOP submit settles without skipping local invalidation");const auto p=f.packet("control","request_id",request);check(p.value("accepted")==true&&p.value("applied")==false&&p.value("physical_outcome")=="UNKNOWN"&&p.value("result").toObject().value("status")=="ALREADY_OFF_OBSERVED","existing no-op report, not latch/applied/Ready");}
        check(f.count("constant")==constant&&f.count("finite")==finite,"two STOP READ children actually removed; no Initial/Final read removed");
        check(f.runtime->submit({{"action","application_close_inventory"},{"request_id",close},{"inventory_id",id(704)}})&&waitFor([&]{return !f.packet("application_close_inventory","inventory_id",id(704)).isEmpty();}),"independent fresh Final still acquired after both no-op reports");
        const auto final=f.packet("application_close_inventory","inventory_id",id(704));check(f.count("constant")==constant+1&&f.count("finite")==finite+1&&f.count("FORBIDDEN_CONTROL")==0,"Final real children preserved, no remote START/STOP action");
        check(final.value("snapshot").toObject().value("owned")==external&&final.value("snapshot").toObject().value("operator_stop")==!external&&!final.contains("ready"),"external change visible in Final: existing Close owned/OFF/target guards must Hold, no ticket permission");
    }
}
void sourceReconnectRead(){
    using namespace pgn::booking;
    for(const QByteArray outcome:{QByteArray("off"),QByteArray("wrong-manifest"),QByteArray("fail-again"),QByteArray("freeze")}){
    Fixture f;BookingStore store(f.config.transport.paths.bookingDatabase);SourceRegistry registry;
    registry.url="https://PUBLIC-TESTONLY.invalid/source";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString(Time,Qt::ISODate);registry.ranges={{1,"100000","110000","Open",{}}};
    need(store.queue(registry,"100000","110000",Device).isEmpty(),"own reconnect queue");
    const auto prepared=store.prepareInitialStart(id(20),Device,{"100000","110000","100000","110000","100000","10000"},registry,{WorkBoundary::Ready,id(21)});
    need(prepared.dispatch.has_value(),"own genuine reconnect initial admission");const auto booking=prepared.dispatch->bookingId;
    qputenv("PGN_RUNTIME_TEST_BOOKING",booking.toUtf8());qputenv("PGN_RUNTIME_TEST_SOURCE_MODE","fail-first");
    const auto prefix=Source.left(Source.indexOf('\n')+1);need(!prefix.isEmpty()&&prefix.size()<Source.size(),"strict committed RAW prefix");
    QJsonObject manifest{{"schema","pgn-native-process-1"},{"run",OwnedRun},{"uuid",Device},{"pid",4242},{"birth",Time},{"start","100000"},{"end","110000"},{"queued_intent",booking},{"stdout","C:/PUBLIC-TESTONLY/"+OwnedRun+".stdout"}};
    QJsonObject packet{{"schema","pgn-runtime-snapshot-1"},{"observed_utc",Time},{"owned",true},{"state","RUNNING"},{"operator_stop",false},{"manifest",manifest},{"offset",0},{"next_offset",qint64(prefix.size())},{"pending_bytes",qint64(Source.size()-prefix.size())},{"chunk",QString::fromLatin1(prefix.toBase64())}};
    {SourceHistory history(f.config.transport.paths.sourceHistoryDatabase);SourceMirror mirror(f.config.transport.paths.sourceMirrorDirectory);NativeObservationSession session(history,mirror,Device);const auto frame=decodeSshSnapshot(QJsonDocument(packet).toJson(QJsonDocument::Compact));check(session.consume(frame,OwnedRun,0,false,true).state==NativeObservationState::Source,"genuine source prefix, no fabricated cursor");}
    QString firstFailedRequest;bool freezeAdmitted{};
    f.packetSink=[&](const QJsonObject& value){
        if(firstFailedRequest.isEmpty()&&value.value("type")=="native_source_attempt"&&value.value("status")=="failed"&&value.value("local_exit")==255){
            firstFailedRequest=value.value("request_id").toString();
            // Change simulated network state AT the causal failure, not after
            // an event-loop drain whose duration depends on machine load.
            if(outcome=="freeze")freezeAdmitted=f.runtime->submit({{"action","application_close_prepare"},{"request_id",id(702)}});
            else qputenv("PGN_RUNTIME_TEST_SOURCE_MODE",outcome);
        }
    };
    check(f.runtime->start()&&waitFor([&]{return !firstFailedRequest.isEmpty();}),"real SELF transport255 before reconnect");
    if(outcome!="freeze")check(waitFor([&]{return f.packet("native_source_reconnect","run",OwnedRun).value("delay_ms")==1000;}),"failure schedules actual one-second read-only backoff, not an immediate control");
    if(outcome=="freeze"){
        check(freezeAdmitted,"close freeze admitted during reconnect backoff");
        check(waitFor([&]{return f.packet("application_close_frozen","request_id",id(702)).value("accepted")==true;}),"real writer freeze observed before retry timer");
        check(!waitFor([&]{return f.count("stream")>1;},1400),"frozen close cannot start a reconnect reader");
        check(f.count("FORBIDDEN_CONTROL")==0,"observer freeze/reconnect does not send physical STOP");continue;
    }
    if(outcome!="off"){
        check(waitFor([&]{return f.count("stream")==2&&f.packet("native_source_attempt","status","failed").value("request_id")!=firstFailedRequest;}),"second attempt retains genuine failure instead of claiming recovered source");
        if(outcome=="fail-again"){
            const auto retry=f.packet("native_source_reconnect","run",OwnedRun);
            check(retry.value("delay_ms")==2000,"repeated transport outage doubles backoff, no tight retry loop");
            f.runtime->stop();check(waitFor([&]{return f.stopped;}),"runtime stop drains before pending retry");
        }
        check(!waitFor([&]{return f.count("stream")>2;},2200),"manifest corruption never retries; explicit runtime stop cancels transport retry");
        QFile unchanged(f.config.transport.paths.sourceMirrorDirectory+'/'+OwnedRun+".stdout");check(unchanged.open(QIODevice::ReadOnly)&&unchanged.readAll()==prefix,"failed reconnect cannot append poisoned or missing RAW");
        check(f.count("FORBIDDEN_CONTROL")==0&&f.controls==0,"failure path sends no GPU control");continue;
    }
    check(waitFor([&]{return !f.packet("native_source_attempt","status","complete").isEmpty();},5000),"source reconnect reads actual remainder and completes without user action");
    check(f.count("stream")==2,"exactly one new read attempt, no duplicate reader");
    QFile raw(f.config.transport.paths.sourceMirrorDirectory+'/'+OwnedRun+".stdout");check(raw.open(QIODevice::ReadOnly)&&raw.readAll()==Source,"committed prefix plus reconnect remainder exactly equals source, no loss/duplication");
    check(f.count("FORBIDDEN_CONTROL")==0&&f.controls==0,"read reconnect never sends physical launch/STOP/control");
    }
}
void sourceCloseReadRepair(){
    using namespace pgn::booking;
    for(const QByteArray outcome:{QByteArray("off"),QByteArray("fail-again"),QByteArray("wrong-manifest"),QByteArray("completed-off"),QByteArray("completed-bad-run")}){
        Fixture f;BookingStore store(f.config.transport.paths.bookingDatabase);SourceRegistry registry;
        registry.url="https://PUBLIC-TESTONLY.invalid/source";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString(Time,Qt::ISODate);registry.ranges={{1,"100000","110000","Open",{}}};
        need(store.queue(registry,"100000","110000",Device).isEmpty(),"own queue");
        const auto prepared=store.prepareInitialStart(id(20),Device,{"100000","110000","100000","110000","100000","10000"},registry,{WorkBoundary::Ready,id(21)});
        need(prepared.dispatch.has_value(),"own genuine initial admission");const auto booking=prepared.dispatch->bookingId;
        qputenv("PGN_RUNTIME_TEST_BOOKING",booking.toUtf8());qputenv("PGN_RUNTIME_TEST_SOURCE_MODE","fail-first");
        QJsonObject manifest{{"schema","pgn-native-process-1"},{"run",OwnedRun},{"uuid",Device},{"pid",4242},{"birth",Time},{"start","100000"},{"end","110000"},{"queued_intent",booking},{"stdout","C:/PUBLIC-TESTONLY/"+OwnedRun+".stdout"}};
        QJsonObject packet{{"schema","pgn-runtime-snapshot-1"},{"observed_utc",Time},{"owned",false},{"state","OFF"},{"operator_stop",true},{"manifest",manifest},{"last_exit",QJsonObject{{"run",OwnedRun},{"exit_code",0}}},{"offset",0},{"next_offset",qint64(Source.size())},{"pending_bytes",0},{"chunk",QString::fromLatin1(Source.toBase64())}};
        {SourceHistory history(f.config.transport.paths.sourceHistoryDatabase);SourceMirror mirror(f.config.transport.paths.sourceMirrorDirectory);NativeObservationSession session(history,mirror,Device);
            const auto frame=decodeSshSnapshot(QJsonDocument(packet).toJson(QJsonDocument::Compact));check(session.consume(frame,OwnedRun,0,false,true).state==NativeObservationState::Source&&session.conservation(frame,OwnedRun).ready,"own existing prefix genuinely enrolled, no hand-made cursor");}
        bool closeFrozen{};const auto request=id(500);
        f.packetSink=[&](const QJsonObject& p){if(!closeFrozen&&p.value("type")=="native_source_attempt"&&p.value("status")=="failed")closeFrozen=f.runtime->submit({{"action","application_close_prepare"},{"request_id",request}});};
        check(f.runtime->start()&&waitFor([&]{return f.packet("native_source_attempt","status","failed").value("local_exit")==255;}),"initial normal SOURCE read really fails without publication; typed receipt survives shared status");
        const auto failed=f.packet("native_source_attempt","status","failed");check(failed["run"]==OwnedRun&&failed["received_frames"]==0&&!failed.contains("chunk")&&!failed.contains("ready"),"attempt is actual bounded local outcome, no RAW or Ready");
        qputenv("PGN_RUNTIME_TEST_SOURCE_MODE",outcome);
        check(closeFrozen,"close freeze admitted at causal failure before autonomous replay");
        check(waitFor([&]{return !f.packet("application_close_frozen","request_id",request).isEmpty();}),"freeze acknowledged before initial inventory");
        auto read=[&](int number){const auto inventory=id(number);check(f.runtime->submit({{"action","application_close_inventory"},{"request_id",request},{"inventory_id",inventory}}),"explicit fresh close read");check(waitFor([&]{return !f.packet("application_close_inventory","inventory_id",inventory).isEmpty();}),"bounded close read settles");return f.packet("application_close_inventory","inventory_id",inventory);};
        const auto initial=read(501);check(initial["accepted"]==true&&initial["source_conservation"].toObject()["ready"]==false&&f.count("stream")==1,"Initial does not delay STOP or infer conservation from known manifest");
        const auto final=read(502);const bool success=outcome=="off"||outcome.startsWith("completed-");
        check(final["source_conservation"].toObject()["ready"]==success,"only normal genuine EOF admission plus reacquisition conserves final SOURCE");
        check(f.count("stream")==((outcome=="wrong-manifest"||outcome=="completed-off")?1:2),"exact manifest guard rejects drift; only matching completion selects finite repair");
        if(success){check(final["accepted"]==true&&final["source_conservation"].toObject()["run"]==OwnedRun&&final["source_conservation"].toObject()["required_end"].toInteger()==Source.size(),"same real run/full-prefix offsets after fresh read, not old snapshot relabeled");check(f.count("constant")>=4+int(outcome=="completed-off"),"new acquisition paid AFTER genuine SOURCE work; completed repair also pays separate finite snapshot");}
        else {check(final["accepted"]==false,"failed or incompatible repair remains Held");const auto again=read(503);check(again["accepted"]==false&&f.count("stream")==((outcome=="wrong-manifest")?1:2),"no automatic or repeated repair under same close token");}
        QFile raw(f.config.transport.paths.sourceMirrorDirectory+'/'+OwnedRun+".stdout");check(raw.open(QIODevice::ReadOnly)&&raw.readAll()==Source,"RAW bytes unchanged by EOF repair");check(f.count("FORBIDDEN_CONTROL")==0,"no GPU/STOP/start action in SOURCE-only seam");
    }
}
void successfulSourceSuffixAtStart(bool activeRace){
    using namespace pgn::booking;
    Fixture f;BookingStore store(f.config.transport.paths.bookingDatabase);SourceRegistry registry;
    registry.url="https://PUBLIC-TESTONLY.invalid/source";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString(Time,Qt::ISODate);registry.ranges={{1,"100000","110000","Open",{}}};
    need(store.queue(registry,"100000","110000",Device).isEmpty(),"own suffix queue");
    const auto prepared=store.prepareInitialStart(id(20),Device,{"100000","110000","100000","110000","100000","10000"},registry,{WorkBoundary::Ready,id(21)});
    need(prepared.dispatch.has_value(),"own real initial suffix admission");qputenv("PGN_RUNTIME_TEST_BOOKING",prepared.dispatch->bookingId.toUtf8());
    qputenv("PGN_RUNTIME_TEST_SOURCE_MODE",activeRace?"complete-late-footer-race":"complete-late-footer");
    const auto release=f.temp.filePath("suffix-release");qputenv("PGN_RUNTIME_TEST_SUFFIX_RELEASE",release.toUtf8());
    const auto prefix=Source.indexOf("[phase0-gpu] gaps reported=");
    const auto rawPath=f.config.transport.paths.sourceMirrorDirectory+'/'+OwnedRun+".stdout";
    check(f.runtime->start(),"runtime observes actual owned epoch before later terminal suffix");
    check(waitFor([&]{return QFileInfo(rawPath).size()==prefix&&(activeRace||!f.packet("native_source_attempt","status","complete").isEmpty());}),"original successful live prefix actually committed; no synthetic cursor");
    const auto firstAttempt=f.packet("native_source_attempt","status","complete").value("request_id").toString();
    if(activeRace)f.packetSink=[&](const QJsonObject& packet){
        if(packet.value("type")=="application_inventory_phase"&&packet.value("inventory_id")==id(873)&&packet.value("phase")=="source_read"&&packet.value("active")==true){
            // Return from the current delivery first, so the inventory is
            // waiting on the old active reader when its successful EOF arrives.
            QTimer::singleShot(10,f.runtime.get(),[&]{QFile file(release);check(file.open(QIODevice::WriteOnly)&&file.write("release")==7,"release original active read at known stopped boundary");});
        }
    };
    check(f.runtime->submit({{"action","application_start_inventory"},{"request_id",id(872)},{"inventory_id",id(873)}}),"explicit Start acquires fresh stopped EOF");
    check(waitFor([&]{return !f.packet("application_start_inventory","inventory_id",id(873)).isEmpty();}),"real runtime drains successful-read suffix before start reply");
    const auto result=f.packet("application_start_inventory","inventory_id",id(873));
    check(result.value("source_conservation").toObject().value("ready")==true&&result.value("source_conservation").toObject().value("committed_offset").toInteger(-1)==Source.size(),"same source actor enrolled exact later terminal extent");
    QFile raw(rawPath);check(raw.open(QIODevice::ReadOnly)&&raw.readAll()==Source,"prefix and terminal suffix conserved exactly once without observer restart");raw.close();
    int completed{};for(const auto& packet:f.packets)if(packet.value("type")=="native_source_attempt"&&packet.value("status")=="complete")++completed;
    check(completed==2&&f.count("stream")==1,"one completed-snapshot continuation, never second live reader or failed-read retry");
    if(!activeRace)check(f.packet("native_source_attempt","status","complete").value("request_id")!=firstAttempt,"suffix owns a fresh real read identity");
    check(result.value("snapshot").toObject().value("resume_lineage").toObject().value("ready")!=true,"raw repair does not grant absent checkpoint/lineage authority");
    f.packetSink={};check(f.runtime->submit({{"action","application_start_inventory"},{"request_id",id(874)},{"inventory_id",id(875)}})&&waitFor([&]{return !f.packet("application_start_inventory","inventory_id",id(875)).isEmpty();}),"subsequent start still acquires fresh independent proof");
    int after{};for(const auto& packet:f.packets)if(packet.value("type")=="native_source_attempt"&&packet.value("status")=="complete")++after;
    check(after==2&&f.count("FORBIDDEN_CONTROL")==0&&f.controls==0,"no duplicate source continuation and no physical command");
    qunsetenv("PGN_RUNTIME_TEST_SUFFIX_RELEASE");
}
void cancelAutonomousFiniteRead(){
    Fixture f;qputenv("PGN_RUNTIME_TEST_SOURCE_MODE","finite-wait");
    check(f.runtime->start()&&waitFor([&]{return f.count("finite")==1;}),"own autonomous finite read entered SELF before freeze");
    qputenv("PGN_RUNTIME_TEST_SOURCE_MODE","off");const auto request=id(510);
    check(f.runtime->submit({{"action","application_close_prepare"},{"request_id",request}}),"accepted freeze cancels only autonomous finite client");
    check(waitFor([&]{return !f.packet("application_close_frozen","request_id",request).isEmpty();},5000),"freeze drains cancelled read without waiting its artificial 15s body");
    check(f.runtime->submit({{"action","application_close_inventory"},{"request_id",request},{"inventory_id",id(511)}})&&waitFor([&]{return !f.packet("application_close_inventory","inventory_id",id(511)).isEmpty();}),"separate final finite acquisition still paid");
    check(f.count("finite")==2&&f.packet("application_close_inventory","inventory_id",id(511))["finite_device"].toObject()["status"]=="observed"&&f.count("FORBIDDEN_CONTROL")==0,"cancelled READ cannot fabricate idle; fresh real finite packet required, no device control");
}
void sourceActiveClose(){
    using namespace pgn::booking;Fixture f;BookingStore store(f.config.transport.paths.bookingDatabase);SourceRegistry registry;
    registry.url="https://PUBLIC-TESTONLY.invalid/source";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString(Time,Qt::ISODate);registry.ranges={{1,"100000","110000","Open",{}}};
    need(store.queue(registry,"100000","110000",Device).isEmpty(),"own active read queue");const auto p=store.prepareInitialStart(id(20),Device,{"100000","110000","100000","110000","100000","10000"},registry,{WorkBoundary::Ready,id(21)});need(p.dispatch.has_value(),"own prepared");
    qputenv("PGN_RUNTIME_TEST_BOOKING",p.dispatch->bookingId.toUtf8());qputenv("PGN_RUNTIME_TEST_SOURCE_MODE","active-wait");
    check(f.runtime->start()&&waitFor([&]{return f.count("stream")==1;}),"one genuine SOURCE read entered, publication deliberately delayed");
    qputenv("PGN_RUNTIME_TEST_SOURCE_MODE","off");const auto request=id(520);check(f.runtime->submit({{"action","application_close_prepare"},{"request_id",request}}),"freeze while normal SOURCE still active");
    check(waitFor([&]{return !f.packet("application_close_frozen","request_id",request).isEmpty();}),"freeze remains independent of SOURCE EOF");
    for(int n:{521,522}){check(f.runtime->submit({{"action","application_close_inventory"},{"request_id",request},{"inventory_id",id(n)}}),"fresh close acquisition during active-read lifecycle");check(waitFor([&]{return !f.packet("application_close_inventory","inventory_id",id(n)).isEmpty();}),"active read/publication plus fresh acquisition settles");}
    const auto final=f.packet("application_close_inventory","inventory_id",id(522));
    check(final["accepted"]==true&&final["source_conservation"].toObject()["ready"]==true&&final["source_conservation"].toObject()["run"]==OwnedRun,"active normal read supplies real conservation; new final inventory proves identity");
    check(f.count("stream")==1&&f.count("FORBIDDEN_CONTROL")==0,"no reconnect or command while first SOURCE read active");
}
void configuredProfileRouting(){
    using namespace pgn::booking;
    for(const auto mode:{QByteArray("qualified"),QByteArray("qualified-legacy"),QByteArray("scope-disabled"),QByteArray("scope-wrong-type"),QByteArray("capacity-null"),QByteArray("binding-changed")}){
        Fixture f;const auto registry=f.configureProfile(mode);BookingStore store(f.config.transport.paths.bookingDatabase);
        const auto first=addUnsigned(ZoneStart,"7000000000000000"),last=addUnsigned(first,Width);
        check(store.queue(registry,first,last,Device).isEmpty(),"Real tempStore existing manual intent in Zone1000");
        const auto before=store.active();need(before.size()==1&&before[0].start==first&&before[0].state=="QUEUED","own immutable queued tuple");const auto booking=before[0].id;
        check(f.runtime->start(),"Actual Runtime profile-before-inventory path admitted");
        check(waitFor([&]{return f.type("native_work_boundary_inventory")&&f.type("device_control_snapshot");}),"Actual typed profile and both discovery observations settle");
        check(f.count("profile")>=1&&f.count("FORBIDDEN_CONTROL")==0,"Actual profile factory action recognized, no control sent by discovery");
        QFile trace(f.temp.filePath("marker.txt"));need(trace.open(QIODevice::ReadOnly),"own ordered acquisition trace");const auto initialTrace=trace.readAll();trace.close();
        check(initialTrace.indexOf("profile\n")>=0&&initialTrace.indexOf("profile\n")<initialTrace.indexOf("constant\n"),"Capability acquired BEFORE actual inventory, no timestamp relabeling");
        const bool qualified=mode=="qualified"||mode=="qualified-legacy";
        check(f.runtime->submit({{"action","application_start_inventory"},{"request_id",id(310)},{"inventory_id",id(311)}}),"Request separate actual initial inventory");
        check(waitFor([&]{return !f.packet("application_start_inventory","request_id",id(310)).isEmpty();}),"Initial typed capacity envelope settles");
        const auto initial=f.packet("application_start_inventory","request_id",id(310));
        check((initial["accepted"]==true)==qualified,"CapacityNULL or whole-binding mismatch holds initial; valid A73 narrower Zone1000 passes");
        if(!qualified)check(!initial["error"].toString().isEmpty(),"Initial held reason explicit, no zero availability");
        check(f.runtime->submit({{"action","dispatch"},{"booking_id",booking}}),"Arm only existing reservation through actual Runtime path");
        check(waitFor([&]{return !f.packet("dispatch","booking_id",booking).isEmpty();}),"Actual arm result settles");
        const auto armed=f.packet("dispatch","booking_id",booking);check((armed["accepted"]==true)==qualified,"Same-store mutation gated by profile and entire binding DTO");
        const auto after=store.active();check(after.size()==1&&after[0].id==booking&&after[0].start==first&&after[0].end==last&&after[0].createdUtc==before[0].createdUtc,"Immutable booking identity/range/original age preserved");
        check(after.size()==1&&after[0].state==(qualified?"ARMED":"QUEUED"),"Negative guard cannot arm, positive only records explicit ARMED not STARTED");
        check(dispatchCount(f.config.transport.paths.bookingDatabase)==(qualified?1:0),"No PREPARED/dispatch created by unqualified observation");
        if(qualified)check(scalar(f.config.transport.paths.bookingDatabase,"SELECT count(*) FROM dispatch_requests WHERE state='ARMED' AND run IS NULL")==1,"Existing intent armed without assigning run or launching");
        else check(!armed["error"].toString().isEmpty(),"Arm failure explicit and booking retained");
        check(f.count("FORBIDDEN_CONTROL")==0&&f.count("stream")==0&&f.metrics==0&&!QFileInfo::exists(f.config.transport.paths.sourceHistoryDatabase),"Positive and negative capacity fixtures launch nothing and invent no source/metric");
    }
}
void configuredLatchFreshObservation(){
    Fixture f;f.configureProfile("latch-unqualified");check(f.runtime->start(),"Unqualified cold profile observation starts, no new work permitted");
    check(waitFor([&]{return f.type("native_work_boundary_inventory")&&f.type("device_control_snapshot");}),"Initial cold/unlatched observation known");
    bool applied{};f.bridge.onControl=[&](const auto&,const auto&,bool observed,const auto&){++f.controls;applied=observed;};
    f.bridge.control(Device,id(320),false);
    check(waitFor([&]{return !f.packet("native_control_observation","request_id",id(320)).isEmpty();}),"New actual readonly OFF observation follows latch-only report");
    const auto latch=f.packet("control","request_id",id(320));check(latch["accepted"]==false&&latch["desired_off_latched"]==true&&latch["applied"]==false,"Latch ACK never physical acceptance");
    const auto observed=f.packet("native_control_observation","request_id",id(320))["snapshot"].toObject();
    check(observed["owned"]==false&&observed["state"]=="OFF"&&observed["operator_stop"]==true&&observed["manifest"].isNull(),"Only fresh exact OFF/STOP/manifestNULL settles button");
    QFile trace(f.temp.filePath("marker.txt"));need(trace.open(QIODevice::ReadOnly),"own latch/read ordering");const auto order=trace.readAll();
    check(order.lastIndexOf("constant\n")>order.indexOf("configured-latch\n")&&f.count("configured-latch")==1,"New SSH-SELF read AFTER latch, no receipt relabeled as observation");
    check(f.controls==1&&applied&&f.metrics==0&&f.count("FORBIDDEN_CONTROL")==0,"Actual typed observation resolves button without fake GPU0% or any start");
}
}
namespace {
void queuedStartDuringDiscovery(){
    {
        Fixture f;check(f.runtime->start(),"Own runtime discovery begins");
        check(f.bridge.inspectApplicationStart(id(800),id(801)),"Actual bridge accepts explicit read while discovery is active");
        check(waitFor([&]{return !f.packet("application_start_inventory","request_id",id(800)).isEmpty();}),"Queued read actually settles after discovery, not a fake ACK");
        check(f.count("constant")==2&&f.controls==0&&f.count("FORBIDDEN_CONTROL")==0,"Exactly two separate reads, no physical START or retry");
        f.bridge.moduleAction({{"action","module_list"},{"request_id",id(802)}});
        check(waitFor([&]{return f.type("module_registry");}),"Actual public bridge/writer remains usable after queued read");
    }
    {
        Fixture f;check(f.runtime->start(),"Second own discovery begins");
        const QJsonObject first{{"action","application_start_inventory"},{"request_id",id(810)},{"inventory_id",id(811)}};
        check(f.runtime->submit(first)&&f.runtime->submit(first),"Same pending read is idempotent without a second slot");
        check(f.runtime->submit({{"action","application_start_inventory"},{"request_id",id(812)},{"inventory_id",id(813)}}),"Excess read receives semantic refusal, not transport failure");
        check(f.runtime->submit({{"action","application_close_prepare"},{"request_id",id(814)}}),"Close freezes the same writer");
        check(waitFor([&]{return !f.packet("application_start_inventory","request_id",id(812)).isEmpty()&&!f.packet("application_start_inventory","request_id",id(810)).isEmpty();}),"Real queued refusal and cancellation packets delivered");
        const auto refused=f.packet("application_start_inventory","request_id",id(812));
        check(refused["accepted"]==false&&refused["error"].toString().contains("already queued"),"Bounded overflow is explicit and correlated");
        const auto cancelled=f.packet("application_start_inventory","request_id",id(810));
        check(cancelled["accepted"]==false&&cancelled["error"].toString().contains("cancelled"),"Close cancels pending readonly admission before any preparation");
        check(waitFor([&]{return f.type("application_close_frozen");}),"Actual close barrier completes");
        check(f.count("constant")<=1&&f.controls==0&&f.count("FORBIDDEN_CONTROL")==0,"No queued read or physical control leaks past freeze");
    }
}
void hardwareDuringAdmission(){
 Fixture f;bool beforeInventory{};
 f.packetSink=[&](const auto& p){if(p.value("type")=="resource_snapshot")beforeInventory=!f.type("native_work_boundary_inventory");};
 qputenv("PGN_RUNTIME_TEST_HARDWARE_DURING_PROFILE","1");
 check(f.runtime->start(),"Own delayed discovery starts");
 check(waitFor([&]{return f.type("resource_snapshot");}),"Dedicated vendor telemetry delivered during pending admission");
 qunsetenv("PGN_RUNTIME_TEST_HARDWARE_DURING_PROFILE");
 check(beforeInventory,"Physical dashboard does not wait for control inventory");
 check(f.controls==0&&f.count("FORBIDDEN_CONTROL")==0,"Telemetry cannot start/stop or fabricate scientific rates");
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    if(app.arguments().size()>1&&app.arguments().at(1)=="-F")return fake(app.arguments());
    if(app.arguments().contains("--driver-recovery")||app.arguments().contains("--driver-recovery-source")){
        using namespace pgn::booking;
        {
            Fixture f;f.driver(true);SourceRegistry registry;registry.url="https://PUBLIC-TESTONLY.invalid/source";registry.sha256=QString(64,'a');registry.observedUtc=QDateTime::fromString(Time,Qt::ISODate);registry.ranges={{1,"100000","110000","Open",{}}};
            BookingStore store(f.config.transport.paths.bookingDatabase);need(store.queue(registry,"100000","110000",Device).isEmpty(),"Own recovery intent");
            const auto prepared=store.prepareInitialStart(id(20),Device,{"100000","110000","100000","110000","100000","10000"},registry,{WorkBoundary::Ready,id(21)});need(prepared.dispatch.has_value(),"Own recovery prepared intent");
            qputenv("PGN_RUNTIME_TEST_BOOKING",prepared.dispatch->bookingId.toUtf8());qputenv("PGN_RUNTIME_TEST_SOURCE_MODE","complete-driver-outage");
            check(f.runtime->start()&&waitFor([&]{return f.packet("native_work_boundary_inventory","type","native_work_boundary_inventory").value("retry_read_after_driver_recovery")==true;}),"EOF then driver outage yields a held, correlated full inventory");
            const auto held=f.packet("native_work_boundary_inventory","type","native_work_boundary_inventory");
            check(held.value("accepted")==false&&held.value("snapshot").isNull(),"Missing driver cannot authorize a snapshot or START");
            const auto attempt=f.packet("native_source_attempt","status","complete");
            check(attempt.value("committed_offset").toInteger(-1)==Source.size()&&f.count("stream")==1,"Completed SOURCE remains fully committed through the outage");
            const auto reads=f.count("constant");waitFor([]{return false;},5600);
            check(reads==2&&f.count("constant")==reads,"Repeated vendor outage observations do not spin full inventory READs");
            const auto acceptedBoundaries=[&]{int count=0;for(const auto& packet:f.packets)if(packet.value("type")=="native_work_boundary_inventory"&&packet.value("accepted")==true)++count;return count;};
            const auto acceptedBefore=acceptedBoundaries();f.driver(true);
            // The unchanged lineage verifier requires a second fresh physical
            // read AFTER its proof. These two snapshots are ONE acquisition,
            // not a duplicate inventory or permission to remove the refresh.
            const bool recovered=waitFor([&]{return f.count("constant")==reads+2&&acceptedBoundaries()==acceptedBefore+1;},6500);
            check(recovered,"Fresh vendor recovery wakes one accepted boundary with its required post-lineage refresh");
            check(!f.packet("native_hardware_availability","state","available").isEmpty(),"Actual vendor recovery independently updates availability notice without fabricating mining/control acknowledgement");
            if(!recovered)std::cerr<<"RECOVERY_DIAGNOSTIC reads="<<f.count("constant")<<" stream="<<f.count("stream")<<" latest="<<QJsonDocument(f.packet("native_work_boundary_inventory","type","native_work_boundary_inventory")).toJson(QJsonDocument::Compact).toStdString()<<" statuses="<<f.statuses.join(" | ").toStdString()<<'\n';
            if(recovered)waitFor([]{return false;},5600);
            check(f.count("constant")==reads+2&&acceptedBoundaries()==acceptedBefore+1&&f.count("stream")==1&&f.count("FORBIDDEN_CONTROL")==0,"Accepted recovered boundary is not redispatched, reread, or converted to GPU control");
        }
        if(!app.arguments().contains("--driver-recovery-source"))for(const int mode:{0,1,2}){
            Fixture f;f.driver(false);
            check(f.runtime->start()&&waitFor([&]{return f.packet("native_work_boundary_inventory","type","native_work_boundary_inventory").value("retry_read_after_driver_recovery")==true;}),"Cold driver outage is explicit, not a fabricated idle observation");
            if(mode==0)check(waitFor([&]{return !f.packet("native_hardware_availability","state","unknown").isEmpty();}),"Cold rejected vendor-only sample publishes UNKNOWN, not valid telemetry/zero rates; no recent-source freshness gate hides this discriminating test");
            const auto reads=f.count("constant");need(reads==1,"One initial cold READ");
            if(mode==1)check(f.runtime->submit({{"action","application_close_prepare"},{"request_id",id(850)}})&&waitFor([&]{return f.type("application_close_frozen");}),"Freeze reaches writer barrier before driver recovery");
            if(mode==2){f.runtime->stop();check(waitFor([&]{return f.stopped;}),"Stop drains the cold observer before recovery");}
            f.driver(true);
            if(mode==0)check(waitFor([&]{return f.count("constant")==reads+1&&f.packet("native_work_boundary_inventory","type","native_work_boundary_inventory").value("accepted")==true;},6500),"Cold discovery resumes once from a fresh positive driver observation");
            else {waitFor([]{return false;},5600);check(f.count("constant")==reads,"Frozen or stopped runtime never resurrects discovery on recovery");}
            check(f.controls==0&&f.count("FORBIDDEN_CONTROL")==0,"Driver recovery alone never changes physical GPU state");
        }
        std::cout<<"DRIVER_RECOVERY "<<checks<<" checks / "<<failures<<" failures; SELF files and reads only, no GPU\n";return failures?1:0;
    }
    if(app.arguments().contains("--completed-read-handoff")){
        using namespace pgn::booking;
        Fixture f;SourceRegistry registry;registry.url="https://PUBLIC-TESTONLY.invalid/source";registry.sha256=QString(64,'a');registry.observedUtc=QDateTime::fromString(Time,Qt::ISODate);registry.ranges={{1,"100000","110000","Open",{}}};
        BookingStore store(f.config.transport.paths.bookingDatabase);need(store.queue(registry,"100000","110000",Device).isEmpty(),"own handoff intent");
        const auto prepared=store.prepareInitialStart(id(20),Device,{"100000","110000","100000","110000","100000","10000"},registry,{WorkBoundary::Ready,id(21)});need(prepared.dispatch.has_value(),"own prepared handoff fixture");
        qputenv("PGN_RUNTIME_TEST_BOOKING",prepared.dispatch->bookingId.toUtf8());qputenv("PGN_RUNTIME_TEST_SOURCE_MODE","complete-no-terminal");
        bool physicalOffReachedCard=false;
        f.bridge.onDevice=[&](const auto& lane){++f.metrics;physicalOffReachedCard|=lane.runId==OwnedRun&&lane.raw.value("pgn_process_running")==false&&lane.state=="stopped";};
        check(f.runtime->start()&&waitFor([&]{return f.packet("native_source_attempt","status","complete").value("received_frames")==1;}),"one successful actual SELF SOURCE read ends without terminal OFF publication");
        check(waitFor([&]{return f.count("constant")>=2;},2000),"successful EOF has one fresh physical-boundary READ successor, not a lost wakeup");
        check(waitFor([&]{return f.type("native_process_observation");},6000),"later fresh native boundary publishes physical OFF even when final SOURCE frame was still RUNNING");
        const auto physical=f.packet("native_process_observation","type","native_process_observation");
        check(physical.value("state")=="OFF"&&physical.value("owned")==false&&physical.value("last_exit").toObject().value("run")==OwnedRun&&
            !physical.contains("lines")&&!physical.contains("chunk")&&!physical.contains("source_conservation")&&!physical.contains("resume_lineage"),
            "physical OFF envelope cannot mint SOURCE bytes, rates, conserved proof or Ready-to-close");
        check(physicalOffReachedCard,"actual runtime-to-bridge path delivers OFF to the same card without restarting SOURCE");
        check(f.count("stream")==1&&f.count("FORBIDDEN_CONTROL")==0,"handoff does not retry SOURCE or issue physical control");
        std::cout<<"COMPLETED_READ_HANDOFF "<<checks<<" checks / "<<failures<<" failures; SELF read-only, no GPU\n";return failures?1:0;
    }
    if(app.arguments().contains("--start-observed-only")){firstOwned(false,false,true);std::cout<<"Start observation source: "<<checks<<" checks / "<<failures<<" failures; own SELF only, no physical control"<<std::endl;return failures?1:0;}
    if(app.arguments().contains("--hardware-pending-only")){hardwareDuringAdmission();std::cout<<"Independent hardware: "<<checks<<" checks / "<<failures<<" failures; SELF vendor fixture, no real GPU"<<std::endl;return failures?1:0;}
    if(app.arguments().contains("--queued-start-only")){queuedStartDuringDiscovery();std::cout<<"Queued start discovery: "<<checks<<" checks / "<<failures<<" failures; own SELF read fixtures, no physical GPU"<<std::endl;return failures?1:0;}
    if(app.arguments().contains("--source-suffix-start")){successfulSourceSuffixAtStart(false);successfulSourceSuffixAtStart(true);}else if(app.arguments().contains("--source-reconnect"))sourceReconnectRead();else if(app.arguments().contains("--completed-attachment"))firstOwned(false,true);else if(app.arguments().contains("--close-initial-ticket"))closeInitialTicket();else if(app.arguments().contains("--source-close-repair"))sourceCloseReadRepair();else if(app.arguments().contains("--source-active-close"))sourceActiveClose();else if(app.arguments().contains("--cancel-finite-read"))cancelAutonomousFiniteRead();else if(app.arguments().contains("--initial-finite-only"))initialFiniteAfterConstant();else if(app.arguments().contains("--timing-only"))timingAudit();else{unifiedCold();firstOwned();ownerLoss();configuredProfileRouting();configuredLatchFreshObservation();}
}catch(const std::exception& e){++failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
std::cout<<"NATIVE_APPLICATION_RUNTIME "<<checks<<" checks / "<<failures<<" failures\n";return failures?1:0;}

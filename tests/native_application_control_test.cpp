#include "mining/native_application_control.h"
#include "mining/native_application_inventory.h"
#include "mining/native_bridge.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <sqlite3.h>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <chrono>
using namespace pgn::mining;
namespace {
int checks{},failures{};
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<why<<'\n';}}
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>bool waitFor(F f,int ms=30000){QElapsedTimer age;age.start();while(!f()&&age.elapsed()<ms){QCoreApplication::processEvents(QEventLoop::AllEvents,10);QThread::msleep(1);}return f();}
QString id(int n){return "40000000-0000-4000-8000-"+QString::number(n,16).rightJustified(12,'0');}
const QString Constant="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",Finite=QString::fromLatin1(Finite3060Device),Run(32,'a'),Other(32,'b'),Birth="2026-10-15T00:00:00.1234567Z",Image(64,'A');
QJsonObject process(){
    const QString path="C:/PUBLIC-TESTONLY/engine.exe";
    return {{"schema","pgn-runtime-snapshot-1"},{"owned",true},{"state","RUNNING"},{"operator_stop",false},{"driver_valid",false},{"observed_utc",Birth},
        {"offset",0},{"next_offset",0},{"pending_bytes",0},{"chunk",""},{"observed_stop_protocol","pgn-stop-observed-1"},
        {"binding",QJsonObject{{"uuid",Constant},{"executable",path},{"pins",QJsonArray{QJsonObject{{"path",path},{"sha256",Image}}}}}},
        {"manifest",QJsonObject{{"schema","pgn-native-process-1"},{"uuid",Constant},{"run",Run},{"pid",1234},{"birth",Birth}}}};
}
QJsonObject finite(bool owned,bool desired){
    const auto job=QString("C:/PGN/runtime/devices/test-3060/jobs/")+Run;
    return {{"schema","pgn-device-control-observation-1"},{"device",Finite},{"scope","BOUNDED_SCIENCE_TEST"},{"observed_utc",Birth},
        {"owned",owned},{"desired_enabled",desired},{"stop_requested",false},{"source_credit",false},{"auto_advance",false},
        {"manifest",owned?QJsonValue(QJsonObject{{"run",Run},{"uuid",Finite},{"pid",1234},{"birth",Birth},{"image_sha256",Finite3060ImageSha256},{"start",Finite3060Start},{"length",Finite3060Length},
            {"stop",job+".stop"},{"stdout",job+".stdout"},{"stderr",job+".stderr"},{"request_id","previous-worker-request"}}):QJsonValue(QJsonValue::Null)},
        {"last_exit",QJsonValue::Null},{"last_error",QJsonValue::Null}};
}
QJsonObject configuredProfile(){
 const QString path="C:/PUBLIC-TESTONLY/engine.exe";
 return {{"schema","pgn-configured-profile-observation-1"},{"protocol","pgn-configured-stop-1"},{"status","CONFIGURED_PROFILE_OBSERVED"},
  {"profile",QJsonObject{{"device",Constant},{"executable",path},{"image_sha256",Image},{"binding_sha256",QString(64,'B')}}},
  {"binding",QJsonObject{{"uuid",Constant},{"executable",path},{"pins",QJsonArray{QJsonObject{{"path",path},{"sha256",Image}}}},{"unknown_extension","ORIGINAL"}}},{"source_capacity",QJsonValue::Null}};
}
void output(const QJsonObject& object){const auto raw=QJsonDocument(object).toJson(QJsonDocument::Compact);need(std::fwrite(raw.constData(),1,size_t(raw.size()),stdout)==size_t(raw.size())&&std::fflush(stdout)==0,"own fake stdout");}
int fake(const QStringList& args){
    const auto wide=QByteArray::fromBase64(args.last().section(' ',-1).toLatin1());const auto script=QString::fromUtf16(reinterpret_cast<const char16_t*>(wide.constData()),wide.size()/2);
    const bool isFinite=script.contains("test-3060/native-test-control.ps1");const auto match=QRegularExpression("-Action ([a-z-]+)").match(script);need(match.hasMatch(),"own exact action");
    const auto action=match.captured(1);need(action=="snapshot"||action=="stop-observed"||action=="configured-profile"||action=="stop-configured"||action=="initial"||isFinite&&(action=="start"||action=="stop"),"only explicit TESTSELF ABI in fake");
    need(std::fgetc(stdin)==EOF,"own fake null input");QFile marker(qEnvironmentVariable("PGN_APP_CONTROL_MARKER"));need(marker.open(QIODevice::WriteOnly|QIODevice::Append),"own marker file");
    const auto line=(isFinite?"finite/":"constant/")+action+(action=="stop"?(script.contains("-ExpectedRun")?"/cas":"/latch"):QString{});
    need(marker.write(line.toUtf8()+'\n')>0&&marker.flush(),"own marker write");marker.close();
    const auto mode=qEnvironmentVariable("PGN_APP_CONTROL_MODE");
    if(mode=="slow")QThread::msleep(15000);
    if(action=="configured-profile"){
        if(mode=="profile-missing")return 2;auto p=configuredProfile();if(mode=="profile-legacy")p["protocol"]="legacy";output(p);return 0;
    }
    if(action=="stop-configured"){
        need(script.contains("-ExpectedBindingSha256 '"+QString(64,'B')+"'")&&script.contains("-ExpectedDevice '"+Constant+"'")&&script.contains("-ExpectedImageSha256 '"+Image+"'")&&!script.contains("-ExpectedPid"),"configured expected tuple not a PID signal");
        const auto request=QRegularExpression("-RequestId '([^']+)'").match(script),reason=QRegularExpression("-ConfiguredReason '(AppClose|CardOff)'").match(script);need(request.hasMatch()&&reason.hasMatch(),"explicit configured reason and request");
        auto profile=configuredProfile()["profile"].toObject();if(mode=="profile-changed")profile["binding_sha256"]=QString(64,'C');
        output({{"protocol","pgn-configured-stop-1"},{"status","CONFIGURED_OFF_LATCHED"},{"request_id",request.captured(1)},{"reason",reason.captured(1)},{"profile",profile},{"physical_applied",false}});return 0;
    }
    if(mode=="slow-start"&&(action=="start"||action=="initial"))QThread::msleep(500);
    if(mode=="readonly-finite-timeout"&&action=="start")QThread::msleep(15000);
    if(action=="initial"){
        const auto admission=QRegularExpression("-InitialAdmission '([A-Za-z0-9+/=]+)'").match(script);need(admission.hasMatch(),"actual native admission in TESTSELF");
        const auto parsed=QJsonDocument::fromJson(QByteArray::fromBase64(admission.captured(1).toLatin1())).object();
        need(parsed.value("schema")=="pgn-initial-start-admission-1","actual admission schema");
        output({{"status","START_REQUESTED"},{"request_id",parsed["receipt"].toObject()["request_id"]},{"launch",QJsonObject{{"status","START_REQUESTED"}}}});return 0;
    }
    if(isFinite){
        const bool owned=mode=="finite-owned"||mode=="finite-changed";auto snapshot=finite(owned,action!="stop");
        if(mode.startsWith("readonly-finite")){
            snapshot=finite(false,false);
            if(mode=="readonly-finite-historical"||mode=="readonly-finite-exit-drift"){
                snapshot=finite(true,false);snapshot["owned"]=false;
                snapshot["last_exit"]=QJsonObject{{"run",mode=="readonly-finite-exit-drift"?Other:Run},{"exit_code",0},{"utc",Birth}};
            }
            if(mode=="readonly-finite-desired")snapshot["desired_enabled"]=true;
            if(mode=="readonly-finite-owned")snapshot=finite(true,false);
            if(mode=="readonly-finite-owned-string")snapshot["owned"]="false";
            if(mode=="readonly-finite-owned-missing")snapshot.remove("owned");
            if(mode=="readonly-finite-string")snapshot["desired_enabled"]="false";
            if(mode=="readonly-finite-missing")snapshot.remove("desired_enabled");
        }
        if(mode=="finite-bad")snapshot.remove("owned");
        if(mode=="finite-changed"&&action=="stop"){auto m=snapshot["manifest"].toObject();m["run"]=Other;snapshot["manifest"]=m;}
        if(mode=="finite-owned"&&action=="stop"){snapshot["owned"]=false;snapshot["stop_requested"]=true;}
        output(snapshot);return 0;
    }
    if(action=="snapshot"){
        need(script.contains(" -NoDriver"),"STOP preflight never waits for driver metrics");auto snapshot=process();
        if(mode.startsWith("readonly-constant")){
            snapshot["owned"]=false;snapshot["state"]="OFF";snapshot["operator_stop"]=true;
            snapshot["last_exit"]=QJsonObject{{"run",Run},{"exit_code",0},{"utc",Birth}};
            if(mode=="readonly-constant-owned"){snapshot["owned"]=true;snapshot["state"]="RUNNING";}
            if(mode=="readonly-constant-owned-string")snapshot["owned"]="false";
            if(mode=="readonly-constant-owned-missing")snapshot.remove("owned");
            if(mode=="readonly-constant-string")snapshot["operator_stop"]="true";
            if(mode=="readonly-constant-missing")snapshot.remove("operator_stop");
            if(mode=="readonly-constant-unlatched")snapshot["operator_stop"]=false;
            if(mode=="readonly-constant-exit-drift"){auto e=snapshot["last_exit"].toObject();e["run"]=Other;snapshot["last_exit"]=e;}
            if(mode=="readonly-constant-exit-string"){auto e=snapshot["last_exit"].toObject();e["exit_code"]="0";snapshot["last_exit"]=e;}
            if(mode=="readonly-constant-manifest"){auto m=snapshot["manifest"].toObject();m["pid"]="1234";snapshot["manifest"]=m;}
        }
        if(mode=="owner-unknown")return 2;
        if(mode=="process-bad")snapshot["observed_stop_protocol"]="legacy";
        if(mode=="cold"||mode=="cold-unlatched"||mode.startsWith("profile-")){snapshot["manifest"]=QJsonValue::Null;snapshot["owned"]=false;snapshot["state"]="OFF";snapshot["operator_stop"]=mode=="cold";}
        output(snapshot);return 0;
    }
    need(script.contains("-ExpectedRun '"+Run+"'")&&script.contains("-ExpectedPid '1234'")&&script.contains("-ExpectedBirth '"+Birth+"'")&&script.contains("-ExpectedImageSha256 '"+Image+"'"),"STOP exact run/PID/birth/image sent");
    output({{"protocol","pgn-stop-observed-1"},{"status","STOP_REQUESTED"},{"physical_applied",false},{"observed",QJsonObject{{"run",mode=="cas-changed"?Other:Run},{"pid","1234"},{"birth",Birth},{"image_sha256",Image}}}});return 0;
}
void ownSql(const QString& path,const char* text){sqlite3* db{};need(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"own temporary SQL open");const auto result=sqlite3_exec(db,text,nullptr,nullptr,nullptr);sqlite3_close(db);need(result==SQLITE_OK,"own temporary SQL statement");}
struct Fixture {
    QTemporaryDir temp;pgn::booking::BookingStore store;NativeApplicationWriter writer;SshObservationEndpoint endpoint;
    std::unique_ptr<NativeApplicationControl> control;QList<QJsonObject> packets;std::function<void(const QJsonObject&)> additional;
    Fixture(const char* mode="process",NativeControlTimeouts testTimeouts={10000,20000},int maximumAge=5000):store(temp.filePath("booking.sqlite")){
        need(temp.isValid()&&store.valid(),"own application SQLite");const auto key=temp.filePath("PUBLIC-TESTONLY.key");QFile f(key);need(f.open(QIODevice::WriteOnly)&&f.write("PUBLIC-TESTONLY\n")>0,"own public fake endpoint");f.close();
        endpoint={QCoreApplication::applicationFilePath(),key,"PUBLIC-TESTONLY.invalid","fixture",22};
        ownSql(temp.filePath("booking.sqlite"),"CREATE TABLE IF NOT EXISTS dispatch_requests(id TEXT PRIMARY KEY,start TEXT NOT NULL,end TEXT NOT NULL,device TEXT NOT NULL,state TEXT NOT NULL,predecessor TEXT,run TEXT,created_utc TEXT NOT NULL);");
        qputenv("PGN_APP_CONTROL_MODE",mode);qputenv("PGN_APP_CONTROL_MARKER",temp.filePath("marker.txt").toUtf8());
        control=std::make_unique<NativeApplicationControl>(writer,store,endpoint,testTimeouts,maximumAge);
        control->onPacket=[this](const auto& p){packets.append(p);if(additional)additional(p);};
    }
    int count(const QByteArray& line){QFile f(temp.filePath("marker.txt"));if(!f.open(QIODevice::ReadOnly))return 0;return f.readAll().count(line+'\n');}
    QJsonObject response(const QString& request){for(const auto& p:packets)if(p.value("type")=="control"&&p.value("request_id")==request)return p;return {};}
    bool submit(int n,const QString& device,const char* action){return control->submit({{"action",action},{"request_id",id(n)},{"device",device}});}
    QJsonObject prepareConstant(){
        using namespace pgn::booking;
        const QString start="184467440737095516260",length="1000000",end=addUnsigned(start,length);
        SourceRegistry registry;registry.url="https://PUBLIC-TESTONLY.invalid/ranges";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString("2026-10-15T00:00:00Z",Qt::ISODate);registry.ranges={{1000,start,end,"Open",{}}};
        check(store.queue(registry,start,end,Constant).isEmpty(),"own real native reservation");
        check(store.prepareInitialStart(id(200),Constant,{start,end,start,end,start,length},registry,{WorkBoundary::Ready,id(201)}).status==InitialStartStatus::Prepared,"own real SAME SQLite PREPARED");
        SshObservationFrame frame;frame.packet={{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},{"driver_valid",true},{"binding",QJsonObject{{"uuid",Constant}}},{"observed_utc","2026-10-15T00:00:00.000Z"},{"manifest",QJsonValue::Null}};
        check(control->rememberStartInventory(id(200),id(201),frame),"actual typed start inventory armed");
        return {{"action","start_prepared"},{"request_id",id(200)},{"device",Constant},{"observation_id",id(201)},{"base_run",QJsonValue::Null},{"base_birth",QJsonValue::Null},{"base_start",QJsonValue::Null},{"base_end",QJsonValue::Null}};
    }
};
qint64 mono(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
NativeCloseInitialObservation actualInitial(Fixture& f){
    bool frozen{};check(f.writer.prepareClose(id(600),[&](const auto& r){frozen=r.entered;})&&waitFor([&]{return frozen;}),"actual writer freeze accepted");
    NativeCloseInitialObservation value;value.closeId=id(600);value.inventoryId=id(601);value.epoch=1;value.beganMonotonicMs=mono();
    qputenv("PGN_APP_CONTROL_MODE","readonly-constant");bool constantDone{};SshObservationResult constantResult;SshSnapshotTransport constant;
    SshObservationRequest read;read.noDriver=true;constant.start(f.endpoint,read,[&](const auto& frame){value.constant=frame;return true;},[&](const auto& result){constantResult=result;constantDone=true;});
    check(waitFor([&]{return constantDone;})&&constantResult.status==SshObservationStatus::Complete&&constantResult.exitCode==0,"real own SELF Initial constant acquisition");
    qputenv("PGN_APP_CONTROL_MODE","readonly-finite");bool finiteDone{};NativeControlTransportResult finiteResult;NativeControlTransport finite;
    finite.start(NativeControlCommand::finite3060Snapshot(f.endpoint),{10000,20000},[&](const auto& result){finiteResult=result;finiteDone=true;});
    check(waitFor([&]{return finiteDone;})&&finiteResult.control.finiteObservation.has_value(),"real own SELF Initial finite acquisition");value.finite=*finiteResult.control.finiteObservation;
    return value;
}
void initialTicketCases(){
    for(const auto device:{Constant,Finite}){
        Fixture f("readonly-constant",{10000,20000},30000);auto value=actualInitial(f);
        check(f.control->rememberCloseInitial(value),"typed actual Initial recorded with unchanged production TTL30000");
        qputenv("PGN_APP_CONTROL_MODE",device==Constant?"readonly-constant":"readonly-finite");
        check(f.submit(610,device,"stop")&&waitFor([&]{return !f.response(id(610)).isEmpty();}),"normal STOP still enters ONE writer");
        const auto reply=f.response(id(610));
        std::cout<<"INITIAL_TICKET device="<<(device==Constant?"constant":"finite")<<" constant_reads="<<f.count("constant/snapshot")<<" finite_reads="<<f.count("finite/snapshot")<<" ttl=30000\n";
        check(reply.value("accepted")==true&&reply.value("applied")==false&&reply.value("physical_outcome")=="UNKNOWN"&&reply.value("desired_off_latched")!=true&&reply.value("result").toObject().value("status")=="ALREADY_OFF_OBSERVED","exact existing local no-op ABI, no latch or Ready");
        check(f.count("constant/snapshot")==1&&f.count("finite/snapshot")==1,"positive ticket really suppresses one extra STOP READ child; Initial reads remain paid");
        check(!f.control->rememberCloseInitial(value),"same Initial cannot reset consumed device allowance");
        check(f.submit(611,device,"stop")&&waitFor([&]{return !f.response(id(611)).isEmpty();}),"new STOP UUID cannot reuse spent ticket");
        check(f.count(device==Constant?"constant/snapshot":"finite/snapshot")==2,"spent ticket pays ordinary fresh READ");
    }
    for(const QByteArray poison:{"absent","stale","prepared","other-close","strings","start-seen"}){
        Fixture f("readonly-finite",{10000,20000},30000);
        if(poison=="start-seen"){check(f.submit(619,Finite,"start")&&waitFor([&]{return !f.response(id(619)).isEmpty();}),"prior START lifetime uncertainty retained");}
        auto value=actualInitial(f);
        if(poison=="other-close")value.closeId=id(699);
        if(poison=="strings")value.finite.packet["desired_enabled"]="false";
        if(poison=="stale")value.beganMonotonicMs=mono()-29000; // TESTONLY boundary input, no remote time altered
        if(poison!="absent")check(f.control->rememberCloseInitial(value)==(poison!="other-close"),"invalid close rejected; other typed observations only recorded, never trusted blindly");
        NativeApplicationWriter::Done release;check(f.writer.enqueue(NativeApplicationOperation::Read,[&](auto done){release=std::move(done);},[](const auto&){} )&&waitFor([&]{return bool(release);}),"hold exact writer before STOP entry");
        qputenv("PGN_APP_CONTROL_MODE","readonly-finite");check(f.submit(620,Finite,"stop"),"STOP invalidation/enqueue unchanged before decision");
        if(poison=="prepared")ownSql(f.temp.filePath("booking.sqlite"),"INSERT INTO dispatch_requests VALUES('PUBLIC-TESTONLY','1','2','GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265','PREPARED',NULL,NULL,'2026-10-15T00:00:00Z');");
        if(poison=="stale"){QElapsedTimer elapsed;elapsed.start();while(elapsed.elapsed()<1100){QCoreApplication::processEvents();QThread::msleep(1);}}
        release({});check(waitFor([&]{return !f.response(id(620)).isEmpty();}),"rejected ticket settles through ordinary path");
        check(f.count("finite/snapshot")==2,"absent/stale/PREPARED/wrong close/string/START-seen cannot suppress fresh READ");
    }
    {
        Fixture f("readonly-constant",{10000,20000},30000);const auto value=actualInitial(f);check(f.control->rememberCloseInitial(value),"record actual OFF Initial before external-state poison");
        qputenv("PGN_APP_CONTROL_MODE","process");check(f.submit(630,Constant,"stop")&&waitFor([&]{return !f.response(id(630)).isEmpty();}),"no-op reports only earlier OFF observation, not remote current state");
        bool done{};SshObservationFrame final;SshSnapshotTransport read;SshObservationRequest request;request.noDriver=true;
        read.start(f.endpoint,request,[&](const auto& frame){final=frame;return true;},[&](const auto&){done=true;});check(waitFor([&]{return done;}),"separate actual Final READ is still required");
        check(final.packet.value("owned")==true&&final.packet.value("state")=="RUNNING"&&f.response(id(630)).value("physical_outcome")=="UNKNOWN","external change remains visible to Final stop guards; no ticket Ready/applied");
        check(f.count("constant/snapshot")==2,"one Initial+one Final actual child, not a recycled final packet");
    }
}
void alreadyDisabledCases(){
    for(const auto mode:{"readonly-constant","readonly-finite","readonly-finite-historical"}){
        Fixture f(mode);const auto device=QByteArray(mode).contains("constant")?Constant:Finite;
        check(f.submit(300,device,"stop"),"already-disabled STOP still goes through local generation admission");
        check(waitFor([&]{return !f.response(id(300)).isEmpty();}),"fresh already-disabled read settles");
        const auto p=f.response(id(300)),result=p.value("result").toObject();
        check(p.value("accepted")==true&&p.value("applied")==false&&p.value("desired_off_latched")!=true,"no-op is observation, no mutation ACK/latch/Ready");
        check(result.value("status")=="ALREADY_OFF_OBSERVED"&&result.value("scope")=="READ_ONLY_PROFILE_OBSERVATION_NOT_COMMAND_ACK","existing no-op ABI avoids extra rediscovery without new authority");
        check((result.value("requires_fresh_observation")==true)==(device==Constant),"historical constant report requests separate observation, finite publishes its real typed snapshot");
        check(f.count(device==Constant?"constant/snapshot":"finite/snapshot")==1,"no-op always pays exactly one fresh read");
        check(f.count("constant/stop-observed")==0&&f.count("constant/stop-configured")==0&&f.count("finite/stop/cas")==0&&f.count("finite/stop/latch")==0,"already-disabled read sends no remote mutation");
    }
    for(const auto mode:{"readonly-constant-owned","readonly-constant-owned-string","readonly-constant-owned-missing","readonly-constant-string","readonly-constant-missing","readonly-constant-unlatched","readonly-constant-exit-drift","readonly-constant-exit-string","readonly-constant-manifest","readonly-finite-desired","readonly-finite-owned","readonly-finite-owned-string","readonly-finite-owned-missing","readonly-finite-string","readonly-finite-missing","readonly-finite-exit-drift"}){
        Fixture f(mode);const auto device=QByteArray(mode).contains("constant")?Constant:Finite;
        check(f.submit(301,device,"stop")&&waitFor([&]{return !f.response(id(301)).isEmpty();}),"poison settles without retry");
        check(f.response(id(301)).value("result").toObject().value("status")!="ALREADY_OFF_OBSERVED","missing/string/owned/unlatched/tuple drift cannot mint no-op");
    }
    for(const auto device:{Constant,Finite}){
        Fixture f(device==Constant?"readonly-constant":"readonly-finite");const auto command=f.prepareConstant();
        const auto before=f.store.savedInitialStartAdmission(id(200),Constant,id(201),{}).receiptUtf8;
        check(f.submit(302,device,"stop")&&waitFor([&]{return !f.response(id(302)).isEmpty();}),"real SAMEStore PREPARED cannot be ignored");
        check(f.response(id(302)).value("result").toObject().value("status")!="ALREADY_OFF_OBSERVED","PREPARED any-device inhibits no-op");
        check(f.store.savedInitialStartAdmission(id(200),Constant,id(201),{}).receiptUtf8==before,"STOP/no-op refusal preserves PREPARED bytes");
    }
    {
        Fixture f("readonly-constant");ownSql(f.temp.filePath("booking.sqlite"),"DROP TABLE dispatch_requests;");
        check(f.submit(303,Constant,"stop")&&waitFor([&]{return !f.response(id(303)).isEmpty();}),"missing SAMEStore dispatch schema falls back to normal STOP");
        check(f.response(id(303)).value("result").toObject().value("status")!="ALREADY_OFF_OBSERVED","unknown schema is not known absence");
    }
    for(const bool pending:{false,true}){
        Fixture f("cold");if(pending)f.prepareConstant();else ownSql(f.temp.filePath("booking.sqlite"),"DROP TABLE dispatch_requests;");
        check(f.submit(306,Constant,"stop")&&waitFor([&]{return !f.response(id(306)).isEmpty();}),"empty persisted-OFF profile still checks SAMEStore before its existing no-op");
        check(f.response(id(306)).value("result").toObject().value("status")!="ALREADY_OFF_OBSERVED"&&f.count("constant/stop-configured")==1,"empty profile cannot amnesty unknown schema or PREPARED");
    }
    {
        Fixture f("readonly-finite-timeout",{5000,6500});check(f.submit(304,Finite,"start"),"own fake START attempt admitted");
        check(waitFor([&]{return f.count("finite/start")==1;}),"own SELF entered before its timeout, no remote hardware");
        check(waitFor([&]{return !f.response(id(304)).isEmpty();}),"uncertain START settles once after deadline");
        check(f.response(id(304)).value("accepted")==false&&f.count("finite/start")==1,"timed-out START remains unconfirmed, never retried");
        check(f.submit(305,Finite,"stop")&&waitFor([&]{return !f.response(id(305)).isEmpty();}),"subsequent fresh OFF STOP still submits normal latch");
        check(f.response(id(305)).value("result").toObject().value("status")!="ALREADY_OFF_OBSERVED"&&f.count("finite/stop/latch")==1,"late START uncertainty cannot be amnestied by fresh OFF");
    }
}
void cancellationCases(){
    for(const bool full:{false,true}){
        Fixture f("finite-empty");NativeApplicationWriter::Done release;
        check(f.writer.enqueue(NativeApplicationOperation::Read,[&](auto done){release=done;},[](auto){}),"own writer blocker queued");
        check(waitFor([&]{return bool(release);}),"writer blocker actually entered");check(f.submit(100,Finite,"start"),"finite ON queued before STOP");
        if(full)for(qsizetype i=1;i<NativeApplicationWriter::PendingLimit;++i)check(f.writer.enqueue(NativeApplicationOperation::Read,[](auto done){done({});},[](auto){}),"fill only own bounded queue");
        check(f.submit(101,Finite,"stop")==!full,"STOP invalidates even when writer queue is full");release({});
        check(waitFor([&]{return !f.response(id(100)).isEmpty()&&!f.response(id(101)).isEmpty();}),"cancelled start and STOP settle locally");
        const auto cancelled=f.response(id(100));check(cancelled["accepted"]==false&&cancelled["cancelled_generation"]==true&&cancelled["physical_outcome"]=="UNKNOWN","queued ON explicitly cancelled not zero/ready");
        check(f.count("finite/start")==0&&!cancelled.contains("result"),"zero child ON after queued cancellation");
        if(full)check(f.count("finite/snapshot")==0&&f.count("finite/stop/latch")==0,"queue refusal never pretends STOP was sent");
        else {
            check(f.count("finite/stop/latch")==1,"actual separate configured OFF latch retained");
            NativeApplicationWriter::Done again;check(f.writer.enqueue(NativeApplicationOperation::Read,[&](auto done){again=done;},[](auto){}),"fresh explicit ON blocker");check(waitFor([&]{return bool(again);}),"fresh blocker entered");
            check(f.submit(102,Finite,"start"),"fresh explicit ON with new ID may follow STOP");check(!f.submit(101,Finite,"stop"),"replayed STOP does not invalidate new generation");again({});
            check(waitFor([&]{return !f.response(id(102)).isEmpty();}),"new explicit ON settles");check(f.response(id(102))["accepted"]==true&&f.count("finite/start")==1,"no permanent implicit OFF latch or duplicate cancellation");
        }
    }
    {
        Fixture f("finite-empty");NativeApplicationWriter::Done release;f.writer.enqueue(NativeApplicationOperation::Read,[&](auto done){release=done;},[](auto){});check(waitFor([&]{return bool(release);}),"per-device blocker entered");
        check(f.submit(110,Finite,"start")&&f.submit(111,Constant,"stop"),"other-card STOP admitted");release({});check(waitFor([&]{return !f.response(id(111)).isEmpty();}),"other-card sequence settles");
        check(f.response(id(110))["accepted"]==true&&f.count("finite/start")==1,"constant STOP must not cancel finite generation");
    }
    {
        Fixture f;const auto command=f.prepareConstant();const auto before=f.store.savedInitialStartAdmission(id(200),Constant,id(201),{}).receiptUtf8;
        NativeApplicationWriter::Done release;f.writer.enqueue(NativeApplicationOperation::Read,[&](auto done){release=done;},[](auto){});check(waitFor([&]{return bool(release);}),"prepared blocker entered");
        check(f.control->submit(command)&&f.submit(112,Constant,"stop"),"real prepared start then STOP admitted");release({});check(waitFor([&]{return !f.response(id(112)).isEmpty();}),"prepared cancellation and STOP settle");
        check(f.response(id(200))["cancelled_generation"]==true&&f.count("constant/initial")==0,"STOP integrates prepared generation BEFORE writer entry");
        check(f.store.savedInitialStartAdmission(id(200),Constant,id(201),{}).receiptUtf8==before,"durable PREPARED unchanged, no local cleanup pretending remote state");
    }
    {
        Fixture f("slow-start");check(f.submit(120,Finite,"start"),"own active finite ON submitted");check(waitFor([&]{return f.count("finite/start")==1;}),"SELF actually entered before STOP");
        check(f.submit(121,Finite,"stop"),"STOP invalidates without waiting for old ACK");check(waitFor([&]{return !f.response(id(121)).isEmpty();}),"active old result then STOP settle");
        const auto old=f.response(id(120));check(old["accepted"]==false&&old["cancelled_generation"]==true&&old["reported_outcome"]=="ObservationReported","old actual reply not local acceptance");
        check(old["result"].toObject()["desired_enabled"]==true&&old["physical_outcome"]=="UNKNOWN"&&f.count("finite/start")==1,"preserve original old wire report with zero retry");
    }
}
void processStop(){
    Fixture f;check(f.submit(1,Constant,"stop"),"explicit exact STOP queued");check(f.packets.isEmpty(),"no reentrant reply");
    check(waitFor([&]{return !f.response(id(1)).isEmpty();}),"actual native STOP composition settles");
    const auto p=f.response(id(1));check(p.value("accepted")==true&&p.value("applied")==false,"STOP report is not physical completion");
    check(p.value("result").toObject().value("observed").toObject().value("run")==Run,"exact identity retained");
    check(f.count("constant/snapshot")==1&&f.count("constant/stop-observed")==1&&f.count("constant/stop")==0,"one fresh process snapshot then one exact STOP, never legacy fallback");
    check(!f.submit(1,Constant,"stop"),"same request never resubmitted");check(f.count("constant/stop-observed")==1,"no duplicate stop send");
    check(!f.submit(2,Constant,"start"),"naked productive START cannot bypass SQLite admission");
    auto request=SshObservationRequest{};request.noDriver=true;const auto command=makeSshObservationCommand(f.endpoint,request);
    const auto wide=QByteArray::fromBase64(command.arguments.last().section(' ',-1).toLatin1());const auto script=QString::fromUtf16(reinterpret_cast<const char16_t*>(wide.constData()),wide.size()/2);
    check(script.endsWith(" -NoDriver"),"real encoded process snapshot opts out of metrics only");
    request.action=SshObservationAction::Stream;bool refused=false;try{makeSshObservationCommand(f.endpoint,request);}catch(const std::exception&){refused=true;}check(refused,"driver-free option cannot silently alter SOURCE stream");
}
void configuredStopCases(){
 for(const auto mode:{"cold-unlatched","owner-unknown","profile-missing","profile-legacy","profile-changed"}){
  Fixture f(mode);check(f.submit(150,Constant,"stop"),"configured OFF attempt queued");check(waitFor([&]{return !f.response(id(150)).isEmpty();}),"configured OFF acquisition/latch settles");
  const auto p=f.response(id(150));const bool good=QByteArray(mode)=="cold-unlatched"||QByteArray(mode)=="owner-unknown";
  check(p["accepted"]==false&&p["applied"]==false,"configured latch never accepted physical STOP");
  check(f.count("constant/snapshot")==1&&f.count("constant/configured-profile")==1&&f.count("constant/stop-observed")==0&&f.count("constant/stop")==0,"distinct profile acquisition, no observed/legacy fallback");
  check(f.count("constant/stop-configured")==int(good||QByteArray(mode)=="profile-changed"),"missing capability refuses before latch submission");
  check((p["desired_off_latched"]==true)==good,"only exact response reports local desired-OFF latch");
  if(good)check(p["reported_outcome"]=="ConfiguredOffLatchReported"&&p["physical_outcome"]=="UNKNOWN"&&p["result"].toObject()["status"]=="CONFIGURED_OFF_LATCHED","preserve latch proof level and raw response");
  for(const auto& published:f.packets)check(published["type"]!="native_control_observation"&&published["type"]!="device_control_snapshot","configured latch cannot fabricate an OFF sample or metrics");
 }
 {Fixture f("cas-changed");check(f.submit(151,Constant,"stop"),"observed CAS attempt queued");check(waitFor([&]{return !f.response(id(151)).isEmpty();}),"CAS rejection settles");
  check(f.response(id(151))["accepted"]==false&&f.count("constant/stop-observed")==1&&f.count("constant/configured-profile")==0&&f.count("constant/stop-configured")==0,"observed CAS failure NEVER falls back to profile latch");}
 {Fixture f("cold-unlatched");NativeBridge bridge;int acknowledgements{},metrics{},notices{};
  check(bridge.attachNativeRuntime(f.control.get(),[&](const auto& packet){f.control->submit(packet);return true;}),"attach real native bridge for latch semantics");
  f.additional=[&](const auto& p){check(bridge.acceptNativeRuntimePacket(f.control.get(),p),"only same-owner control packet accepted");};
  bridge.onControl=[&](const auto&,const auto&,bool applied,const auto&){++acknowledgements;check(!applied,"latch cannot trigger onControl appliedtrue");};bridge.onDevice=[&](const auto&){++metrics;};
  bridge.onStatus=[&](const auto& detail){if(detail.contains("desired-OFF latched only"))++notices;};
  bridge.control(Constant,id(152),false);check(waitFor([&]{return notices>0;}),"bridge exposes explicit held-latch status without resolving physical request");check(metrics==0&&acknowledgements==0,"no fake metric/OFF or process ACK inferred");}
 {Fixture f("cold-unlatched");bool frozen{};f.writer.prepareClose(id(153),[&](auto r){frozen=r.entered;});check(f.submit(154,Constant,"stop"),"AppClose configured OFF allowed under existing freeze");
  check(waitFor([&]{return frozen&&!f.response(id(154)).isEmpty();}),"AppClose latch settles");check(f.response(id(154))["result"].toObject()["reason"]=="AppClose","typed AppClose reason preserved");}
}
void failedAndCold(){
    for(const auto mode:{"process-bad","cold-unlatched"}){Fixture f(mode);check(f.submit(3,Constant,"stop"),"guarded STOP attempt admitted");
        check(waitFor([&]{return !f.response(id(3)).isEmpty();}),"guard refusal settles");check(f.response(id(3)).value("accepted")==false,"unsupported epoch/latch held");
        check(f.count("constant/snapshot")==1&&f.count("constant/stop-observed")==0&&f.count("constant/stop")==0,"invalid/empty-unlatched profile never falls back");}
    Fixture f("cold");NativeBridge bridge;bool resolved=false;int metrics{};
    check(bridge.attachNativeRuntime(f.control.get(),[&](const auto& p){f.control->submit(p);return true;}),"native control attached to real bridge");
    f.additional=[&](const auto& p){check(bridge.acceptNativeRuntimePacket(f.control.get(),p),"only own attached native runtime delivers");};
    bridge.onControl=[&](const auto& device,const auto& request,bool observed,const auto& detail){check(device==Constant&&request==id(4),"cold observation correlation exact");check(observed&&detail.contains("persisted STOP"),"cold profile OFF observed, not command ACK/GPU idle");resolved=true;};
    bridge.onDevice=[&](const auto&){++metrics;};
    bridge.control(Constant,id(4),false);check(waitFor([&]{return resolved;}),"empty native profile can settle its OFF button without invented run");
    check(f.count("constant/snapshot")==1&&f.count("constant/stop-observed")==0,"already OFF persisted STOP needs zero mutation");
    check(metrics==0,"empty profile observation never fabricates a zero/fresh GPU metric");
}
void finiteCases(){
    for(const auto mode:{"finite-owned","finite-empty","finite-bad","finite-changed"}){
        Fixture f(mode);check(f.submit(5,Finite,"stop"),"finite card OFF admitted");check(waitFor([&]{return !f.response(id(5)).isEmpty();}),"finite actual two-stage path settles");
        const bool bad=QByteArray(mode)=="finite-bad",changed=QByteArray(mode)=="finite-changed",owned=QByteArray(mode)=="finite-owned"||changed;
        const auto p=f.response(id(5));check(p.value("accepted")==(!bad&&!changed)&&p.value("applied")==false,"finite observation not action-request ACK");
        check(f.count("finite/snapshot")==1,"finite STOP always pays fresh actual inventory");
        check(f.count("finite/stop/cas")==int(owned&&!bad),"manifest case uses exact observed run CAS");
        check(f.count("finite/stop/latch")==int(!owned&&!bad),"explicit empty configured card uses separate latch only");
        if(changed)check(f.count("finite/stop/latch")==0,"failed observed-run stop NEVER falls back to empty latch");
        if(owned&&!changed){bool physical=false;for(const auto& packet:f.packets)if(packet.value("type")=="device_control_snapshot")physical=packet.value("snapshot").toObject().value("owned")==false;check(physical,"real typed post-control finite observation forwarded separately");}
    }
    Fixture f("finite-empty");check(f.submit(6,Finite,"start"),"explicit finite-only ON admitted");check(waitFor([&]{return !f.response(id(6)).isEmpty();}),"finite ON request returns");
    check(f.response(id(6)).value("accepted")==true&&f.response(id(6)).value("applied")==false,"finite enabled/no-process response is not a launch ACK");
    bool preserved=false;for(const auto& packet:f.packets)if(packet.value("type")=="device_control_snapshot"){const auto p=packet.value("snapshot").toObject();preserved=p.value("owned")==false&&p.value("desired_enabled")==true&&p.value("manifest").isNull();}
    check(preserved,"pending finite launch not hidden by fake stopped/disabled defaults");
}
void freezeAndLifetime(){
    {Fixture f("finite-empty");bool frozen=false;check(f.writer.prepareClose(id(7),[&](const auto& result){frozen=result.entered;}),"shared close barrier accepted");
        check(!f.submit(8,Finite,"start"),"freeze refuses new finite launch");check(f.submit(9,Constant,"stop")&&f.submit(10,Finite,"stop"),"both STOP paths remain queued under same freeze");
        check(waitFor([&]{return frozen&&!f.response(id(9)).isEmpty()&&!f.response(id(10)).isEmpty();}),"serialized two-card STOP and barrier settle");
        check(f.count("constant/stop-observed")==1&&f.count("finite/stop/latch")==1&&f.count("finite/start")==0,"no competing launch after freeze");}
    {Fixture f("slow");check(f.submit(11,Constant,"stop"),"own slow process acquisition queued");check(waitFor([&]{return f.count("constant/snapshot")==1;}),"own fake client actually entered");
        f.control.reset();bool done=false;check(f.writer.enqueue(NativeApplicationOperation::Read,[&](auto finish){done=true;finish({});},[](const auto&){}),"read queued after destroyed control");
        check(waitFor([&]{return done;}),"destructor releases own local writer attempt");check(f.packets.isEmpty(),"no callback/second command after destroyed control");}
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{if(app.arguments().size()>1&&app.arguments().at(1)=="-F")return fake(app.arguments());if(app.arguments().contains("--initial-ticket-only"))initialTicketCases();else if(app.arguments().contains("--already-disabled-only"))alreadyDisabledCases();else{processStop();failedAndCold();finiteCases();freezeAndLifetime();cancellationCases();configuredStopCases();}}catch(const std::exception& e){++failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
std::cout<<"NATIVE_APPLICATION_CONTROL "<<checks<<" checks / "<<failures<<" failures\n";return failures?1:0;}

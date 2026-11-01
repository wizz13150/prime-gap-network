#include "mining/native_prepared_control.h"
#include "booking/decimal_range.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <sqlite3.h>
#include <cstdio>
#include <iostream>
#include <memory>
using namespace pgn::mining;using namespace pgn::booking;
namespace {
int checks{},failures{};
void check(bool b){++checks;if(!b){++failures;std::cerr<<"prepared-control check "<<checks<<" failed\n";}}
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",Request="11111111-2222-4333-8444-555555555555",Observation="12345678-1234-4567-89ab-123456789abc",Close="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
const QString Start="184467440737095516260",Length="1000000",End=addUnsigned(Start,Length);
const QString Old(32,'a'),Next(32,'b'),Birth="2026-10-15T00:00:00.0000000Z";
void pump(){for(int i=0;i<32;++i)QCoreApplication::processEvents();}
void waitFor(const std::function<bool()>& done){QElapsedTimer timer;timer.start();while(!done()&&timer.elapsed()<5000){QCoreApplication::processEvents();QThread::msleep(1);}check(done());}
// Only this very TEST executable is ever launched. It decodes real generated
// SSH argv and the real native receipt but neither SSH nor PS nor GPU runs.
int fake(const QStringList& args){
    const auto key=args.value(args.indexOf("-i")+1);QFile calls(key+".calls");if(!calls.open(QIODevice::Append))return 7;calls.write("1\n");calls.close();
    const auto remote=args.last();const auto raw=QByteArray::fromBase64(remote.mid(remote.lastIndexOf(' ')+1).toLatin1());QString script;
    if(raw.size()%2)return 8;for(int i=0;i<raw.size();i+=2)script+=QChar(quint8(raw[i])|(quint16(quint8(raw[i+1]))<<8));
    const auto match=QRegularExpression("-(Initial|Resume)Admission '([A-Za-z0-9+/=]+)'").match(script);if(!match.hasMatch())return 9;
    const bool resume=match.captured(1)=="Resume";
    const auto admission=QJsonDocument::fromJson(QByteArray::fromBase64(match.captured(2).toLatin1())).object();const auto receipt=admission.value("receipt").toObject();
    if(admission.value("schema")!=(resume?"pgn-checkpoint-resume-admission-1":"pgn-initial-start-admission-1")||receipt.value("request_id")!=(resume?Close:Request))return 10;
    const auto mode=args.value(args.size()-2);if(mode=="slow")QThread::msleep(2000);
    QJsonObject reply{{"status",mode=="held"?(resume?"CHECKPOINT_RESUME_HELD":"INITIAL_START_HELD"):"START_REQUESTED"},{"request_id",resume?Close:Request}};
    if(mode=="held")reply["error"]="Closed fixture hold";else reply["launch"]=QJsonObject{{"status","START_REQUESTED"}};
    const auto bytes=QJsonDocument(reply).toJson(QJsonDocument::Compact);
    std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);std::fflush(stdout);return mode=="failed"?2:0;
}
struct Fixture {
    QTemporaryDir temp;QString dbPath,key,booking;BookingStore store;NativeApplicationWriter writer;SshObservationEndpoint endpoint;
    QJsonObject command;SshObservationFrame frame;QList<QJsonObject> replies;
    Fixture():dbPath(temp.filePath("booking.sqlite")),key(temp.filePath("PUBLIC-NOT-A-KEY.txt")),store(dbPath){
        check(temp.isValid()&&store.valid());QFile f(key);check(f.open(QIODevice::WriteOnly));f.write("PUBLIC TEST ONLY");f.close();
        endpoint={QCoreApplication::applicationFilePath(),key,"ok","fixture",22};
        SourceRegistry registry;registry.url="https://PUBLIC-FIXTURE.invalid/ranges";registry.sha256=QString(64,'1');registry.observedUtc=QDateTime::fromString("2026-10-15T00:00:00Z",Qt::ISODate);registry.ranges={{1000,Start,End,"Open",{}}};
        check(store.queue(registry,Start,End,Device).isEmpty());const auto prepared=store.prepareInitialStart(Request,Device,{Start,End,Start,End,Start,Length},registry,{WorkBoundary::Ready,Observation});check(prepared.status==InitialStartStatus::Prepared);
        if(prepared.dispatch)booking=prepared.dispatch->bookingId;
        command={{"action","start_prepared"},{"request_id",Request},{"device",Device},{"observation_id",Observation},{"base_run",QJsonValue::Null},{"base_birth",QJsonValue::Null},{"base_start",QJsonValue::Null},{"base_end",QJsonValue::Null}};
        frame.packet={{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"owned",false},{"driver_valid",true},{"binding",QJsonObject{{"uuid",Device}}},{"observed_utc","2026-10-15T00:00:00.000Z"},{"manifest",QJsonValue::Null},{"has_checkpoint",false}};
        frame.pendingBytes=0;
    }
    std::unique_ptr<NativePreparedControl> controller(int age=30000,NativeControlTimeouts timeout={1000,3000}){
        auto c=std::make_unique<NativePreparedControl>(writer,store,endpoint,timeout,age);c->onPacket=[this](auto p){replies.append(p);};return c;
    }
    int calls(){QFile f(key+".calls");if(!f.open(QIODevice::ReadOnly))return 0;return f.readAll().count('\n');}
    void sql(const QByteArray& statement){sqlite3* db{};check(sqlite3_open_v2(dbPath.toUtf8().constData(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK);check(sqlite3_exec(db,statement.constData(),nullptr,nullptr,nullptr)==SQLITE_OK);sqlite3_close(db);}
    void resume(){
        sql(("UPDATE dispatch_requests SET state='STARTED',run='"+Old+"' WHERE id='"+booking+"'; UPDATE bookings SET state='DISPATCHED' WHERE id='"+booking+"'").toUtf8());
        check(store.observeRuntime(Old,Start,End,Device,false).isEmpty());
        const QJsonObject manifest{{"schema","pgn-native-process-1"},{"run",Old},{"uuid",Device},{"pid",4242},{"birth",Birth},{"start",Start},{"end",End},{"queued_intent",booking},{"stdout","C:/PUBLIC-TESTONLY/old.stdout"}};
        check(store.saveSourceRunManifest(Device,manifest).isEmpty());
        StoredSourceResume cp;cp.checkpointBytes="PGNCG001";for(const quint64 word:{100ULL,10ULL,1000000ULL,123ULL,9007199254741995ULL,41ULL,17ULL})for(int i=0;i<8;++i)cp.checkpointBytes.append(char((word>>(8*i))&255));
        auto& r=cp.resume;r.run=Old;r.start=Start;r.end=End;r.length=Length;r.positions="123";r.nextStart=addUnsigned(Start,"123");r.remaining="999877";r.tests="9007199254741995";r.gaps="41";r.batches="17";r.counterTimeNs="9007199254742995";r.checkpointLabel="C:/PUBLIC-TESTONLY/checkpoint";r.checkpointSha256=QString::fromLatin1(QCryptographicHash::hash(cp.checkpointBytes,QCryptographicHash::Sha256).toHex());
        check(store.saveResumeCheckpoint(Device,r,cp.checkpointBytes).isEmpty());check(store.prepareCheckpointResume(Close,Device,booking,Old,Next,r.checkpointSha256,Observation).status==CheckpointResumeAdmissionStatus::Prepared);
        command={{"action","resume_prepared"},{"request_id",Close},{"device",Device},{"observation_id",Observation},{"booking_id",booking},{"old_run",Old},{"new_run",Next},{"base_birth",Birth},{"base_start",Start},{"base_end",End}};
        frame.packet["manifest"]=manifest;frame.packet["has_checkpoint"]=true;
    }
};
void transportScenarios(){
    for(const auto& mode:QStringList{"held","failed","slow"}){
        Fixture f;f.endpoint.host=mode;const bool timeout=mode=="slow";
        // Hold/failed-response cases must reach the SELF body. A deadline case
        // may expire before OR after its body starts; neither proves a remote
        // application. Do not mislabel local process initialization as retry.
        auto c=f.controller(30000,timeout?NativeControlTimeouts{1000,1200}:NativeControlTimeouts{5000,10000});check(c->rememberStartInventory(Request,Observation,f.frame));check(c->submit(f.command));waitFor([&]{return !f.replies.isEmpty();});
        const int entered=f.calls();if(!timeout&&entered!=1)std::cerr<<"SELF mode="<<mode.toStdString()<<" calls="<<entered<<" packet="<<QJsonDocument(f.replies.value(0)).toJson(QJsonDocument::Compact).constData()<<'\n';
        check((timeout?entered<=1:entered==1)&&f.replies.value(0).value("accepted")==false&&f.replies.value(0).value("applied")==false&&!f.replies.value(0).value("error").toString().isEmpty()&&!f.replies.value(0).contains("result"));
        if(timeout)check(f.replies.value(0).value("error").toString().contains("deadline"));
        check(!c->rememberStartInventory(Request,Observation,f.frame));pump();check(f.calls()==entered);
    }
}
void cancellationScenarios(){
    {
        Fixture f;auto c=f.controller();check(c->rememberStartInventory(Request,Observation,f.frame));
        const auto before=f.store.savedInitialStartAdmission(Request,Device,Observation,{});
        c->invalidateQueuedStarts();check(!c->submit(f.command));
        check(!c->rememberStartInventory(Request,Observation,f.frame));
        check(!c->rememberStartInventory(Close,Observation,f.frame));
        check(f.calls()==0&&f.store.savedInitialStartAdmission(Request,Device,Observation,{}).receiptUtf8==before.receiptUtf8);
    }
    for(const bool resume:{false,true}){
        Fixture f;if(resume)f.resume();
        const auto request=resume?Close:Request;const auto saved=f.store.savedCheckpointResumeEpoch(Device,Next);
        const auto initial=f.store.savedInitialStartAdmission(Request,Device,Observation,{});
        NativeApplicationWriter::Done release;
        check(f.writer.enqueue(NativeApplicationOperation::Read,[&](auto done){release=done;},[](auto){}));pump();check(bool(release));
        auto c=f.controller();check(c->rememberStartInventory(request,Observation,f.frame));check(c->submit(f.command));
        c->invalidateQueuedStarts();release({});pump();
        check(f.calls()==0&&f.replies.size()==1&&f.replies[0].value("accepted")==false&&f.replies[0].value("cancelled_generation")==true);
        check(f.replies[0].value("physical_outcome")=="UNKNOWN"&&!f.replies[0].contains("result"));
        if(resume)check(f.store.savedCheckpointResumeEpoch(Device,Next).receiptUtf8==saved.receiptUtf8);
        else check(f.store.savedInitialStartAdmission(Request,Device,Observation,{}).receiptUtf8==initial.receiptUtf8);
        check(!c->rememberStartInventory(request,Observation,f.frame));
    }
    for(const bool resume:{false,true}){
        Fixture f;if(resume)f.resume();f.endpoint.host="slow";auto c=f.controller(30000,{5000,10000});
        check(c->rememberStartInventory(resume?Close:Request,Observation,f.frame));check(c->submit(f.command));
        waitFor([&]{return f.calls()==1;});check(f.replies.isEmpty());c->invalidateQueuedStarts();
        waitFor([&]{return !f.replies.isEmpty();});const auto reply=f.replies.value(0);
        check(f.calls()==1&&reply.value("accepted")==false&&reply.value("cancelled_generation")==true&&reply.value("applied")==false);
        check(reply.value("reported_outcome")=="RequestReported"&&reply.value("result").toObject().value("status")=="START_REQUESTED");
        check(reply.value("physical_outcome")=="UNKNOWN");pump();check(f.calls()==1);
    }
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(app.arguments().contains("-F"))return fake(app.arguments());try{
    if(app.arguments().contains("--transport-only")){transportScenarios();std::cout<<"NATIVE_PREPARED_TRANSPORT "<<checks<<" checks / "<<failures<<" failures\n";return failures?1:0;}
    {
        Fixture f;auto c=f.controller();check(c->rememberStartInventory(Request,Observation,f.frame));
        auto bad=f.command;bad["extra"]="not admitted";check(!c->submit(bad));check(f.calls()==0);f.replies.clear();
        bad=f.command;bad["base_run"]=QString(32,'a');check(!c->submit(bad));check(f.calls()==0);f.replies.clear();
        check(c->submit(f.command));check(f.calls()==0);waitFor([&]{return !f.replies.isEmpty();});
        check(f.calls()==1&&f.replies.size()==1);const auto p=f.replies.value(0);check(p.value("accepted")==true&&p.value("applied")==false&&p.value("result").toObject().value("status")=="START_REQUESTED");
        check(!c->submit(f.command));check(!c->rememberStartInventory(Request,Observation,f.frame));pump();check(f.calls()==1);
        check(!c->rememberStartInventory(Close,Observation,f.frame)); // nonce cannot be rebound to another request
        check(f.store.savedInitialStartAdmission(Request,Device,Observation,{}).status==InitialStartAdmissionReadStatus::Present);
    }
    {
        Fixture f;auto c=f.controller();check(c->rememberStartInventory(Request,Observation,f.frame));check(c->submit(f.command));bool closed{};
        check(f.writer.prepareClose(Close,[&](auto r){closed=r.entered&&r.error.isEmpty();}));pump();check(closed&&f.calls()==0&&f.replies.size()==1&&f.replies.value(0).value("accepted")==false);
    }
    {
        Fixture f;NativeApplicationWriter::Done blocker;check(f.writer.enqueue(NativeApplicationOperation::Read,[&](auto done){blocker=done;},[](auto){}));pump();
        auto c=f.controller(1);check(c->rememberStartInventory(Request,Observation,f.frame));check(c->submit(f.command));QThread::msleep(3);blocker({});pump();check(f.calls()==0&&f.replies.size()==1&&f.replies.value(0).value("accepted")==false);
    }
    {
        Fixture f;auto c=f.controller();check(c->rememberStartInventory(Request,Observation,f.frame));
        f.sql("UPDATE dispatch_requests SET state='FAILED'");check(c->submit(f.command));pump();check(f.calls()==0&&f.replies.size()==1&&f.replies.value(0).value("accepted")==false);
    }
    {
        Fixture f;
        // Keep actual production WAL policy. These valid completed fixture
        // bookings are outside the selected range and count zero active work;
        // their real typed SQL validation pays the elapsed admission cost.
        f.sql("WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM n WHERE i<50000) INSERT INTO bookings(id,start,end,start_key,end_key,state,kind,device,created_utc) SELECT printf('%032x',i+1000),cast(i*2 as text),cast(i*2+1 as text),printf('%03d',length(cast(i*2 as text)))||cast(i*2 as text),printf('%03d',length(cast(i*2+1 as text)))||cast(i*2+1 as text),'SOURCE_FINISHED','RUNTIME','GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac','PUBLIC-TESTONLY-UNCHANGED' FROM n");
        check(f.store.savedInitialStartAdmission(Request,Device,Observation,{}).status==InitialStartAdmissionReadStatus::Present);
        auto c=f.controller(10);check(c->rememberStartInventory(Request,Observation,f.frame));check(c->submit(f.command));pump();
        check(f.calls()==0&&f.replies.size()==1&&f.replies.value(0).value("accepted")==false);
        check(f.replies.value(0).value("error").toString().startsWith("Prepared observation expired during admission"));
    }
    transportScenarios();cancellationScenarios();
    {
        Fixture f;auto c=f.controller();check(c->rememberStartInventory(Request,Observation,f.frame));check(c->submit(f.command));c.reset();bool closed{};check(f.writer.prepareClose(Close,[&](auto r){closed=r.entered;}));pump();check(closed&&f.calls()==0&&f.replies.isEmpty());
    }
    {
        Fixture f;f.endpoint.host="slow";auto c=f.controller();check(c->rememberStartInventory(Request,Observation,f.frame));check(c->submit(f.command));waitFor([&]{return f.calls()==1;});
        c.reset();bool localBarrier{};check(f.writer.prepareClose(Close,[&](auto r){localBarrier=r.entered;}));pump();
        check(localBarrier&&f.replies.isEmpty()&&f.calls()==1); // NO physical-quit permission inferred
        check(f.store.savedInitialStartAdmission(Request,Device,Observation,{}).status==InitialStartAdmissionReadStatus::Present);
    }
    {
        Fixture f;auto c=f.controller();for(const auto& field:QStringList{"manifest","driver_valid","owned","observed_utc","binding"}){auto frame=f.frame;frame.packet.remove(field);check(!c->rememberStartInventory(Request,Observation,frame));}
        auto frame=f.frame;frame.packet["owned"]=true;check(!c->rememberStartInventory(Request,Observation,frame));
    }
    {
        Fixture f;f.resume();auto c=f.controller();check(c->rememberStartInventory(Close,Observation,f.frame));check(c->submit(f.command));waitFor([&]{return !f.replies.isEmpty();});
        check(f.calls()==1&&f.replies.value(0).value("accepted")==true&&f.replies.value(0).value("applied")==false);
    }
    {
        Fixture f;f.resume();auto c=f.controller();check(c->rememberStartInventory(Close,Observation,f.frame));
        // Immutable epoch alone is STILL present. Current prepared-state proof
        // must refuse after actual LOCAL/dispatch state changes before entry.
        f.sql("UPDATE dispatch_requests SET state='FAILED'");check(f.store.savedCheckpointResumeEpoch(Device,Next).status==CheckpointResumeReadStatus::Present);
        check(c->submit(f.command));pump();check(f.calls()==0&&f.replies.size()==1&&f.replies.value(0).value("accepted")==false);
    }
    std::cout<<"NATIVE_PREPARED_CONTROL "<<checks<<" checks / "<<failures<<" failures\n";return failures?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

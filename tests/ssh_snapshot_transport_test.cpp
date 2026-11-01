#include "mining/ssh_snapshot_transport.h"
#include "mining/native_runtime_configuration.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVariant>
#include <cstdio>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace pgn::mining;
namespace {
int checks=0;
void require(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
template<class F>void rejected(F fn,const char* what){bool held=false;try{fn();}catch(const std::exception&){held=true;}require(held,what);}
QJsonObject packet(qint64 offset=0,const QByteArray& bytes={}){
    return {{"schema","pgn-runtime-snapshot-1"},{"state","OFF"},{"manifest",QJsonValue::Null},
        {"offset",offset},{"next_offset",offset+bytes.size()},{"chunk",QString::fromLatin1(bytes.toBase64())},
        {"pending_bytes",0},{"telemetry_lines",QJsonArray{}}};
}
QByteArray wire(const QJsonObject& p,bool pretty=false){return QJsonDocument(p).toJson(pretty?QJsonDocument::Indented:QJsonDocument::Compact);}
void output(const QByteArray& bytes,bool error=false){std::fwrite(bytes.constData(),1,bytes.size(),error?stderr:stdout);std::fflush(error?stderr:stdout);}
int fixture(){
    // This executable DOES NOT IMPLEMENT SSH. It only observes received argv
    // and emits synthetic source packets. No socket/GPU/Node/UI/key operation.
    const auto mode=qEnvironmentVariable("PGN_SSH_FIXTURE_MODE");
    if(mode=="hardware"){
        output(wire({{"schema","pgn-native-hardware-observation-1"},{"driver_valid",true},
            {"observed_utc","2026-10-15T17:00:00.000Z"},{"gpus",QJsonArray{"CLOSED VENDOR CSV FIXTURE"}}}));return 0;
    }
    QJsonArray args;for(const auto& arg:QCoreApplication::arguments().mid(1))args.append(arg);
    auto p=packet();p.insert("fixture_args",args);p.insert("fixture_pid",QString::number(QCoreApplication::applicationPid()));
    if(mode=="wait"){QThread::msleep(3000);return 0;}
    if(mode=="stderr"){output(QByteArray(SshSnapshotTransport::StderrLimit+100,'e'),true);return 0;}
    if(mode=="oversize"){output(QByteArray(SshSnapshotTransport::FrameLimit+1,'x'));return 0;}
    if(mode=="badjson"){output("{bad JSON}\n");return 0;}
    if(mode=="exit"){output(wire(p)+"\n");output("closed fixture error",true);return 7;}
    if(mode=="empty")return 0;
    if(mode=="partial"){output(wire(p));return 0;}
    if(mode=="prefixpartial"){output(wire(p)+"\n{\"schema\":");return 0;}
    if(mode=="multi"){output(wire(p)+"\r\n"+wire(p)+"\n");return 0;}
    if(mode=="splitstream"){
        p.insert("fixture_text",QString::fromUtf8("\xc3\xa9\xe6\xbc\xa2"));
        const auto bytes=wire(p)+"\n";const auto split=bytes.indexOf(QByteArray("\xc3\xa9"))+1;
        if(split<1||split>=bytes.size()){output("fixture did not locate a real UTF8 split",true);return 9;}
        output(bytes.left(split));QThread::msleep(12);output(bytes.mid(split));return 0;
    }
    if(mode=="large"){
        QByteArray bytes(65536,Qt::Uninitialized);for(qsizetype i=0;i<bytes.size();++i)bytes[i]=char(i%251);
        p=packet(9007199254740993LL,bytes);p.insert("fixture_args",args);
        output(wire(p));return 0;
    }
    const auto bytes=wire(p,true); // Actual snapshot contract is MULTILINE.
    if(mode=="split"){
        qsizetype at=0;for(const int count:{1,2,3,17}){output(bytes.mid(at,count));at+=count;QThread::msleep(3);}output(bytes.mid(at));
    }else output(bytes);
    return 0;
}
struct Execution {SshObservationResult result;QList<SshObservationFrame> frames;};
Execution execute(const SshObservationEndpoint& endpoint,SshObservationRequest request,const char* mode,bool accept=true,bool cancel=false){
    qputenv("PGN_SSH_FIXTURE_MODE",mode);QEventLoop loop;QTimer watchdog;watchdog.setSingleShot(true);
    SshSnapshotTransport client;Execution out;bool completed=false;
    QObject::connect(&watchdog,&QTimer::timeout,&loop,[&]{client.cancel();loop.quit();});
    client.start(endpoint,request,[&](const auto& frame){out.frames.push_back(frame);return accept;},[&](const auto& result){out.result=result;completed=true;loop.quit();});
    if(cancel)QTimer::singleShot(40,&loop,[&]{client.cancel();});
    watchdog.start(8000);if(!completed)loop.exec();require(completed,"real QProcess attempt completes within fixture watchdog");
    rejected([&]{client.start(endpoint,request,[](const auto&){return true;},[](const auto&){});},"no internal reconnect/reuse");
    return out;
}
int liveHardware(const QStringList& arguments){
    // Explicit root-only live read, never invoked by the closed SELF suite.
    if(arguments.size()!=3||arguments[1]!="--live-hardware")return 2;
    try{
        const auto unused=QDir::tempPath()+"/pgn-hardware-probe-unused-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        const NativeRuntimePaths paths{unused+"/bookings.sqlite",unused+"/source.sqlite",unused+"/mirror",unused+"/modules.sqlite","C:/PGN/runtime/current-job/checkpoint"};
        const auto decoded=loadNativeRuntimeConfiguration(arguments[2],"C:/Windows/System32/OpenSSH/ssh.exe",paths);
        if(!decoded.available())throw std::runtime_error(decoded.error.toStdString());
        SshObservationRequest request;request.action=SshObservationAction::Hardware;
        QEventLoop loop;SshSnapshotTransport transport;bool completed=false;SshObservationResult result;QJsonObject packet;
        transport.start(decoded.configuration->endpoint,request,[&](const auto& frame){packet=frame.packet;return true;},[&](const auto& value){result=value;completed=true;loop.quit();});
        if(!completed)loop.exec();
        const bool known=result.status==SshObservationStatus::Complete&&result.exitCode==0&&!packet.isEmpty();
        output(wire({{"schema","pgn-live-hardware-probe-1"},{"known",known},{"packet",packet},
            {"error",result.error},{"local_exit_code",result.exitCode?QJsonValue(*result.exitCode):QJsonValue(QJsonValue::Null)},
            {"source_history_opened",false},{"requested_mutations",0}}));return known?0:2;
    }catch(const std::exception& e){output(e.what(),true);return 2;}
}
int liveReadonly(const QStringList& arguments){
    // This mode is NEVER part of the default SELF suite. It has one explicit
    // remote read attempt, no database/session/stream/control/UI construction.
    // Only root/operator executes it; all ordinary tests remain closed SELF.
    QJsonObject report{{"schema","pgn-live-readonly-ssh-probe-1"},{"scope","ONE_SNAPSHOT_TRANSPORT_AND_TYPED_IDENTITY_ONLY_NOT_UI_READY_SOURCE_CONSERVATION_OR_CONTROL"},{"observation_known",false},{"transport_known",false},{"status","Unknown"},{"error",""},{"no_driver",false},{"startup_timeout_ms",5000},{"idle_timeout_ms",15000},{"snapshot_timeout_ms",30000},{"source_history_opened",false},{"requested_mutations",0}};
    QElapsedTimer total;total.start();std::optional<qint64> frameElapsed,completionElapsed;std::optional<SshObservationFrame> observed;
    try{
        if(arguments.size()!=5||(arguments.at(1)!="--live-readonly"&&arguments.at(1)!="--live-stream-first"&&arguments.at(1)!="--live-readonly-no-driver"))throw std::runtime_error("Exact readonly probe/config/offset/run required; no connection attempted");
        const bool streamFirst=arguments.at(1)=="--live-stream-first";
        const bool noDriver=arguments.at(1)=="--live-readonly-no-driver";report["no_driver"]=noDriver;
        if(noDriver)report["scope"]="ONE_DRIVER_FREE_SNAPSHOT_TRANSPORT_AND_TYPED_IDENTITY_NOT_PHYSICAL_DRIVER_UI_READY_SOURCE_CONSERVATION_OR_CONTROL";
        if(streamFirst)report["scope"]="ONE_TYPED_STREAM_FRAME_ONLY_NO_EOF_COMPLETION_OR_SOURCE_CONSERVATION";
        const auto offsetText=arguments.at(3),expected=arguments.at(4);bool converted=false;const auto offset=offsetText.toLongLong(&converted);
        if(!converted||offset<0||QString::number(offset)!=offsetText||expected.size()!=32||!QRegularExpression("^[0-9a-f]{32}$").match(expected).hasMatch())throw std::runtime_error("Canonical nonnegative signed64 offset and exact lowercase run32 required; no connection attempted");
        // The real Main executable observed for ROOT560 uses this exact SSH
        // program, not a PATH alias or a PowerShell GUI invocation wrapper.
        const QString program="C:/Windows/System32/OpenSSH/ssh.exe";
        // Loader requires explicit paths, but this endpoint-only probe NEVER
        // creates/opens them. Unique non-created TEMP names avoid production
        // database/mirror access even during the loader's metadata checks.
        const auto unused=QDir::tempPath()+"/pgn-readonly-probe-unused-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        const NativeRuntimePaths paths{unused+"/bookings.sqlite",unused+"/source.sqlite",unused+"/mirror",unused+"/modules.sqlite","C:/PGN/runtime/current-job/checkpoint"};
        const auto decoded=loadNativeRuntimeConfiguration(arguments.at(2),program,paths);
        if(!decoded.available())throw std::runtime_error(("Configuration unavailable: "+decoded.errorCode+": "+decoded.error).toStdString());
        const auto& config=*decoded.configuration;report.insert("configuration_sha256",config.sourceSha256);report.insert("ssh_executable",config.endpoint.executable);report.insert("host",config.endpoint.host);report.insert("user",config.endpoint.user);report.insert("port",config.endpoint.port);
        SshObservationRequest request;request.action=streamFirst?SshObservationAction::Stream:SshObservationAction::Snapshot;request.offset=offset;request.expectedRun=expected;request.noDriver=noDriver;
        report.insert("requested_offset",offset);report.insert("expected_run",expected);report.insert("setup_elapsed_ms",total.elapsed());
        QEventLoop loop;SshSnapshotTransport transport;SshObservationResult result;bool completed=false;
        // No extra watchdog/retry/budget override: measure the exact production
        // transport through its OWN snapshot deadline and clean EOF/exit0.
        QElapsedTimer attempt;attempt.start();
        QJsonArray trace;
        transport.start(config.endpoint,request,[&](const SshObservationFrame& frame){frameElapsed=attempt.elapsed();observed=frame;if(streamFirst)QTimer::singleShot(0,&transport,[&]{transport.cancel();});return true;},[&](const SshObservationResult& value){completionElapsed=attempt.elapsed();result=value;completed=true;loop.quit();},[&](const SshObservationTrace& event){trace.append(event.json());});
        if(!completed)loop.exec();
        const bool transportKnown=completed&&result.status==SshObservationStatus::Complete&&result.exitCode&&*result.exitCode==0&&result.completeFrames==1&&observed.has_value();
        report.insert("transport_known",transportKnown);report.insert("transport_elapsed_ms",attempt.elapsed());report.insert("complete_frames",result.completeFrames);report.insert("local_exit_code",result.exitCode?QJsonValue(*result.exitCode):QJsonValue(QJsonValue::Null));report.insert("stderr_bytes",result.standardError.size());report.insert("stderr_base64",QString::fromLatin1(result.standardError.toBase64()));report.insert("error",result.error.left(512));
        switch(result.status){case SshObservationStatus::Complete:report.insert("transport_status","Complete");break;case SshObservationStatus::Failed:report.insert("transport_status","Failed");break;case SshObservationStatus::TimedOut:report.insert("transport_status","TimedOut");break;case SshObservationStatus::Cancelled:report.insert("transport_status","Cancelled");break;}
        bool typedIdentity=false,driverValid=false;
        if(observed){
            const auto& p=observed->packet;const auto m=p.value("manifest").toObject(),b=p.value("binding").toObject();const auto own=p.value("owned"),state=p.value("state"),driver=p.value("driver_valid");const QString device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
            typedIdentity=p.value("schema")=="pgn-runtime-snapshot-1"&&m.value("schema")=="pgn-native-process-1"&&m.value("run")==expected&&m.value("uuid").toString().compare(device,Qt::CaseInsensitive)==0&&b.value("uuid").toString().compare(device,Qt::CaseInsensitive)==0&&own.isBool()&&state.isString()&&(own.toBool()?(state=="RUNNING"||state=="STOPPING"):state=="OFF");
            driverValid=driver.isBool()&&driver.toBool();report.insert("driver_known",driver.isBool());report.insert("driver_valid",driver.isBool()?driver:QJsonValue(QJsonValue::Null));report.insert("observed_run",m.value("run"));report.insert("observed_utc",p.value("observed_utc"));report.insert("state",state);report.insert("owned",own);report.insert("offset",observed->offset);report.insert("next_offset",observed->nextOffset);report.insert("pending_bytes",observed->pendingBytes?QJsonValue(*observed->pendingBytes):QJsonValue(QJsonValue::Null));report.insert("chunk_bytes",observed->chunk.size());report.insert("decoded_compact_json_bytes",wire(p).size());
        }else{report.insert("driver_known",false);report.insert("driver_valid",QJsonValue::Null);}
        const bool firstFrameKnown=streamFirst&&observed&&result.completeFrames>=1&&result.status==SshObservationStatus::Cancelled;
        const bool known=(transportKnown||firstFrameKnown)&&typedIdentity&&driverValid;report.insert("typed_identity_known",typedIdentity);report.insert("observation_known",known);report.insert("status",known?"ObservationKnown":"Unknown");
        const bool driverFreeKnown=noDriver&&transportKnown&&typedIdentity&&observed&&observed->packet.value("driver_valid")==false&&observed->packet.value("gpus").isArray()&&observed->packet.value("gpus").toArray().isEmpty();
        report.insert("requested_scope_verified",noDriver?driverFreeKnown:known);
        if(driverFreeKnown)report["status"]="DriverFreeTypedSnapshotKnown";
        report.insert("first_stream_frame_known",firstFrameKnown);report.insert("trace",trace);
        if(!known&&!driverFreeKnown&&result.error.isEmpty())report.insert("error","EOF0 alone is not expected run/device/typed state/driver knowledge; no UI readiness inferred");
    }catch(const std::exception& error){report.insert("error",QString::fromUtf8(error.what()).left(512));}
    report.insert("frame_after_eof_elapsed_ms",frameElapsed?QJsonValue(*frameElapsed):QJsonValue(QJsonValue::Null));report.insert("completion_elapsed_ms",completionElapsed?QJsonValue(*completionElapsed):QJsonValue(QJsonValue::Null));report.insert("total_elapsed_ms",total.elapsed());
    output(wire(report));return report.value("requested_scope_verified")==true?0:2;
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--live-hardware"))return liveHardware(app.arguments());
    if(app.arguments().contains("--live-readonly-no-driver"))return liveReadonly(app.arguments());
    if(app.arguments().contains("--live-readonly"))return liveReadonly(app.arguments());
    if(app.arguments().contains("--live-stream-first"))return liveReadonly(app.arguments());
    if(argc>1&&QString::fromLocal8Bit(argv[1])=="-F")return fixture();
    try{
        QTemporaryDir temp;require(temp.isValid(),"own temporary fixture directory");
        const auto key=temp.filePath("PUBLIC NOT A KEY.txt");{QFile f(key);require(f.open(QIODevice::WriteOnly),"own PUBLIC fixture file");f.write("PUBLIC_FIXTURE_NOT_A_KEY\n");}
        SshObservationEndpoint endpoint{QCoreApplication::applicationFilePath(),key,"fixture.invalid","fixture_user",2222};
        SshObservationRequest snapshot;snapshot.startupTimeoutMs=1500;snapshot.idleTimeoutMs=1500;snapshot.snapshotTimeoutMs=3000;
        const auto command=makeSshObservationCommand(endpoint,snapshot);
        require(command.arguments.contains("StrictHostKeyChecking=yes")&&command.arguments.contains("UpdateHostKeys=no")&&command.arguments.contains("CheckHostIP=no")&&command.arguments.contains("BatchMode=yes"),"strict no-write trust/key behavior in argv");
        require(command.arguments.mid(0,2)==QStringList{"-F","none"}&&command.arguments.contains("PermitLocalCommand=no")&&command.arguments.contains("ClearAllForwardings=yes"),"no inherited config/proxy/forwarding actions");
        auto exactRequest=snapshot;exactRequest.offset=9007199254740993LL;exactRequest.expectedRun=QString(32,'a');
        const auto exactCommand=makeSshObservationCommand(endpoint,exactRequest);
        const auto remote=exactCommand.arguments.back();const auto encoded=remote.mid(remote.lastIndexOf(' ')+1).toLatin1();
        const auto scriptBytes=QByteArray::fromBase64(encoded);QString script;
        require(scriptBytes.size()%2==0,"EncodedCommand has complete UTF16LE code units");
        for(qsizetype i=0;i<scriptBytes.size();i+=2)script+=QChar(uchar(scriptBytes[i])|(ushort(uchar(scriptBytes[i+1]))<<8));
        require(script=="[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);$OutputEncoding=[Console]::OutputEncoding;& 'C:/PGN/runtime/native-control.ps1' -Action snapshot -Offset 9007199254740993 -ExpectedRun '"+QString(32,'a')+"'","UTF8 producer plus exact readonly script/run/64bit decimal encoding");
        auto leased=exactRequest;leased.action=SshObservationAction::Stream;
        const auto leaseWide=QByteArray::fromBase64(makeSshObservationCommand(endpoint,leased).arguments.back().section(' ',-1).toLatin1());
        const auto leaseScript=QString::fromUtf16(reinterpret_cast<const char16_t*>(leaseWide.constData()),leaseWide.size()/2);
        require(leaseScript.endsWith(" -StreamCount 150")&&leaseScript.contains("-Action stream -Offset 9007199254740993"),"remote read lease is finite; exact cursor survives, no mining control");
        {auto small=leased;small.streamFrames=3;const auto c=makeSshObservationCommand(endpoint,small);const auto b=QByteArray::fromBase64(c.arguments.last().section(' ',-1).toLatin1());require(QString::fromUtf16(reinterpret_cast<const char16_t*>(b.constData()),b.size()/2).endsWith(" -StreamCount 3"),"completed tail requests only its bounded finite READ batch");}
        for(const auto frames:{0,151}){auto bad=leased;bad.streamFrames=frames;rejected([&]{makeSshObservationCommand(endpoint,bad);},"unbounded/oversized read lease refused");}
        for(const auto host:{QString("-oProxyCommand=x"),QString("host name"),QString("host;start"),QString("host'"),QString("host@other")}){auto bad=endpoint;bad.host=host;rejected([&]{makeSshObservationCommand(bad,snapshot);},"host injection refused");}
        {auto bad=endpoint;bad.executable="relative.exe";rejected([&]{makeSshObservationCommand(bad,snapshot);},"relative executable refused");}
        {auto bad=endpoint;bad.keyFile=temp.filePath("absent");rejected([&]{makeSshObservationCommand(bad,snapshot);},"missing key refused");}
        {auto bad=endpoint;bad.port=0;rejected([&]{makeSshObservationCommand(bad,snapshot);},"implicit port refused");}
        {auto bad=snapshot;bad.action=static_cast<SshObservationAction>(99);rejected([&]{makeSshObservationCommand(endpoint,bad);},"non-observation action refused");}
        for(const auto run:{QString("bad"),QString(32,'A'),QString("';start'"),QString(32,QChar(0))}){auto bad=snapshot;bad.expectedRun=run;rejected([&]{makeSshObservationCommand(endpoint,bad);},"run injection refused");}
        {auto bad=snapshot;bad.offset=1;rejected([&]{makeSshObservationCommand(endpoint,bad);},"unbound nonzero cursor refused");}
        const auto cold=decodeSshSnapshot(wire(packet()));require(cold.offset==0&&cold.nextOffset==0&&cold.chunk.isEmpty()&&cold.packet.value("manifest").isNull(),"OFF/null/known0 is valid, not a fake run");
        auto localized=packet();const auto message=QString::fromUtf8("Refus : cl\xc3\xa9 inaccessible");
        localized.insert("last_error",QJsonObject{{"error",message}});
        require(decodeSshSnapshot(wire(localized)).packet.value("last_error").toObject().value("error")==message,"localized UTF8 diagnostic remains intact without changing OFF authority");
        auto localizedLegacyBytes=wire(localized);localizedLegacyBytes.replace(QByteArray("\xc3\xa9"),QByteArray("\xe9"));
        rejected([&]{decodeSshSnapshot(localizedLegacyBytes);},"legacy console bytes are still refused rather than silently repaired by receiver");
        const auto maximum=decodeSshSnapshot(wire(packet(std::numeric_limits<qint64>::max())));require(maximum.offset==std::numeric_limits<qint64>::max(),"LLMAX exact through actual Qt6 JSON");
        auto wide=wire(packet(9007199254740993LL));require(decodeSshSnapshot(wide).offset==9007199254740993LL,"integer >2^53 remains exact");
        auto rounded=wide;rounded.replace("\"offset\":9007199254740993","\"offset\":9007199254740993.0");
        const auto qtRounded=QJsonDocument::fromJson(rounded).object().value("offset");
        require(qtRounded.toVariant().metaType()==QMetaType::fromType<qlonglong>()&&qtRounded.toInteger()==9007199254740992LL,"real SDK witness: metaType LongLong alone can hide fractional rounding");
        rejected([&]{decodeSshSnapshot(rounded);},"fractional lexical alias rejected before rounding can bind a source cursor");
        for(const auto token:{QByteArray("9223372036854775808"),QByteArray("1e3"),QByteArray("-0"),QByteArray("\"0\"")}){auto bad=wire(packet());bad.replace("\"offset\":0","\"offset\":"+token);rejected([&]{decodeSshSnapshot(bad);},"noncanonical/out-of-domain offset refused");}
        auto duplicate=wire(packet());duplicate.insert(1,"\"o\\u0066fset\":0,");rejected([&]{decodeSshSnapshot(duplicate);},"escaped duplicate key cannot bypass guard");
        auto missing=packet();missing.remove("offset");rejected([&]{decodeSshSnapshot(wire(missing));},"missing offset never0");
        const auto pendingZero=decodeSshSnapshot(wire(packet()));require(pendingZero.pendingBytes&&*pendingZero.pendingBytes==0,"present pending0 is genuinely known zero");
        auto legacy=packet();legacy.remove("pending_bytes");require(!decodeSshSnapshot(wire(legacy)).pendingBytes,"missing pending remains unknown, no compatibility zero invented");
        for(const auto value:{qint64(1),qint64(9007199254740993LL),std::numeric_limits<qint64>::max()}){auto observed=packet();observed.insert("pending_bytes",value);require(decodeSshSnapshot(wire(observed)).pendingBytes==value,"pending exact signed64 including wide values");}
        for(const auto token:{QByteArray("0.0"),QByteArray("0.0000000000000001"),QByteArray("9007199254740993.0"),QByteArray("1e3"),QByteArray("-0"),QByteArray("-1"),QByteArray("\"0\""),QByteArray("null"),QByteArray("true"),QByteArray("9223372036854775808")}){auto bad=wire(packet());bad.replace("\"pending_bytes\":0","\"pending_bytes\":"+token);rejected([&]{decodeSshSnapshot(bad);},"pending lexical alias/overflow/type cannot round into readiness");}
        auto duplicatePending=wire(packet());duplicatePending.insert(1,"\"pending_\\u0062ytes\":0,");rejected([&]{decodeSshSnapshot(duplicatePending);},"escaped duplicate pending key cannot bypass exact field");
        auto nested=packet(9007199254740993LL);nested.insert("opaque",QJsonObject{{"offset",1},{"note","[}, escaped quote\""}});require(decodeSshSnapshot(wire(nested)).offset==9007199254740993LL,"nested opaque fields do not spoof top-level offsets");
        // Preserve raw lexemes: a QJsonObject-built fixture would already lose
        // the fractional/rounded declarations this boundary must reject.
        const auto manifestWire=[](const QByteArray& fields){auto raw=wire(packet());raw.replace("\"manifest\":null","\"manifest\":{"+fields+"}");return raw;};
        const auto canonicalPid=decodeSshSnapshot(manifestWire("\"pid\":1234"));
        require(canonicalPid.packet.value("manifest").toObject().value("pid").toInteger()==1234,"canonical declared PID remains exact");
        for(const auto token:{QByteArray("9007199254740993.0"),QByteArray("1234.0000000000000001"),QByteArray("1234.0"),QByteArray("1e3"),QByteArray("-0"),QByteArray("9223372036854775808"),QByteArray("-9223372036854775809"),QByteArray("\"1234\""),QByteArray("null"),QByteArray("true")})
            rejected([&]{decodeSshSnapshot(manifestWire("\"pid\":"+token));},"PID lexical aliases/type/range rejected before declaration rounding");
        for(const auto value:{std::numeric_limits<qint64>::min(),qint64(-1),qint64(0),qint64(9007199254740993LL),std::numeric_limits<qint64>::max()}){
            const auto exactPid=decodeSshSnapshot(manifestWire("\"pid\":"+QByteArray::number(value)));
            require(exactPid.packet.value("manifest").toObject().value("pid").toInteger()==value,"transport preserves signed64 PID; positivity/physical range belongs to admission");
        }
        require(decodeSshSnapshot(manifestWire("\"p\\u0069d\":1234")).packet.value("manifest").toObject().value("pid").toInteger()==1234,"single escaped PID key is decoded without aliasing its integer");
        for(const auto fields:{QByteArray("\"pid\":1234,\"pid\":1234"),QByteArray("\"pid\":1234,\"p\\u0069d\":1234"),QByteArray("\"note\":1,\"n\\u006fte\":2")})
            rejected([&]{decodeSshSnapshot(manifestWire(fields));},"all decoded duplicate immediate manifest keys rejected");
        auto missingManifest=packet();missingManifest.remove("manifest");
        require(!decodeSshSnapshot(wire(missingManifest)).packet.contains("manifest"),"missing manifest remains transport-opaque, not fabricated");
        require(decodeSshSnapshot(manifestWire("\"opaque\":{\"pid\":1234.5},\"note\":\"[}, \\\"\"")).packet.value("manifest").toObject().contains("opaque"),"manifest without immediate PID retains opaque nested content");
        for(const auto encodedBad:{QString("A==="),QString("YQ"),QString("YQ==\n")}){auto bad=packet();bad.insert("chunk",encodedBad);rejected([&]{decodeSshSnapshot(wire(bad));},"noncanonical base64 held");}
        auto wrong=packet();wrong.insert("next_offset",1);rejected([&]{decodeSshSnapshot(wire(wrong));},"offset/length mismatch held");
        SshObservationRequest hardware;hardware.action=SshObservationAction::Hardware;
        const auto hardwareCommand=makeSshObservationCommand(endpoint,hardware);
        const auto hardwareBytes=QByteArray::fromBase64(hardwareCommand.arguments.last().section(' ',-1).toLatin1());
        const auto hardwareScript=QString::fromUtf16(reinterpret_cast<const char16_t*>(hardwareBytes.constData()),hardwareBytes.size()/2);
        require(hardwareScript.contains("nvidia-smi.exe")&&!hardwareScript.contains("native-control.ps1")&&!hardwareScript.contains("-Action "),"hardware uses only read-only vendor CLI, no source/control inventory");
        const auto hw=execute(endpoint,hardware,"hardware");
        require(hw.result.status==SshObservationStatus::Complete&&hw.frames.size()==1&&hw.frames[0].chunk.isEmpty()&&hw.frames[0].packet.size()==4,"typed hardware-only frame cannot carry source chunks or process observations");
        require(execute(endpoint,hardware,"split").result.status==SshObservationStatus::Failed,"hardware path rejects source DTO instead of conflating authority");
        {auto wrong=hardware;wrong.expectedRun=QString(32,'a');rejected([&]{makeSshObservationCommand(endpoint,wrong);},"hardware has no run/cursor");}
        auto first=execute(endpoint,snapshot,"split");require(first.result.status==SshObservationStatus::Complete&&first.frames.size()==1&&first.result.exitCode==0,"split multiline snapshot waits for clean EOF");
        QStringList received;for(const auto& value:first.frames[0].packet.value("fixture_args").toArray())received<<value.toString();require(received==command.arguments,"REAL QProcess receives separate exact argv including spaced key path");
        auto large=execute(endpoint,snapshot,"large");require(large.result.status==SshObservationStatus::Complete&&large.frames[0].offset==9007199254740993LL&&large.frames[0].chunk.size()==65536,"full64KiB base64 snapshot fits record bound");
        auto stream=snapshot;stream.action=SshObservationAction::Stream;
        auto multi=execute(endpoint,stream,"multi");require(multi.result.status==SshObservationStatus::Complete&&multi.frames.size()==2,"multiple JSONL frames/CRLF in one process");
        auto splitStream=execute(endpoint,stream,"splitstream");require(splitStream.result.status==SshObservationStatus::Complete&&splitStream.frames.size()==1&&splitStream.frames[0].packet.value("fixture_text")==QString::fromUtf8("\xc3\xa9\xe6\xbc\xa2"),"JSONL UTF8 codepoint split is buffered until record complete");
        auto partial=execute(endpoint,stream,"partial");require(partial.result.status==SshObservationStatus::Failed&&partial.frames.isEmpty(),"unterminated JSONL EOF is not success");
        auto prefix=execute(endpoint,stream,"prefixpartial");require(prefix.result.status==SshObservationStatus::Failed&&prefix.frames.size()==1&&prefix.result.completeFrames==1,"complete prefix plus partial tail is NOT globally complete");
        for(const auto mode:{"oversize","badjson","empty"}){auto held=execute(endpoint,snapshot,mode);require(held.result.status==SshObservationStatus::Failed,"invalid/oversize/empty snapshot held");}
        auto error=execute(endpoint,snapshot,"exit");require(error.result.status==SshObservationStatus::Failed&&error.result.exitCode==7&&error.frames.isEmpty(),"nonzero snapshot exit not promoted");
        auto stderrFlood=execute(endpoint,snapshot,"stderr");require(stderrFlood.result.status==SshObservationStatus::Failed&&stderrFlood.result.standardError.size()==SshSnapshotTransport::StderrLimit,"stderr bounded and explicitly refused");
        auto timed=snapshot;timed.snapshotTimeoutMs=100;auto timeout=execute(endpoint,timed,"wait");require(timeout.result.status==SshObservationStatus::TimedOut,"snapshot deadline owns only its fixture client");
        auto idle=stream;idle.idleTimeoutMs=100;auto stalled=execute(endpoint,idle,"wait");require(stalled.result.status==SshObservationStatus::TimedOut,"stream valid-frame idle deadline");
        auto cancelled=execute(endpoint,stream,"wait",true,true);require(cancelled.result.status==SshObservationStatus::Cancelled,"explicit cancel terminates own fixture only");
        auto refused=execute(endpoint,stream,"multi",false);require(refused.result.status==SshObservationStatus::Failed&&refused.frames.size()==1,"consumer refusal ends client with identified prefix");
#ifdef Q_OS_WIN
        {
            const auto broken=temp.filePath("own-invalid-image.exe");{QFile f(broken);require(f.open(QIODevice::WriteOnly),"own invalid image fixture");f.write("PUBLIC_NOT_AN_EXECUTABLE\n");}
            auto invalidImage=endpoint;invalidImage.executable=broken;
            auto held=execute(invalidImage,snapshot,"empty");require(held.result.status==SshObservationStatus::Failed&&!held.result.exitCode&&held.frames.isEmpty(),"native QProcess FailedToStart has no fabricated exit0/frame");
        }
#endif
        {
            qputenv("PGN_SSH_FIXTURE_MODE","multi");QEventLoop loop;bool completion=false;QPointer<SshSnapshotTransport> owner=new SshSnapshotTransport;
            QObject::connect(owner,&QObject::destroyed,&loop,&QEventLoop::quit);
            owner->start(endpoint,stream,[&](const auto&){delete owner.data();return true;},[&](const auto&){completion=true;loop.quit();});
            QTimer::singleShot(3000,&loop,&QEventLoop::quit);loop.exec();const bool lost=owner.isNull();if(owner)delete owner.data();
            require(lost&&!completion,"owner destruction inside frame callback is safe, no phantom completion");
        }
        {
            SshObservationResult host;host.status=SshObservationStatus::Failed;host.exitCode=1;host.standardError="System.Management.Automation 0x800705AF";
            require(isSshHostStartupResourceFailure(host),"only explicit pre-frame host allocation failure is replayable");
            QByteArray wide;for(char c:host.standardError){wide.append(c);wide.append(char(0));}host.standardError=wide;
            require(isSshHostStartupResourceFailure(host),"actual UTF16LE PowerShell startup error is recognized");
            host.completeFrames=1;require(!isSshHostStartupResourceFailure(host),"no host-startup retry classification after a source frame");
            host.completeFrames=0;host.standardError="application validation failed";require(!isSshHostStartupResourceFailure(host),"unknown remote exit1 remains held");
        }
        qunsetenv("PGN_SSH_FIXTURE_MODE");
        std::cout<<"SSH_OBSERVATION "<<checks<<" checks / 0 failures; REAL QProcess SELF fixture only, exact argv/UTF16LE/64bit, snapshot multiline vs JSONL, bounds/EOF/error/timeouts/cancel/ownerloss; NO SSH/network/Node/GPU/UI\n";return 0;
    }catch(const std::exception& error){std::cerr<<"SSH_OBSERVATION failed after "<<checks<<": "<<error.what()<<'\n';return 1;}
}

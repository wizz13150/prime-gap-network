#include "mining/ssh_snapshot_transport.h"
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pgn::mining {
QJsonObject SshObservationTrace::json() const {
    const auto name=[&]{switch(stage){case SshObservationTraceStage::RequestStarted:return "request_started";case SshObservationTraceStage::ProcessStarted:return "process_started";case SshObservationTraceStage::FirstStdout:return "first_stdout";case SshObservationTraceStage::ProcessFinished:return "process_finished";case SshObservationTraceStage::StartupDeadline:return "startup_deadline";case SshObservationTraceStage::IdleDeadline:return "idle_deadline";case SshObservationTraceStage::SnapshotDeadline:return "snapshot_deadline";case SshObservationTraceStage::Settled:return "settled";}return "unknown";};
    const auto state=[&]{if(!status)return QJsonValue(QJsonValue::Null);switch(*status){case SshObservationStatus::Complete:return QJsonValue("complete");case SshObservationStatus::Failed:return QJsonValue("failed");case SshObservationStatus::TimedOut:return QJsonValue("timed_out");case SshObservationStatus::Cancelled:return QJsonValue("cancelled");}return QJsonValue(QJsonValue::Null);};
    return {{"schema","pgn-ssh-observation-trace-1"},{"stage",name()},{"action",action==SshObservationAction::Snapshot?"snapshot":action==SshObservationAction::Stream?"stream":"hardware"},{"expected_run",expectedRun},{"offset",offset},{"monotonic_ms",monotonicMs},{"elapsed_ms",elapsedMs},
        {"process_id",processId?QJsonValue(*processId):QJsonValue(QJsonValue::Null)},{"stdout_bytes",stdoutBytes?QJsonValue(*stdoutBytes):QJsonValue(QJsonValue::Null)},{"exit_code",exitCode?QJsonValue(*exitCode):QJsonValue(QJsonValue::Null)},{"status",state()}};
}
namespace {
qint64 monotonicMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
[[noreturn]] void fail(const char* what){throw std::runtime_error(what);}
bool runValid(const QString& run){return QRegularExpression("^[0-9a-f]{32}$").match(run).hasMatch();}
void existingAbsoluteFile(const QString& path){
    if(path.contains(QChar(0))||!QFileInfo(path).isAbsolute()||!QFileInfo(path).isFile())fail("Explicit absolute existing SSH executable/key required");
}
QByteArray utf16le(const QString& script){
    QByteArray bytes;bytes.reserve(script.size()*2);
    for(const auto c:script){bytes.append(char(c.unicode()&255));bytes.append(char(c.unicode()>>8));}return bytes;
}
bool space(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
void skipSpace(const QByteArray& bytes,qsizetype& at){while(at<bytes.size()&&space(bytes[at]))++at;}
qsizetype stringEnd(const QByteArray& bytes,qsizetype at){
    if(at>=bytes.size()||bytes[at]!='"')fail("JSON object key missing");
    while(++at<bytes.size()){if(bytes[at]=='\\')++at;else if(bytes[at]=='"')return at+1;}
    fail("JSON string incomplete");
}
qint64 canonicalInteger(const QByteArray& raw,bool allowNegative=false){
    const auto text=raw.trimmed();const bool negative=allowNegative&&text.startsWith('-');const qsizetype start=negative?1:0;
    if(text.size()==start||(text[start]=='0'&&(negative||text.size()-start>1)))fail("Observation field is not a canonical integer");
    const auto limit=quint64(std::numeric_limits<qint64>::max())+(negative?1:0);quint64 magnitude=0;
    for(qsizetype at=start;at<text.size();++at){const auto c=text[at];if(c<'0'||c>'9'||magnitude>(limit-(c-'0'))/10)fail("Observation field outside signed64 integer domain");magnitude=magnitude*10+(c-'0');}
    if(!negative)return qint64(magnitude);
    return magnitude==limit?std::numeric_limits<qint64>::min():-qint64(magnitude);
}
// QJson remains the JSON grammar authority. Visit only an explicitly selected
// object's immediate fields; opaque nested values are skipped, not interpreted.
template<class Field>void objectFields(const QByteArray& bytes,const Field& field){
    qsizetype at=0;skipSpace(bytes,at);if(at>=bytes.size()||bytes[at++]!='{')fail("Snapshot must be an object");
    QSet<QString> seen;
    while(true){
        skipSpace(bytes,at);if(at>=bytes.size())fail("Snapshot incomplete");if(bytes[at]=='}')break;
        const auto start=at;at=stringEnd(bytes,at);
        const auto key=QJsonDocument::fromJson("["+bytes.mid(start,at-start)+"]").array().at(0).toString();
        if(seen.contains(key))fail("Duplicate observation object field");seen.insert(key);
        skipSpace(bytes,at);if(at>=bytes.size()||bytes[at++]!=':')fail("Snapshot field separator missing");skipSpace(bytes,at);
        const auto valueStart=at;
        if(at<bytes.size()&&bytes[at]=='"')at=stringEnd(bytes,at);
        else if(at<bytes.size()&&(bytes[at]=='{'||bytes[at]=='[')){
            int depth=0;
            do{const auto c=bytes[at];if(c=='"'){at=stringEnd(bytes,at);continue;}if(c=='{'||c=='[')++depth;else if(c=='}'||c==']')--depth;++at;}while(at<bytes.size()&&depth);
        }else while(at<bytes.size()&&bytes[at]!=','&&bytes[at]!='}')++at;
        field(key,bytes.mid(valueStart,at-valueStart));
        skipSpace(bytes,at);if(at<bytes.size()&&bytes[at]==','){++at;continue;}if(at<bytes.size()&&bytes[at]=='}')break;
        fail("Snapshot value separator missing");
    }
}
// Qt preserves canonical int64, but decimal/exponent input may first round to
// a different integer. Retain lexemes for offsets and declared manifest PID.
struct ExactSnapshotOffsets {qint64 first,second;std::optional<qint64> pending;};
ExactSnapshotOffsets offsets(const QByteArray& bytes,const QJsonObject& packet){
    std::optional<qint64> first,next,pending;const auto manifest=packet.value("manifest");
    objectFields(bytes,[&](const QString& key,const QByteArray& value){
        if(key=="offset")first=canonicalInteger(value);
        else if(key=="next_offset")next=canonicalInteger(value);
        else if(key=="pending_bytes")pending=canonicalInteger(value);
        else if(key=="manifest"&&manifest.isObject()){
            objectFields(value,[&](const QString& name,const QByteArray& raw){
                if(name!="pid")return;
                const auto exact=canonicalInteger(raw,true);const auto parsed=manifest.toObject().value("pid");
                if(!parsed.isDouble()||parsed.toInteger()!=exact)fail("Qt manifest PID integer representation mismatch");
            });
        }
    });
    if(!first||!next)fail("Snapshot source offsets absent");return {*first,*next,pending};
}
}
QString nativeRemoteRuntimeRoot(const SshObservationEndpoint& endpoint){
    const auto validate=[](const QString& value){
        if(value.size()<4||value.size()>4096||!value.isValidUtf16()||value.contains(QChar(0))||value.contains('\r')||value.contains('\n')||value.contains('"')
            ||!QRegularExpression("^[A-Za-z]:/").match(value).hasMatch()||QDir::cleanPath(value)!=value||value.endsWith('/'))fail("Explicit canonical remote Windows runtime directory required");
    };
    if(endpoint.runtimeRoot.isEmpty()){if(!endpoint.gpuLeaseRoot.isEmpty())fail("Physical GPU lease root requires an explicit runtime root");return "C:/PGN/runtime";}
    validate(endpoint.runtimeRoot);validate(endpoint.gpuLeaseRoot);return endpoint.runtimeRoot;
}
QString nativeControlScriptForEndpoint(const SshObservationEndpoint& endpoint,const QString& original){
    const auto root=nativeRemoteRuntimeRoot(endpoint);if(endpoint.runtimeRoot.isEmpty())return original;
    const auto quoted=[](QString value){value.replace("'","''");return value;};
    auto script=original;script.replace("'C:/PGN/runtime/","'"+quoted(root)+"/");
    const auto entry="'"+quoted(root)+"/native-control.ps1'";
    script.replace(entry,entry+" -RuntimeRoot '"+quoted(root)+"' -LeaseRoot '"+quoted(endpoint.gpuLeaseRoot)+"'");
    return script;
}
SshObservationCommand makeSshConnectionCommand(const SshObservationEndpoint& endpoint,const QString& remoteCommand){
    existingAbsoluteFile(endpoint.executable);existingAbsoluteFile(endpoint.keyFile);
    if(!QFileInfo(endpoint.executable).isExecutable())fail("SSH executable is not executable");
    if(!endpoint.port||!QRegularExpression("^[A-Za-z0-9_][A-Za-z0-9_.\\\\-]{0,127}$").match(endpoint.user).hasMatch()||
       endpoint.host.isEmpty()||endpoint.host.size()>253||endpoint.host.startsWith('-')||
       !QRegularExpression("^[A-Za-z0-9.:%\\[\\]_-]+$").match(endpoint.host).hasMatch())fail("Explicit direct SSH user/host/port invalid");
    if(!endpoint.hostKeyAlias.isEmpty()&&(endpoint.hostKeyAlias.size()>253||!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.-]*$").match(endpoint.hostKeyAlias).hasMatch()))fail("Explicit SSH host-key alias invalid");
    if(remoteCommand.isEmpty()||remoteCommand.size()>65536||remoteCommand.contains(QChar(0))||remoteCommand.contains('\r')||remoteCommand.contains('\n'))fail("Bounded remote command required");
    QStringList args{"-F","none","-i",endpoint.keyFile,"-p",QString::number(endpoint.port),"-l",endpoint.user};
    for(const auto option:{"BatchMode=yes","StrictHostKeyChecking=yes","UpdateHostKeys=no","CheckHostIP=no","ClearAllForwardings=yes","PermitLocalCommand=no","RequestTTY=no","ConnectTimeout=5","ServerAliveInterval=5","ServerAliveCountMax=2"})args<<"-o"<<option;
    if(!endpoint.hostKeyAlias.isEmpty())args<<"-o"<<"HostKeyAlias="+endpoint.hostKeyAlias;
    args<<"--"<<endpoint.host<<remoteCommand;return {endpoint.executable,args};
}
SshObservationCommand makeSshObservationCommand(const SshObservationEndpoint& endpoint,const SshObservationRequest& request){
    if(request.offset<0||(!request.expectedRun.isEmpty()&&!runValid(request.expectedRun))||
       (request.expectedRun.isEmpty()&&request.offset!=0))fail("Exact source run/offset invalid");
    if(request.startupTimeoutMs<=0||request.idleTimeoutMs<=0||request.snapshotTimeoutMs<=0)fail("Positive SSH observation timeouts required");
    (void)nativeRemoteRuntimeRoot(endpoint);
    if(!endpoint.hostKeyAlias.isEmpty()&&(endpoint.hostKeyAlias.size()>253||!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.-]*$").match(endpoint.hostKeyAlias).hasMatch()))fail("Explicit SSH host-key alias invalid");
    QString action;switch(request.action){case SshObservationAction::Snapshot:action="snapshot";break;case SshObservationAction::Stream:action="stream";break;case SshObservationAction::Hardware:action="hardware";break;default:fail("Unknown observation action");}
    if(request.noDriver&&request.action!=SshObservationAction::Snapshot)fail("Driver-free observation is a process snapshot only");
    auto script=QString("& 'C:/PGN/runtime/native-control.ps1' -Action %1 -Offset %2 -ExpectedRun '%3'").arg(action,QString::number(request.offset),request.expectedRun);
    if(request.noDriver)script+=" -NoDriver";
    if(request.action==SshObservationAction::Stream){
        if(request.streamFrames<1||request.streamFrames>SshSnapshotTransport::StreamFrameLease)fail("Finite stream frame lease outside supported bound");
        script+=" -StreamCount "+QString::number(request.streamFrames);
    }
    if(request.action==SshObservationAction::Hardware){
        if(request.offset!=0||!request.expectedRun.isEmpty())fail("Hardware observation has no source cursor");
        // Existing vendor CLI, read-only. No native-control invocation, process
        // inventory, source log/chunk, wallet, persistent change or GPU tuning.
        script=QString::fromLatin1(R"PS($ErrorActionPreference='Stop';
$smi=(Get-Command nvidia-smi.exe -ErrorAction Stop).Source;
$basic='index,uuid,name,pci.bus_id,utilization.gpu,memory.used,memory.total,power.draw,power.limit,temperature.gpu,clocks.sm,clocks.mem';
$rows=@(& $smi ('--query-gpu='+$basic+',clocks_event_reasons.sw_thermal_slowdown,clocks_event_reasons.sw_power_cap,clocks_event_reasons.hw_slowdown') --format=csv,noheader,nounits 2>$null);
if($LASTEXITCODE -ne 0){$rows=@(& $smi ('--query-gpu='+$basic) --format=csv,noheader,nounits 2>$null)};
if($LASTEXITCODE -ne 0 -or $rows.Count -eq 0){throw 'Driver observation unavailable'};
[pscustomobject]@{schema='pgn-native-hardware-observation-1';driver_valid=$true;observed_utc=[DateTime]::UtcNow.ToString('o');gpus=@($rows)}|ConvertTo-Json -Compress
)PS");
    }
    // Windows PowerShell otherwise emits the console's legacy code page even
    // when SSH carries JSON. Keep the wire UTF-8, including localized errors;
    // the receiver must continue rejecting malformed byte sequences.
    script="[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);$OutputEncoding=[Console]::OutputEncoding;"+nativeControlScriptForEndpoint(endpoint,script);
    const auto remote="powershell.exe -NoProfile -NonInteractive -EncodedCommand "+QString::fromLatin1(utf16le(script).toBase64());
    return makeSshConnectionCommand(endpoint,remote);
}
SshObservationFrame decodeSshSnapshot(const QByteArray& document){
    if(document.isEmpty()||document.size()>SshSnapshotTransport::FrameLimit)fail("Snapshot document bound exceeded/empty");
    if(QString::fromUtf8(document).toUtf8()!=document)fail("Snapshot is not valid UTF8");
    QJsonParseError error;const auto decoded=QJsonDocument::fromJson(document,&error);
    if(error.error!=QJsonParseError::NoError||!decoded.isObject())fail("Malformed snapshot JSON object");
    const auto packet=decoded.object();if(packet.value("schema")!="pgn-runtime-snapshot-1")fail("Snapshot schema mismatch");
    const auto exact=offsets(document,packet);
    if(packet.value("offset").toInteger(-1)!=exact.first||packet.value("next_offset").toInteger(-1)!=exact.second)fail("Qt snapshot integer representation mismatch");
    if(exact.pending&&(!packet.value("pending_bytes").isDouble()||packet.value("pending_bytes").toInteger(-1)!=*exact.pending))fail("Qt snapshot pending integer representation mismatch");
    if(!packet.value("chunk").isString())fail("Snapshot source chunk absent/wrong type");
    const auto text=packet.value("chunk").toString();const auto encoded=text.toLatin1();
    if(QString::fromLatin1(encoded)!=text||encoded.size()>87384)fail("Snapshot source base64 bound/type invalid");
    const auto chunk=QByteArray::fromBase64(encoded);
    if(chunk.size()>SshSnapshotTransport::ChunkLimit||chunk.toBase64()!=encoded)fail("Snapshot source base64 is not canonical/bounded");
    if(exact.first>std::numeric_limits<qint64>::max()-chunk.size()||exact.second!=exact.first+chunk.size())fail("Snapshot source offset/length mismatch");
    return {packet,exact.first,exact.second,chunk,exact.pending};
}

class SshSnapshotTransport::Impl {
public:
    SshSnapshotTransport* owner;
    QProcess process;QTimer startup,idle,deadline;
    QThread* const creatingThread=QThread::currentThread();std::optional<int> observedExit;
    SshObservationRequest request;FrameConsumer frameConsumer;Completion completion;Trace trace;
    QByteArray pending,errors;qint64 frames{},traceStart{};std::optional<qint64> observedPid,stdoutBytes{0};bool used{},done{},started{},stdoutSeen{};
    bool report(SshObservationTraceStage stage,std::optional<int> code={},std::optional<SshObservationStatus> status={}){
        if(!trace)return true;
        const auto now=monotonicMs();const SshObservationTrace value{stage,request.action,request.expectedRun,request.offset,now,now-traceStart,observedPid,stdoutSeen?stdoutBytes:std::nullopt,code,status};
        const auto callback=trace;const QPointer<SshSnapshotTransport> guard(owner);try{callback(value);}catch(...){}return !guard.isNull();
    }
    explicit Impl(SshSnapshotTransport* o):owner(o){
        startup.setSingleShot(true);idle.setSingleShot(true);deadline.setSingleShot(true);
        QObject::connect(&startup,&QTimer::timeout,owner,[this]{if(report(SshObservationTraceStage::StartupDeadline)&&!done)finish(SshObservationStatus::TimedOut,"SSH client startup deadline");});
        QObject::connect(&idle,&QTimer::timeout,owner,[this]{if(report(SshObservationTraceStage::IdleDeadline)&&!done)finish(SshObservationStatus::TimedOut,"SSH observation valid-frame idle deadline");});
        QObject::connect(&deadline,&QTimer::timeout,owner,[this]{if(report(SshObservationTraceStage::SnapshotDeadline)&&!done)finish(SshObservationStatus::TimedOut,"SSH snapshot deadline");});
        QObject::connect(&process,&QProcess::started,owner,[this]{started=true;observedPid=process.processId();startup.stop();if(request.action==SshObservationAction::Stream)idle.start(request.idleTimeoutMs);report(SshObservationTraceStage::ProcessStarted);});
        QObject::connect(&process,&QProcess::readyReadStandardOutput,owner,[this]{drain(false);});
        QObject::connect(&process,&QProcess::readyReadStandardError,owner,[this]{drain(true);});
        QObject::connect(&process,&QProcess::errorOccurred,owner,[this](QProcess::ProcessError){if(!done)finish(SshObservationStatus::Failed,process.errorString().left(512));});
        QObject::connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),owner,[this](int code,QProcess::ExitStatus status){
            observedExit=code;
            if(!report(SshObservationTraceStage::ProcessFinished,code)||done)return;
            QPointer<SshSnapshotTransport> guard(owner);drain(false);if(!guard||done)return;drain(true);if(!guard||done)return;
            if(status!=QProcess::NormalExit||code!=0){finish(SshObservationStatus::Failed,"SSH client did not exit successfully",code);return;}
            if(request.action!=SshObservationAction::Stream){
                const auto document=std::move(pending);if(!publish(document)||!guard)return;
            }else if(!pending.isEmpty()){finish(SshObservationStatus::Failed,"Partial JSONL record at EOF",code);return;}
            if(!frames){finish(SshObservationStatus::Failed,"No complete observation received",code);return;}
            finish(SshObservationStatus::Complete,{},code);
        });
    }
    ~Impl(){done=true;startup.stop();idle.stop();deadline.stop();stopOwned();}
    void stopOwned(){
        process.disconnect(owner);
        if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(1000);} // ONLY this owned client, no PID search/remote stop.
    }
    void finish(SshObservationStatus status,QString message,std::optional<int> code={}){
        if(done)return;done=true;startup.stop();idle.stop();deadline.stop();
        SshObservationResult result{status,std::move(message),errors,code?code:observedExit,frames};
        stopOwned();if(!report(SshObservationTraceStage::Settled,result.exitCode,status))return;const auto callback=std::move(completion);if(callback)callback(result); // No member access after callback/owner loss.
    }
    bool publish(const QByteArray& document){
        QPointer<SshSnapshotTransport> guard(owner);
        try{
            SshObservationFrame frame;
            if(request.action==SshObservationAction::Hardware){
                if(document.isEmpty()||document.size()>8192)fail("Hardware document bound exceeded/empty");
                QJsonParseError error;const auto json=QJsonDocument::fromJson(document,&error);
                const auto packet=json.object();
                if(error.error!=QJsonParseError::NoError||!json.isObject()||packet.size()!=4||
                   packet.value("schema")!="pgn-native-hardware-observation-1"||packet.value("driver_valid")!=true||
                   !packet.value("gpus").isArray()||!QDateTime::fromString(packet.value("observed_utc").toString(),Qt::ISODateWithMs).isValid())fail("Typed hardware observation required");
                for(const auto row:packet.value("gpus").toArray())if(!row.isString())fail("Hardware rows must be vendor CSV strings");
                frame.packet=packet;
            }else frame=decodeSshSnapshot(document);
            if(frames==std::numeric_limits<qint64>::max())fail("Observation counter capacity exhausted");++frames;
            const auto callback=frameConsumer;const bool accepted=callback(frame);
            if(!guard)return false;
            if(!accepted){finish(SshObservationStatus::Failed,"Observation consumer refused frame");return false;}
            if(done)return false;
            if(request.action==SshObservationAction::Stream)idle.start(request.idleTimeoutMs);
            return true;
        }catch(const std::exception& error){if(guard)finish(SshObservationStatus::Failed,QString::fromUtf8(error.what()).left(512));return false;}
        catch(...){if(guard)finish(SshObservationStatus::Failed,"Observation consumer/decoder error");return false;}
    }
    void feed(const QByteArray& bytes){
        pending+=bytes;
        if(request.action!=SshObservationAction::Stream){if(pending.size()>(request.action==SshObservationAction::Hardware?8192:FrameLimit))finish(SshObservationStatus::Failed,"Observation document bound exceeded");return;}
        while(true){
            const auto end=pending.indexOf('\n');
            if(end<0){if(pending.size()>FrameLimit)finish(SshObservationStatus::Failed,"JSONL record bound exceeded");return;}
            if(end>FrameLimit){finish(SshObservationStatus::Failed,"JSONL record bound exceeded");return;}
            auto record=pending.left(end);pending.remove(0,end+1);if(record.endsWith('\r'))record.chop(1);
            if(record.trimmed().isEmpty())continue;
            if(!publish(record))return;
        }
    }
    void drain(bool standardError){
        if(done)return;QPointer<SshSnapshotTransport> guard(owner);
        process.setReadChannel(standardError?QProcess::StandardError:QProcess::StandardOutput);
        while(process.bytesAvailable()>0){
            const auto bytes=process.read(ChunkLimit);if(bytes.isEmpty())break;
            if(standardError){
                const auto room=StderrLimit-errors.size();errors+=bytes.left(room);
                if(bytes.size()>room){finish(SshObservationStatus::Failed,"SSH stderr bound exceeded");return;}
            }else {
                if(trace){
                    if(stdoutBytes){if(*stdoutBytes>std::numeric_limits<qint64>::max()-bytes.size())stdoutBytes.reset();else *stdoutBytes+=bytes.size();}
                    const bool first=!stdoutSeen;stdoutSeen=true;
                    if(first&&(!report(SshObservationTraceStage::FirstStdout)||done))return;
                }
                feed(bytes);
            }
            if(!guard||done)return;
        }
    }
    void start(const SshObservationEndpoint& endpoint,const SshObservationRequest& r,FrameConsumer framesCallback,Completion doneCallback,Trace traceCallback){
        if(used)fail("SSH observation object is single-use");if(QThread::currentThread()!=creatingThread||owner->thread()!=creatingThread)fail("SSH observation creating-thread ownership required");
        const auto command=makeSshObservationCommand(endpoint,r);if(!framesCallback||!doneCallback)fail("SSH observation callbacks required");
        used=true;request=r;frameConsumer=std::move(framesCallback);completion=std::move(doneCallback);trace=std::move(traceCallback);traceStart=monotonicMs();
        process.setProcessChannelMode(QProcess::SeparateChannels);process.setStandardInputFile(QProcess::nullDevice());
#ifdef Q_OS_WIN
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
#endif
        process.setProgram(command.executable);process.setArguments(command.arguments);
        if(!report(SshObservationTraceStage::RequestStarted)||done)return;
        startup.start(request.startupTimeoutMs);if(request.action!=SshObservationAction::Stream)deadline.start(request.snapshotTimeoutMs);
        process.start();
    }
};
bool isSshHostStartupResourceFailure(const SshObservationResult& result){
 if(result.status!=SshObservationStatus::Failed||!result.exitCode||*result.exitCode==0||result.completeFrames!=0||result.standardError.size()>SshSnapshotTransport::StderrLimit)return false;
 auto ascii=result.standardError;ascii.replace(QByteArray(1,'\0'),QByteArray()); // UTF16LE startup stderr still preserves ASCII signatures.
 return ascii.contains("0x800705AF")&&ascii.contains("System.Management.Automation");
}
SshSnapshotTransport::SshSnapshotTransport(QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(this)){}
SshSnapshotTransport::~SshSnapshotTransport()=default;
void SshSnapshotTransport::start(const SshObservationEndpoint& endpoint,const SshObservationRequest& request,FrameConsumer frames,Completion done,Trace trace){impl_->start(endpoint,request,std::move(frames),std::move(done),std::move(trace));}
void SshSnapshotTransport::cancel(){if(impl_->used&&!impl_->done)impl_->finish(SshObservationStatus::Cancelled,"Observation client cancelled");}
}

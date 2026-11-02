#include "mining/native_finite_device.h"
#include "mining/native_control_command.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <functional>

using namespace pgn::mining;
namespace {
int checks{};
const QString Run(32,'a'),OldRun(32,'b'),Time="2026-10-15T12:34:56.0000000Z";
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
QJsonObject reparsed(const QJsonObject& value){return QJsonDocument::fromJson(QJsonDocument(value).toJson(QJsonDocument::Compact)).object();}
QJsonObject sample(){
    const auto job=QString("C:/PGN/runtime/devices/test-3060/jobs/")+Run;
    return reparsed({{"schema","pgn-device-control-observation-1"},{"device",Finite3060Device},{"scope","BOUNDED_SCIENCE_TEST"},{"observed_utc",Time},
        {"owned",true},{"desired_enabled",true},{"stop_requested",false},{"source_credit",false},{"auto_advance",false},
        {"manifest",QJsonObject{{"run",Run},{"uuid",Finite3060Device},{"pid",1234},{"birth",Time},{"image_sha256",Finite3060ImageSha256},
            {"start",Finite3060Start},{"length",Finite3060Length},{"stop",job+".stop"},{"stdout",job+".stdout"},{"stderr",job+".stderr"},{"request_id","previous-worker-request"}}},
        {"last_exit",QJsonObject{{"run",OldRun},{"exit_code",0},{"utc",Time}}},{"last_error",QJsonValue::Null}});
}
QHash<QString,QByteArray> tokens(){return {{"/manifest/pid","1234"},{"/last_exit/exit_code","0"}};}
void refuses(const QJsonObject& value,const QHash<QString,QByteArray>& lexemes){bool caught=false;try{(void)projectNativeFiniteObservation(reparsed(value),lexemes);}catch(const std::exception&){caught=true;}check(caught,"incompatible finite projection must refuse, no default state");}
void projections(){
    const auto p=sample();const auto observed=projectNativeFiniteObservation(p,tokens());
    check(observed.owned&&observed.desiredEnabled&&!observed.stopRequested,"reported finite booleans preserved");
    check(observed.manifest&&observed.manifest->run==Run&&observed.manifest->pid=="1234","exact typed run/PID");
    check(observed.manifest->start=="199999990000000000000"&&observed.manifest->length=="1030792151040","profile integers >2^53 remain strings");
    check(observed.manifest->requestId=="previous-worker-request","worker request is not a command response correlation");
    check(observed.lastExit&&observed.lastExit->run==OldRun&&observed.lastExit->exitCode==0,"older last exit retained, not current completion");
    check(observed.packet==p&&observed.observedUtc==Time,"raw observation and original time preserved");
    auto idle=p;idle["owned"]=false;idle["manifest"]=QJsonValue::Null;idle["last_exit"]=QJsonValue::Null;
    const auto pending=projectNativeFiniteObservation(idle,{});
    check(!pending.manifest&&!pending.lastExit&&pending.desiredEnabled,"null manifest plus enabled can mean pending launch, never invented idle");
    idle["desired_enabled"]=false;check(!projectNativeFiniteObservation(idle,{}).owned,"explicit null manifest observation accepted without inventing run");
    idle["owned"]=true;refuses(idle,{});idle["owned"]=false;idle["stop_requested"]=true;refuses(idle,{});
    for(const auto& key:p.keys()){auto bad=p;bad.remove(key);refuses(bad,tokens());}
    for(const auto* key:{"owned","desired_enabled","stop_requested","source_credit","auto_advance"}){auto bad=p;bad[key]=0;refuses(bad,tokens());}
    for(const auto* key:{"source_credit","auto_advance"}){auto bad=p;bad[key]=true;refuses(bad,tokens());}
    for(const auto* key:{"schema","device","scope"}){auto bad=p;bad[key]="unknown";refuses(bad,tokens());}
    auto bad=p;bad["extra"]=true;refuses(bad,tokens());
    for(const auto& time:QStringList{"2026-02-30T12:34:56.0000000Z","2026-10-15T12:34:56Z","2026-10-15T12:34:56.0000000+00:00"}){bad=p;bad["observed_utc"]=time;refuses(bad,tokens());}
    const auto m=p.value("manifest").toObject();
    for(const auto& key:m.keys()){bad=p;auto b=m;b.remove(key);bad["manifest"]=b;refuses(bad,tokens());}
    for(const auto* key:{"run","uuid","image_sha256","start","length","stop","stdout","stderr"}){bad=p;auto b=m;b[key]="wrong";bad["manifest"]=b;refuses(bad,tokens());}
    bad=p;auto otherWorker=m;otherWorker["request_id"]="another-worker-request";bad["manifest"]=otherWorker;
    check(projectNativeFiniteObservation(reparsed(bad),tokens()).manifest->requestId=="another-worker-request","valid historical worker request must NOT be compared to current action UUID");
    for(const auto& lexical:QList<QByteArray>{"1234.0","1234.0000000000000001","1.234e3","01234","-0","0","4294967296","9007199254740993.0",""}){auto t=tokens();t["/manifest/pid"]=lexical;refuses(p,t);}
    bad=p;auto b=m;b["pid"]=1235;bad["manifest"]=b;refuses(bad,tokens());
    bad=p;b=m;b["pid"]="1234";bad["manifest"]=b;refuses(bad,tokens());
    bad=p;b=m;b["pid"]=4294967295LL;bad["manifest"]=b;auto t=tokens();t["/manifest/pid"]="4294967295";
    check(projectNativeFiniteObservation(reparsed(bad),t).manifest->pid=="4294967295","uint32 maximum exact PID representation");
    for(const auto& lexical:QList<QByteArray>{"0.0","-0","0e0","2147483648","-2147483649",""}){auto t=tokens();t["/last_exit/exit_code"]=lexical;refuses(p,t);}
    bad=p;auto e=p.value("last_exit").toObject();e["exit_code"]=-1;bad["last_exit"]=e;t=tokens();t["/last_exit/exit_code"]="-1";
    check(projectNativeFiniteObservation(reparsed(bad),t).lastExit->exitCode==-1,"known negative exit preserved, not normalized to success");
    bad=p;bad["last_error"]=QJsonObject{{"utc",Time},{"error","historical error"}};
    check(projectNativeFiniteObservation(reparsed(bad),tokens()).lastError->error=="historical error","historical error preserved");
    bad["last_error"]=QJsonObject{{"utc",Time},{"error",QString(8193,'e')}};refuses(bad,tokens());
    bad=p;b=m;b["request_id"]=QString("nul")+QChar(0);bad["manifest"]=b;refuses(bad,tokens());
}
QByteArray json(const QJsonObject& value){return QJsonDocument(value).toJson(QJsonDocument::Compact);}
QString script(const NativeControlCommand& command){
    const auto remote=command.ssh().arguments.last();const auto bytes=QByteArray::fromBase64(remote.mid(remote.lastIndexOf(' ')+1).toLatin1());
    check(!bytes.isEmpty()&&bytes.size()%2==0,"typed finite script uses UTF16LE EncodedCommand");
    QString result;for(int i=0;i<bytes.size();i+=2)result+=QChar(quint8(bytes[i])|(quint16(quint8(bytes[i+1]))<<8));
    const QString prefix="[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);$OutputEncoding=[Console]::OutputEncoding;$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';try{";
    const QString suffix="}catch{[Console]::Error.WriteLine($_.Exception.Message);exit 1}";
    check(result.startsWith(prefix)&&result.endsWith(suffix),"exact UTF8 diagnostics/nonzero-error envelope preserved");
    return result.mid(prefix.size(),result.size()-prefix.size()-suffix.size());
}
template<class F>void commandRefuses(F operation){bool caught=false;try{operation();}catch(const std::exception&){caught=true;}check(caught,"invalid finite command must refuse before execution");}
void unknown(const NativeControlCommand& command,const QByteArray& bytes,bool normal=true,int code=0,const QByteArray& errors={}){
    const auto r=command.decode(bytes,normal,code,errors);
    check(r.outcome==NativeControlOutcome::Unconfirmed&&!r.error.isEmpty()&&r.error.size()<=400&&r.status.isEmpty()&&r.requestId.isEmpty()&&r.run.isEmpty()&&r.packet.isEmpty()&&!r.finiteObservation&&r.requiresFreshObservation(),"bad finite response leaves no plausible observation/ACK, no retry");
}
void commands(){
    QTemporaryDir temp;check(temp.isValid(),"own temp command metadata only");
    const auto file=[&](const char* name,bool executable=false){const auto path=temp.filePath(name);QFile f(path);check(f.open(QIODevice::WriteOnly)&&f.write("TESTONLY_NOT_EXECUTED")==21,"own fake endpoint files, no user key");f.close();if(executable)check(QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner),"own fake endpoint executable bit; file is never executed");return path;};
    const SshObservationEndpoint endpoint{file("fake-ssh.exe",true),file("fixture-key"),"example.invalid","fixture",22};
    const QString request="11111111-2222-4333-8444-555555555555";
    const auto snapshot=NativeControlCommand::finite3060Snapshot(endpoint),start=NativeControlCommand::finite3060Start(endpoint,request);
    const auto stop=NativeControlCommand::finiteObservedRunStop(endpoint,request,Run);
    const auto close=NativeControlCommand::configured3060StopLatch(endpoint,request,Configured3060OffReason::ApplicationClose);
    const auto off=NativeControlCommand::configured3060StopLatch(endpoint,request,Configured3060OffReason::ExplicitCardOff);
    check(snapshot.action()==NativeControlAction::Finite3060Snapshot&&start.action()==NativeControlAction::Finite3060Start&&stop.action()==NativeControlAction::FiniteObservedRunStop&&close.action()==NativeControlAction::Configured3060StopLatch,"distinct typed actions, no fallback");
    const auto expected=QString("& '%1'").arg(Finite3060Controller);
    check(script(snapshot)==expected+" -Action snapshot","snapshot only exact existing controller, no profile/task invented");
    check(script(start)==expected+" -Action start -RequestId '"+request+"'","explicit existing bounded start, no generic worker/recover");
    check(script(stop)==expected+" -Action stop -RequestId '"+request+"' -ExpectedRun '"+Run+"'","observed-run stop always carries exact expected run");
    check(script(close)==expected+" -Action stop -RequestId '"+request+"'"&&script(off)==script(close),"configured-card OFF latch deliberately has no observed-run claim");
    const auto argv=snapshot.ssh().arguments;
    for(const auto& policy:QStringList{"none","StrictHostKeyChecking=yes","UpdateHostKeys=no","CheckHostIP=no","BatchMode=yes","ClearAllForwardings=yes","PermitLocalCommand=no","RequestTTY=no"})check(argv.contains(policy),"existing DIRECT endpoint trust/forwarding policy preserved");
    check(argv.first()=="-F"&&argv.contains(endpoint.keyFile)&&argv[argv.size()-3]=="--"&&argv[argv.size()-2]==endpoint.host,"endpoint arguments separated, no SSH config fallback");
    commandRefuses([&]{NativeControlCommand::finite3060Start(endpoint,"';worker");});
    commandRefuses([&]{NativeControlCommand::finiteObservedRunStop(endpoint,request,{});});
    commandRefuses([&]{NativeControlCommand::finiteObservedRunStop(endpoint,request,Run.toUpper());});
    commandRefuses([&]{NativeControlCommand::configured3060StopLatch(endpoint,request,Configured3060OffReason::Unspecified);});
    commandRefuses([&]{NativeControlCommand::configured3060StopLatch(endpoint,request,static_cast<Configured3060OffReason>(99));});
    auto invalidEndpoint=endpoint;invalidEndpoint.host="-unsafe";commandRefuses([&]{NativeControlCommand::finite3060Snapshot(invalidEndpoint);});
    const auto raw=json(sample());
    for(const auto* command:{&snapshot,&start,&stop,&close,&off}){
        const auto result=command->decode(raw,true,0);
        check(result.outcome==NativeControlOutcome::ObservationReported&&result.finiteObservation&&result.status=="FINITE_OBSERVATION_REPORTED"&&result.run==Run&&result.requiresFreshObservation(),"complete finite response is ONLY typed observation, not physical/request ACK");
        check(result.requestId==(command->action()==NativeControlAction::Finite3060Snapshot?QString{}:request)&&result.finiteObservation->manifest->requestId=="previous-worker-request","local client request distinct from historical worker identity");
    }
    auto pending=sample();pending["manifest"]=QJsonValue::Null;pending["owned"]=false;pending["last_exit"]=QJsonValue::Null;
    check(start.decode(json(pending),true,0).outcome==NativeControlOutcome::ObservationReported,"start can report pending with null run; never invent success run");
    check(close.decode(json(pending),true,0).outcome==NativeControlOutcome::ObservationReported,"explicit configured latch accepts null manifest without fake PID/run");
    unknown(stop,json(pending));auto wrong=sample();auto m=wrong.value("manifest").toObject();m["run"]=OldRun;wrong["manifest"]=m;unknown(stop,json(wrong));
    unknown(snapshot,raw,false);unknown(snapshot,raw,true,1);unknown(snapshot,raw,true,0,QByteArray(NativeControlCommand::StderrLimit+1,'e'));
    unknown(snapshot,{});unknown(snapshot,raw.left(raw.size()-1));unknown(snapshot,raw+"{}");unknown(snapshot,QByteArray(NativeControlCommand::ResponseLimit+1,' '));
    auto duplicate=raw;duplicate.replace("\"owned\":true","\"owned\":false,\"ow\\u006eed\":true");unknown(snapshot,duplicate);
    duplicate=raw;duplicate.replace("\"pid\":1234","\"pid\":1,\"p\\u0069d\":1234");unknown(snapshot,duplicate);
    duplicate=raw;duplicate.replace("\"exit_code\":0","\"exit_code\":1,\"exit\\u005fcode\":0");unknown(snapshot,duplicate);
    for(const auto& token:QList<QByteArray>{"1234.0","1234.0000000000000001","1.234e3","9007199254740993.0","4294967296","-0","\"1234\""}){auto bad=raw;bad.replace("\"pid\":1234","\"pid\":"+token);unknown(snapshot,bad);}
    for(const auto& token:QList<QByteArray>{"0.0","0e0","-0","\"0\""}){auto bad=raw;bad.replace("\"exit_code\":0","\"exit_code\":"+token);unknown(snapshot,bad);}
    unknown(snapshot,"\xff"+raw);
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{projections();commands();std::cout<<"native finite device checks="<<checks<<" failures=0; PURE command/decode, own temp metadata only, NO SSH/process/GPU/UI\n";return 0;}catch(const std::exception& error){std::cerr<<"native finite device checks="<<checks<<" failure="<<error.what()<<'\n';return 1;}}

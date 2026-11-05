#include "mining/native_control_transport.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <algorithm>
#include <iostream>
#include <thread>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

using namespace pgn::mining;
namespace {
int checks{};
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",Run(32,'a'),Birth="2026-10-15T00:00:00.0000000Z",Image(64,'A');
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
QByteArray response(){return QJsonDocument(QJsonObject{{"protocol","pgn-stop-observed-1"},{"status","STOP_REQUESTED"},
    {"observed",QJsonObject{{"run",Run},{"pid","1234"},{"birth",Birth},{"image_sha256",Image}}},{"physical_applied",false}}).toJson(QJsonDocument::Indented);}
void writeBytes(FILE* file,const QByteArray& bytes){if(std::fwrite(bytes.constData(),1,size_t(bytes.size()),file)!=size_t(bytes.size())||std::fflush(file)!=0)throw std::runtime_error("fake SSH pipe write");}
// The ONLY child this suite starts is this very test image (or its own copy).
// These are normal SSH argv but are interpreted by this closed fake, not SSH.
int fakeSsh(const QStringList& args){
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);_setmode(_fileno(stderr),_O_BINARY);
#endif
    const auto marker=args.indexOf("--"),keyAt=args.indexOf("-i");
    if(marker<0||marker+2!=args.size()-1||keyAt<0||args.value(1)!="-F"||args.value(2)!="none")return 51;
    const auto scenario=args[marker+1],key=args[keyAt+1];
    if(!args.last().startsWith("powershell.exe -NoProfile -NonInteractive -EncodedCommand "))return 52;
#ifdef Q_OS_WIN
    if(GetConsoleWindow()!=nullptr)return 53; // CREATE_NO_WINDOW, never desktop
#endif
    if(std::fgetc(stdin)!=EOF)return 54; // transport stdin is actually null
    {QFile starts(key+".starts");if(!starts.open(QIODevice::Append))return 55;starts.write("started\n");starts.close();}
    if(scenario=="wait"){
        QSaveFile pid(key+".pid");if(!pid.open(QIODevice::WriteOnly))return 56;
        // Leave a scheduler window before writing: the reader must see either
        // no marker or the complete PID, never a newly created empty file.
        QThread::msleep(50);const auto bytes=QByteArray::number(QCoreApplication::applicationPid());
        if(pid.write(bytes)!=bytes.size()||!pid.commit())return 56;
        QThread::msleep(5000);return 0;
    }
    const auto good=response();
    if(scenario=="split"){writeBytes(stdout,good.left(7));QThread::msleep(20);writeBytes(stdout,good.mid(7));return 0;}
    if(scenario=="nonzero"){writeBytes(stdout,good);return 7;}
    if(scenario=="partial"){writeBytes(stdout,good.left(good.size()/2));return 0;}
    if(scenario=="malformed"){writeBytes(stdout,"{not JSON}");return 0;}
    if(scenario=="duplicate"){auto bytes=good;bytes.replace("\"status\": \"STOP_REQUESTED\"","\"status\": \"STOP_REQUESTED\",\"st\\u0061tus\":\"STOP_REQUESTED\"");writeBytes(stdout,bytes);return 0;}
    if(scenario=="invalid-utf8"){writeBytes(stdout,QByteArray("\xff"));return 0;}
    if(scenario=="stdout-overflow"){
        const QByteArray block(65536,' ');for(qsizetype i=0;i<NativeControlCommand::ResponseLimit;i+=block.size())writeBytes(stdout,block);writeBytes(stdout," ");return 0;
    }
    if(scenario=="stderr-overflow"){writeBytes(stderr,QByteArray(NativeControlCommand::StderrLimit+1,'e'));writeBytes(stdout,good);return 0;}
    if(scenario=="limits"){writeBytes(stderr,QByteArray(NativeControlCommand::StderrLimit,'e'));writeBytes(stdout,good);const QByteArray block(65536,' ');auto remaining=NativeControlCommand::ResponseLimit-good.size();while(remaining>0){const auto n=std::min<qsizetype>(block.size(),remaining);writeBytes(stdout,block.left(n));remaining-=n;}return 0;}
    writeBytes(stdout,good);return 0;
}
struct Fixture {
    QTemporaryDir temp;QString key;
    Fixture():key(temp.filePath("PUBLIC-FAKE-KEY-not-secret.txt")){
        check(temp.isValid(),"own temporary fixture directory");QFile file(key);check(file.open(QIODevice::WriteOnly),"own public key-path stand-in");file.write("PUBLIC DATA NOT SSH KEY MATERIAL");file.close();
    }
    NativeControlCommand command(const QString& scenario,const QString& executable=QCoreApplication::applicationFilePath())const{
        return NativeControlCommand::stopObservedIdentity({executable,key,scenario,"fixture",22},{Device,Run,"1234",Birth,Image},"pgn-stop-observed-1");
    }
    int starts()const{QFile file(key+".starts");if(!file.open(QIODevice::ReadOnly))return 0;return file.readAll().count('\n');}
};
struct Case {NativeControlTransportResult result;int callbacks{};};
Case runCase(const QString& scenario,NativeControlTimeouts times={1000,2000}){
    Fixture f;const auto command=f.command(scenario);NativeControlTransport transport;QEventLoop loop;QTimer safety;Case observed;
    safety.setSingleShot(true);QObject::connect(&safety,&QTimer::timeout,&loop,[&]{transport.cancel();loop.quit();});safety.start(4000);
    transport.start(command,times,[&](const auto& result){observed.result=result;++observed.callbacks;QTimer::singleShot(60,&loop,&QEventLoop::quit);});loop.exec();
    check(observed.callbacks==1,"exactly one deferred completion, no retry/double publication");
    const auto bodyEntries=f.starts();
    // This deliberately tiny deadline may kill OUR client before the SELF
    // image reaches main (loader scheduling is not a transport retry). Every
    // other case still requires actual fixture entry, including active cancel.
    const bool preEntryTimeout=scenario=="wait"&&observed.result.status==NativeControlTransportStatus::TimedOut&&observed.result.control.outcome==NativeControlOutcome::Unconfirmed;
    check(preEntryTimeout?(bodyEntries==0||bodyEntries==1):bodyEntries==1,"one submission; timeout may precede SELF body, never duplicate entry");
    if(preEntryTimeout)std::cout<<"timeout SELF body_entries="<<bodyEntries<<"; one start call, no retry, TimedOut/Unconfirmed\n";
    check(observed.result.remoteOutcomeUnknown()&&observed.result.requiresFreshObservation()&&!observed.result.physicalApplied(),"no transport result claims remote stopped/applied/Ready");
    bool single=false;try{transport.start(command,times,[](const auto&){});}catch(const std::logic_error&){single=true;}check(single&&f.starts()==bodyEntries,"single-use after completion/failure/timeout; no extra body entry");
    return observed;
}
void completedAndFailures(){
    for(const auto* name:{"ok","split","limits"}){const auto result=runCase(name).result;check(result.status==NativeControlTransportStatus::CompleteResponse&&result.control.outcome==NativeControlOutcome::RequestReported&&result.error.isEmpty(),"valid complete/split/boundary request response only");if(QString(name)=="limits")check(result.standardError.size()==8192&&!result.stderrOverflow,"exact stderr boundary retained, not clipped-success");}
    for(const auto* name:{"nonzero","partial","malformed","duplicate","invalid-utf8","stdout-overflow","stderr-overflow"}){
        const auto result=runCase(name).result;check(result.status==NativeControlTransportStatus::Failed&&result.control.outcome==NativeControlOutcome::Unconfirmed&&result.control.status.isEmpty()&&result.control.packet.isEmpty()&&!result.error.isEmpty()&&result.error.size()<=400,"invalid response/exit/overflow is unconfirmed, no prefix success");
        if(QString(name)=="stderr-overflow")check(result.stderrOverflow&&result.standardError.size()<=8192,"stderr overflow explicitly labelled bounded prefix");
    }
    const auto timeout=runCase("wait",{1000,1100}).result;check(timeout.status==NativeControlTransportStatus::TimedOut&&timeout.control.outcome==NativeControlOutcome::Unconfirmed,"deadline kills OWN client only; remote outcome remains unknown");
}
void startupAndGuards(){
    Fixture f;const auto copy=f.temp.filePath("own-fake-ssh.exe");check(QFile::copy(QCoreApplication::applicationFilePath(),copy),"own test image copy only");const auto command=f.command("ok",copy);check(QFile::remove(copy),"remove own test copy AFTER immutable command construction");
    NativeControlTransport transport;QEventLoop loop;int callbacks=0;NativeControlTransportResult observed;
    transport.start(command,{1000,2000},[&](const auto& r){++callbacks;observed=r;loop.quit();});QTimer::singleShot(3000,&loop,&QEventLoop::quit);loop.exec();
    check(callbacks==1&&observed.status==NativeControlTransportStatus::Failed&&observed.control.outcome==NativeControlOutcome::Unconfirmed&&!observed.localExitCode,"FailedToStart is not exit0/remote refusal or a retry");check(f.starts()==0,"startup failure did not execute child");
    const auto valid=f.command("ok");for(const auto times:{NativeControlTimeouts{0,100},NativeControlTimeouts{100,0},NativeControlTimeouts{30001,40000},NativeControlTimeouts{1,120001},NativeControlTimeouts{200,100}}){NativeControlTransport t;bool refused=false;try{t.start(valid,times,[](const auto&){});}catch(const std::invalid_argument&){refused=true;}check(refused&&f.starts()==0,"invalid/unbounded timeout refused before submission");}
    NativeControlTransport t;bool rejected=false;std::thread wrong([&]{try{t.start(valid,{1000,2000},[](const auto&){});}catch(const std::logic_error&){rejected=true;}});wrong.join();check(rejected&&f.starts()==0,"wrong thread refused before any QProcess activity");
}
void callbackLifetime(){
    Fixture f;auto* transport=new NativeControlTransport;QPointer<NativeControlTransport> weak(transport);QEventLoop loop;int callbacks=0;
    transport->start(f.command("ok"),{1000,2000},[&](const auto& r){++callbacks;check(r.status==NativeControlTransportStatus::CompleteResponse,"completion before delete");delete transport;transport=nullptr;loop.quit();});QTimer::singleShot(3000,&loop,&QEventLoop::quit);loop.exec();
    check(callbacks==1&&weak.isNull(),"deleting owner INSIDE deferred callback is safe");
    NativeControlTransport throws;QEventLoop second;int threw=0;throws.start(f.command("ok"),{1000,2000},[&](const auto&){++threw;throws.cancel();second.quit();throw std::runtime_error("intentional fixture consumer exception");});QTimer::singleShot(3000,&second,&QEventLoop::quit);second.exec();check(threw==1,"consumer exception/cancel cannot trigger a second result or retry");
}
void activeCleanup(bool destroy){
    Fixture f;auto lifetime=std::make_unique<NativeControlTransport>();auto* transport=lifetime.get();QPointer<NativeControlTransport> weak(transport);QEventLoop loop;QTimer poll,safety;QElapsedTimer elapsed;elapsed.start();int callbacks=0;bool acted=false,callbackValid=false;
#ifdef Q_OS_WIN
    HANDLE owned=nullptr;
#endif
    const auto command=f.command("wait");
    transport->start(command,{5000,10000},[&](const auto& r){++callbacks;callbackValid=!destroy&&r.status==NativeControlTransportStatus::Cancelled&&r.control.outcome==NativeControlOutcome::Unconfirmed;loop.quit();});
    bool activeReuseRefused=false;try{transport->start(command,{5000,10000},[](const auto&){});}catch(const std::logic_error&){activeReuseRefused=true;}
    poll.setInterval(10);QObject::connect(&poll,&QTimer::timeout,&loop,[&]{
        if(acted)return;QFile pidFile(f.key+".pid");if(!pidFile.open(QIODevice::ReadOnly)){if(elapsed.elapsed()>8000)loop.quit();return;}
        bool ok=false;const auto pid=pidFile.readAll().toULongLong(&ok);check(ok&&pid>0&&pid<=4294967295ULL,"known OWN fake-child PID marker");
#ifdef Q_OS_WIN
        // Capture a stable handle BEFORE cancel/delete; no kill-by-PID and no
        // process enumeration. Query only the own live fixture child image.
        owned=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,DWORD(pid));check(owned!=nullptr,"own child handle captured before cleanup");wchar_t image[32768];DWORD size=32768;check(QueryFullProcessImageNameW(owned,0,image,&size)!=0&&QString::fromWCharArray(image,int(size)).replace('\\','/').compare(QCoreApplication::applicationFilePath().replace('\\','/'),Qt::CaseInsensitive)==0,"handle image is our exact test executable, not a user process");
#endif
        acted=true;poll.stop();if(destroy){lifetime.reset();transport=nullptr;QTimer::singleShot(80,&loop,&QEventLoop::quit);}else transport->cancel();
    });poll.start();safety.setSingleShot(true);QObject::connect(&safety,&QTimer::timeout,&loop,&QEventLoop::quit);safety.start(12000);loop.exec();
    check(acted&&activeReuseRefused&&f.starts()==1,"active double-submit refused; cleanup touches only ONE exact own fake child");
#ifdef Q_OS_WIN
    check(owned&&WaitForSingleObject(owned,1000)==WAIT_OBJECT_0,"owned client exited after local cancel/destruction");if(owned)CloseHandle(owned);
#endif
    check((destroy&&callbacks==0&&weak.isNull())||(!destroy&&callbacks==1&&callbackValid),"destroy suppresses callback; cancel publishes once with UNKNOWN remote outcome");
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{const auto args=app.arguments();if(args.contains("-F"))return fakeSsh(args);completedAndFailures();startupAndGuards();callbackLifetime();activeCleanup(false);activeCleanup(true);std::cout<<"native control transport checks="<<checks<<" failures=0; QCoreApplication + OWN fake child only, no SSH/GPU/UI/user process\n";return 0;}catch(const std::exception& error){std::cerr<<"native control transport checks="<<checks<<" failure="<<error.what()<<'\n';return 1;}}

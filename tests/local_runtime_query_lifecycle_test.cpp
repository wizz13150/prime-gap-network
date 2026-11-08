#include "engine/local_runtime_client.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#include <cerrno>
#endif
struct ChildExitProbe{
 qint64 pid;
#ifdef Q_OS_WIN
 HANDLE handle;explicit ChildExitProbe(qint64 p):pid(p),handle(OpenProcess(SYNCHRONIZE,FALSE,DWORD(p))){}~ChildExitProbe(){if(handle)CloseHandle(handle);}bool stopped()const{return handle&&WaitForSingleObject(handle,0)==WAIT_OBJECT_0;}
#else
 explicit ChildExitProbe(qint64 p):pid(p){}bool stopped()const{return kill(pid,0)==-1&&errno==ESRCH;}
#endif
};
static bool waitFor(QCoreApplication& app,const std::function<bool()>& ready,int ms=3000){QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<ms){app.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QThread::msleep(2);}return ready();}
int main(int argc,char**argv){QCoreApplication app(argc,argv);auto args=app.arguments();if(args.contains("--reply")){std::cout<<"{\"schema\":\"QA_QUERY\"}\n";return 0;}if(args.contains("--bad")){std::cout<<"INVALID\n";return 0;}if(args.contains("--pending")){QTimer::singleShot(30000,&app,&QCoreApplication::quit);return app.exec();}
 if(args.contains("--refused")){std::cerr<<std::string(4096,'x')<<"\nValueError: Missing optimized module\n";return 3;}
 int checks=0;auto check=[&](bool okay,const char* msg){++checks;if(!okay){std::cerr<<"FAIL "<<msg<<'\n';std::exit(1);}};auto program=app.applicationFilePath();
 {QObject owner;QString failure;pgn::engine::localRuntimeQuery(&owner,program,{"--refused"},{},[&](QJsonObject,QString error){failure=error;});check(waitFor(app,[&]{return !failure.isEmpty();}),"actual startup refusal arrives");check(failure=="Mining controller: ValueError: Missing optimized module","bounded cause survives instead of generic invalid response");}
 for(const auto& flag:QStringList{"--reply","--bad","--missing"}){QObject owner;int calls=0;QString error;QJsonObject output;pgn::engine::localRuntimeQuery(&owner,flag=="--missing"?program+".absent":program,flag=="--missing"?QStringList{}:QStringList{flag},{},[&](QJsonObject v,QString e){++calls;output=v;error=e;});check(waitFor(app,[&]{return calls>0;}),"normal completion arrives");check(calls==1,"callback exactly once");check(flag=="--reply"?error.isEmpty()&&output.value("schema")=="QA_QUERY":!error.isEmpty(),"reply and error semantics preserved");app.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
 for(int repetition=0;repetition<3;++repetition){auto* owner=new QObject;int calls=0;pgn::engine::localRuntimeQuery(owner,program,{"--pending"},{},[&](QJsonObject,QString){++calls;});auto* child=owner->findChild<QProcess*>();const bool running=child&&(child->state()==QProcess::Running||child->waitForStarted(3000));if(!running)std::cerr<<"DEBUG pending child="<<child<<" calls="<<calls<<" directchildren="<<owner->children().size()<<" state="<<(child?int(child->state()):-1)<<" error="<<(child?child->errorString().toStdString():"none")<<"\n";check(running,"genuine pending child running");auto pid=child->processId();ChildExitProbe probe(pid);delete owner;check(calls==0,"destroyed owner cannot receive result/error");check(probe.stopped(),"only own pending child exited");}
 for(const auto& flag:QStringList{"--reply","--bad","--missing","--timeout"}){auto* owner=new QObject;int calls=0;QString error;pgn::engine::localRuntimeQuery(owner,flag=="--missing"?program+".absent":program,flag=="--missing"?QStringList{}:QStringList{flag=="--timeout"?QString("--pending"):flag},{},[&](QJsonObject,QString e){++calls;error=e;auto* dying=owner;owner=nullptr;delete dying;});if(flag=="--timeout"){auto* deadline=owner->findChild<QTimer*>();check(deadline,"timeout timer exists");deadline->setInterval(40);}check(waitFor(app,[&]{return calls>0;}),"consumer deletes owner completion arrives");check(!owner&&calls==1,"consumer deletion safe and exactly once");check(flag=="--reply"?error.isEmpty():!error.isEmpty(),"consumer deletion preserves errors");if(flag=="--timeout")check(error.contains("timed out"),"real deadline path reached");app.processEvents();}
 std::cout<<"PASS "<<checks<<" local runtime query checks; real child processes, no GPU/network/wallet\n";
}

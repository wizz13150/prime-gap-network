#include "ui/mining_worker_close_attempt.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <stdexcept>
#include <string>
static void need(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static void wait(const std::function<bool()>& ready){QElapsedTimer timer;timer.start();while(!ready()&&timer.elapsed()<5000){QCoreApplication::processEvents();QThread::msleep(2);}need(ready(),"Actual child/event-loop deadline");}
int main(int argc,char** argv){
 if(argc==3&&std::string{argv[1]}=="--worker"){std::string line;while(std::getline(std::cin,line)){if(line.find("quit")!=std::string::npos){const std::string mode{argv[2]};const auto colon=mode.find(':');if(colon!=std::string::npos)QThread::msleep(std::stoi(mode.substr(colon+1)));return std::stoi(mode);}}return 99;}
 QCoreApplication app(argc,argv);
 try{
  QObject window;QProcess process;pgn::ui::MiningWorkerCloseAttempt attempt(&window,&process);
  int waiting=0,ready=0,failed=0;attempt.onWaiting=[&]{++waiting;};attempt.onReady=[&]{++ready;};attempt.onFailure=[&](const QString&){++failed;};
  const auto start=[&](int code){process.start(app.applicationFilePath(),{"--worker",QString::number(code)});need(process.waitForStarted(3000),"Actual test worker start");};
  const auto closing=[&]{window.setProperty("pgn_closing",true);window.setProperty("pgn_update_shutdown",true);};
  start(7);closing();need(!attempt.requestClose(true)&&!attempt.requestClose(true)&&waiting==1,"One quit command per close attempt");wait([&]{return failed==1;});need(ready==0&&!window.property("pgn_closing").toBool()&&!window.property("pgn_update_shutdown").toBool()&&!window.property("pgn_update_shutdown_error").toString().isEmpty(),"Failed close unlocks only UI admission, without success");
  start(0);process.write("quit\n");wait([&]{return process.state()==QProcess::NotRunning;});QCoreApplication::processEvents();need(ready==0,"Failed attempt cannot close a later ordinary worker");
  start(0);closing();need(!attempt.requestClose(true),"Explicit retry requests real shutdown");wait([&]{return ready==1;});need(failed==1,"Successful retry does not manufacture an error");
  // A completed old process may have queued a window-close callback. Launch a
  // fresh process in the same finished delivery before that timer can execute.
  bool restart=true;auto connection=QObject::connect(&process,&QProcess::finished,&window,[&](int,QProcess::ExitStatus){if(restart){restart=false;start(0);}});
  start(0);closing();need(!attempt.requestClose(true),"Delayed-completion scenario starts shutdown");wait([&]{return !restart&&process.state()==QProcess::Running;});QCoreApplication::processEvents();need(ready==1,"Queued old permission cannot close a new process generation");QObject::disconnect(connection);process.write("quit\n");wait([&]{return process.state()==QProcess::NotRunning;});QCoreApplication::processEvents();need(ready==1,"Ordinary new worker exit stays independent");
  start(0);closing();need(!attempt.requestClose(true),"Crash scenario pending");process.kill();wait([&]{return failed==2;});need(ready==1&&!window.property("pgn_closing").toBool(),"Actual abnormal child termination is not close success");
  {
   QProcess second;pgn::ui::MiningWorkerCloseAttempt other(&window,&second);int otherReady=0;other.onReady=[&]{++otherReady;};
   second.start(app.applicationFilePath(),{"--worker","0:200"});need(second.waitForStarted(3000),"Second real owned test worker");start(7);closing();need(!other.requestClose(true)&&!attempt.requestClose(true),"Multiple owned close attempts pending");wait([&]{return failed==3&&second.state()==QProcess::NotRunning;});QCoreApplication::processEvents();need(otherReady==0&&ready==1,"One failed worker invalidates other queued close permissions");
  }
  closing();pgn::ui::abortMiningUpdateShutdown(&window,"Owned Core refused shutdown");need(!window.property("pgn_closing").toBool()&&!window.property("pgn_update_shutdown").toBool()&&window.property("pgn_update_shutdown_error").toString()=="Owned Core refused shutdown","Core refusal releases admission and preserves cause");
  std::cout<<"PASS real child nonzero/crash, explicit retry, stale queued completion and Core refusal; no GPU/network/wallet\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

#include "mining/network_controller_recovery.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QProcess>
#include <iostream>
#include <stdexcept>
#include <vector>
static void need(bool okay,const char* why){if(!okay)throw std::runtime_error(why);}
int main(int argc,char** argv){
 if(argc>1&&QString::fromLocal8Bit(argv[1])=="--transport"){std::string line;while(std::getline(std::cin,line))if(line=="quit")return 255;return 0;}
 QCoreApplication app(argc,argv);QProcess process;bool allowed=true;std::vector<bool> launches;int disconnected=0;
 pgn::mining::NetworkControllerRecovery recovery(&process,nullptr,20,100);
 recovery.mayReconnect=[&]{return allowed;};recovery.disconnected=[&]{++disconnected;};
 recovery.reconnect=[&](bool wanted){launches.push_back(wanted);recovery.launching();process.setProgram(QCoreApplication::applicationFilePath());process.setArguments({"--transport"});process.start();};
 const auto wait=[&](auto condition,int milliseconds=2000){QElapsedTimer clock;clock.start();while(!condition()&&clock.elapsed()<milliseconds){app.processEvents();QThread::msleep(2);}need(condition(),"Recovery did not reach bounded expected state");};
 recovery.intent(true);need(recovery.restartIfClosed(),"ON could not start an ended control transport");wait([&]{return process.state()==QProcess::Running;});need(launches.size()==1&&launches.back(),"User intent was lost");recovery.phase("Searching",true);
 process.write("quit\n");wait([&]{return launches.size()==2&&process.state()==QProcess::Running;});need(disconnected==1&&launches.back(),"Broken transport was not automatically restored");recovery.phase("Searching",true);
 recovery.intent(false);process.write("quit\n");wait([&]{return launches.size()==3&&process.state()==QProcess::Running;});need(!launches.back(),"OFF reconnect accidentally requested mining");recovery.phase("Paused",true);
 recovery.phase("Needs attention",true);process.write("quit\n");wait([&]{return process.state()==QProcess::NotRunning;});QElapsedTimer quiet;quiet.start();while(quiet.elapsed()<160){app.processEvents();QThread::msleep(2);}need(launches.size()==3&&!recovery.retryPending(),"Scientific or terminal fault was silently retried");
 recovery.intent(true);allowed=false;need(!recovery.restartIfClosed()&&launches.size()==3,"Another owner/closing guard was ignored");allowed=true;need(recovery.restartIfClosed(),"Explicit retry did not clear a terminal fault");wait([&]{return process.state()==QProcess::Running;});need(launches.size()==4,"Manual retry duplicated launch");
 recovery.close();process.write("quit\n");wait([&]{return process.state()==QProcess::NotRunning;});need(!recovery.retryPending(),"Closing restarted a control process");
 QProcess absent;pgn::mining::NetworkControllerRecovery failed(&absent,nullptr,20,100);int failedStarts=0;failed.reconnect=[&](bool){++failedStarts;failed.launching();absent.setProgram(QCoreApplication::applicationFilePath()+".missing");absent.start();};failed.intent(true);need(failed.restartIfClosed(),"Failed-start fixture not dispatched");wait([&]{return failed.needsAttention();});need(failedStarts==1&&!failed.retryPending(),"Missing executable entered a retry loop");failed.close();
 for(const bool wanted:{true,false}){
  QProcess contended;pgn::mining::NetworkControllerRecovery waiting(&contended,nullptr,20,100);int attempts=0;std::vector<bool> intents;
  waiting.reconnect=[&](bool on){++attempts;intents.push_back(on);waiting.launching();contended.setProgram(QCoreApplication::applicationFilePath());contended.setArguments({"--transport"});contended.start();};
  QObject::connect(&contended,&QProcess::started,&app,[&]{if(attempts<=2){waiting.phase("Waiting for controller",true);contended.write("quit\n");}else waiting.phase("Searching",true);});
  waiting.intent(wanted);need(waiting.restartIfClosed(),"Contended transport did not start");wait([&]{return attempts==3&&contended.state()==QProcess::Running;});
  need(!waiting.needsAttention()&&intents==std::vector<bool>(3,wanted),"Temporary owner contention lost intent or became terminal");
  waiting.close();contended.write("quit\n");wait([&]{return contended.state()==QProcess::NotRunning;});need(!waiting.retryPending(),"Closed busy transport restarted");
 }
 std::cout<<"PASS owned transport recovery:ON after closed,automatic reconnect,OFF intent,semantic faults,ownership guard,manual retry,close and failed-start; no GPU or Core touched\n";
}

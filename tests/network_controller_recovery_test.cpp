#include "mining/network_controller_recovery.h"
#include "mining/worker_control_arguments.h"
#include "ui/mining_attention_status.h"
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
 QCoreApplication app(argc,argv);
 need(pgn::ui::miningAttentionStatus("Audit 2: Active parent changed after submission").contains("retrying"),"Recoverable audit publication had no human explanation");
 need(!pgn::ui::miningAttentionStatus("Session index WAL budget reached").contains("100%"),"Storage failure looked like completed work");
 need(pgn::ui::miningAttentionStatus("Invalid audit actor signature").contains("rejected"),"Signature rejection was hidden as success");
 need(pgn::ui::miningAttentionStatus({}).contains("Diagnostics"),"Missing diagnostic reason had no recovery route");
 const QStringList original{"-o","StrictHostKeyChecking=yes","-o","ServerAliveInterval=0","user@host","remote-program","--network"};
 const auto hardened=pgn::mining::workerControlArguments("C:\\Windows\\System32\\OpenSSH\\ssh.exe",original);
 need(hardened==QStringList({"-o","ServerAliveInterval=5","-o","ServerAliveCountMax=2","-o","ConnectTimeout=5"})+original,"SSH timeout arguments changed command/authentication boundary");
 need(pgn::mining::workerControlArguments("/usr/bin/ssh",original)==hardened,"Linux SSH did not receive protocol probes");
 need(pgn::mining::workerControlArguments("C:/PGN/horizon-engine-host.exe",original)==original,"Local worker arguments changed");
QProcess process;bool allowed=true;std::vector<bool> launches;int disconnected=0;
 pgn::mining::NetworkControllerRecovery recovery(&process,nullptr,20,100);
 recovery.mayReconnect=[&]{return allowed;};recovery.disconnected=[&]{++disconnected;};
 recovery.reconnect=[&](bool wanted){launches.push_back(wanted);recovery.launching();process.setProgram(QCoreApplication::applicationFilePath());process.setArguments({"--transport"});process.start();};
 const auto wait=[&](auto condition,int milliseconds=2000){QElapsedTimer clock;clock.start();while(!condition()&&clock.elapsed()<milliseconds){app.processEvents();QThread::msleep(2);}need(condition(),"Recovery did not reach bounded expected state");};
 recovery.intent(true);need(recovery.restartIfClosed(),"ON could not start an ended control transport");wait([&]{return process.state()==QProcess::Running;});need(launches.size()==1&&launches.back(),"User intent was lost");recovery.phase("Searching",true);
 process.write("quit\n");wait([&]{return launches.size()==2&&process.state()==QProcess::Running;});need(disconnected==1&&launches.back(),"Broken transport was not automatically restored");recovery.phase("Searching",true);
 recovery.intent(false);process.write("quit\n");wait([&]{return launches.size()==3&&process.state()==QProcess::Running;});need(!launches.back(),"OFF reconnect accidentally requested mining");recovery.phase("Paused",true);
 recovery.phase("Needs attention",true);process.write("quit\n");wait([&]{return process.state()==QProcess::NotRunning;});QElapsedTimer quiet;quiet.start();while(quiet.elapsed()<160){app.processEvents();QThread::msleep(2);}need(launches.size()==3&&!recovery.retryPending(),"Scientific or terminal fault was silently retried");
 recovery.intent(true);allowed=false;need(!recovery.restartIfClosed()&&launches.size()==3,"Another owner/closing guard was ignored");allowed=true;need(recovery.restartIfClosed(),"Explicit retry did not clear a terminal fault");wait([&]{return process.state()==QProcess::Running;});need(launches.size()==4,"Manual retry duplicated launch");
 recovery.phase("Needs attention",false);need(recovery.needsAttention(),"Fault did not remain visible");
 recovery.phase("Searching",false);need(recovery.needsAttention(),"Stale frame cleared fault");
 recovery.phase("Paused",true);need(recovery.needsAttention(),"Pause acknowledgement cleared fault");
 recovery.phase("Searching",true);need(!recovery.needsAttention()&&recovery.wanted(),"Fresh running work stayed hidden until OFF/ON");
 recovery.intent(false);recovery.phase("Needs attention",false);recovery.phase("Searching",true);
 need(recovery.needsAttention()&&!recovery.wanted(),"Late running observation overrode explicit OFF");
 recovery.close();process.write("quit\n");wait([&]{return process.state()==QProcess::NotRunning;});need(!recovery.retryPending(),"Closing restarted a control process");
 QProcess silent;pgn::mining::NetworkControllerRecovery silence(&silent,nullptr,20,100,60);int silences=0,starts=0;
 silence.onSilent=[&]{++silences;};silence.reconnect=[&](bool){++starts;silence.launching();silent.start(QCoreApplication::applicationFilePath(),{"--transport"});};
 silence.intent(true);need(silence.restartIfClosed(),"Silent fixture could not start");wait([&]{return silent.state()==QProcess::Running;});
 silence.phase("Searching",true);wait([&]{return silences==1;});
 need(starts==1&&silent.state()==QProcess::Running&&silence.wanted(),"Silence duplicated or killed a worker / lost ON");
 silence.phase("Paused",true);silence.intent(false);wait([&]{return silences==2;});need(!silence.wanted()&&starts==1,"Silence overrode manual OFF");
 silence.close();silent.write("quit\n");wait([&]{return silent.state()==QProcess::NotRunning;});
 QProcess absent;pgn::mining::NetworkControllerRecovery failed(&absent,nullptr,20,100);int failedStarts=0;failed.reconnect=[&](bool){++failedStarts;failed.launching();absent.setProgram(QCoreApplication::applicationFilePath()+".missing");absent.start();};failed.intent(true);need(failed.restartIfClosed(),"Failed-start fixture not dispatched");wait([&]{return failed.needsAttention();});need(failedStarts==1&&!failed.retryPending(),"Missing executable entered a retry loop");failed.close();
 for(const auto& waitingPhase:QStringList{"Waiting for controller","Waiting for connection"})for(const bool wanted:{true,false}){
  QProcess contended;pgn::mining::NetworkControllerRecovery waiting(&contended,nullptr,20,100);int attempts=0;std::vector<bool> intents;
  waiting.reconnect=[&](bool on){++attempts;intents.push_back(on);waiting.launching();contended.setProgram(QCoreApplication::applicationFilePath());contended.setArguments({"--transport"});contended.start();};
  QObject::connect(&contended,&QProcess::started,&app,[&]{if(attempts<=2){waiting.phase(waitingPhase,true);contended.write("quit\n");}else waiting.phase("Searching",true);});
  waiting.intent(wanted);need(waiting.restartIfClosed(),"Contended transport did not start");wait([&]{return attempts==3&&contended.state()==QProcess::Running;});
  need(!waiting.needsAttention()&&intents==std::vector<bool>(3,wanted),"Temporary owner contention lost intent or became terminal");
  waiting.close();contended.write("quit\n");wait([&]{return contended.state()==QProcess::NotRunning;});need(!waiting.retryPending(),"Closed busy transport restarted");
 }
 std::cout<<"PASS owned transport recovery:ON after closed,automatic reconnect,OFF intent,semantic faults,ownership guard,manual retry,close and failed-start; no GPU or Core touched\n";
}

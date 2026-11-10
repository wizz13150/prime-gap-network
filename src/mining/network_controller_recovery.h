#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
#include <algorithm>
namespace pgn::mining {
// Reconnect the owned control transport; this never owns or kills a GPU job.
// A new controller must still satisfy its durable-session and GPU-lease checks.
class NetworkControllerRecovery final:public QObject {
 QProcess* process_;QTimer retry_,freshness_;QElapsedTimer lastPacket_;bool silent_{};int silenceMs_;bool managed_{},wanted_{},terminal_{};unsigned failures_{};int base_,maximum_;
 bool allowed()const{return !mayReconnect||mayReconnect();}
 void schedule(){if(!managed_||terminal_||!allowed()||retry_.isActive())return;const auto delay=std::min(maximum_,base_*(1<<std::min(5U,failures_++)));if(onWaiting)onWaiting(delay);retry_.start(delay);}
public:
 std::function<void(bool)> reconnect;
 std::function<void()> disconnected;
 std::function<void(int)> onWaiting;
 std::function<void()> onSilent;
 std::function<bool()> mayReconnect;
 explicit NetworkControllerRecovery(QProcess* process,QObject* parent=nullptr,int baseMs=1000,int maximumMs=30000,int silenceMs=15000):QObject(parent),process_(process),retry_(this),freshness_(this),silenceMs_(std::max(1,silenceMs)),base_(baseMs),maximum_(maximumMs){
  retry_.setSingleShot(true);
  freshness_.setInterval(std::min(1000,silenceMs_));
  connect(&freshness_,&QTimer::timeout,this,[this]{
   if(managed_&&!terminal_&&allowed()&&process_->state()==QProcess::Running&&lastPacket_.isValid()&&lastPacket_.elapsed()>=silenceMs_&&!silent_){silent_=true;if(onSilent)onSilent();}
  });
  connect(&retry_,&QTimer::timeout,this,[this]{if(!allowed()){schedule();return;}restartIfClosed();});
  connect(process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int,QProcess::ExitStatus){freshness_.stop();if(!managed_||!allowed())return;if(disconnected)disconnected();schedule();});
  connect(process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){terminal_=true;retry_.stop();if(managed_&&allowed()&&disconnected)disconnected();}});
 }
 void intent(bool on){managed_=true;wanted_=on;terminal_=false;failures_=0;retry_.stop();}
 void launching(){managed_=true;retry_.stop();silent_=false;lastPacket_.start();freshness_.start();}
 void phase(const QString& phase,bool fresh){silent_=false;lastPacket_.start();if(phase=="Needs attention"){terminal_=true;retry_.stop();}else if(fresh&&(phase=="Searching"||phase=="Paused")){failures_=0;}}
 bool restartIfClosed(){if(!managed_||terminal_||!allowed()||process_->state()!=QProcess::NotRunning||!reconnect)return false;retry_.stop();reconnect(wanted_);return true;}
 bool needsAttention()const{return terminal_;}
 bool wanted()const{return wanted_;}
 bool retryPending()const{return retry_.isActive();}
 void close(){managed_=false;terminal_=true;retry_.stop();freshness_.stop();}
};
}

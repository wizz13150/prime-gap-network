#pragma once
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QVariant>
#include <functional>
namespace pgn::ui {
inline void abortMiningUpdateShutdown(QObject* window,const QString& error){
    if(!window)return;
    window->setProperty("pgn_worker_close_epoch",window->property("pgn_worker_close_epoch").toULongLong()+1);
    window->setProperty("pgn_update_shutdown_error",error.left(512));
    window->setProperty("pgn_update_shutdown",false);
    window->setProperty("pgn_closing",false);
}
// Permission to close belongs to the process that actually finished. It must
// never survive a failed shutdown or be inherited by a newly launched worker.
class MiningWorkerCloseAttempt final:public QObject {
    QPointer<QObject> window_;
    QPointer<QProcess> process_;
    bool requested_{},allowed_{};
    qulonglong epoch_{};
    bool current()const{return window_&&epoch_==window_->property("pgn_worker_close_epoch").toULongLong();}
public:
    std::function<void()> onWaiting,onReady;
    std::function<void(const QString&)> onFailure;
    MiningWorkerCloseAttempt(QObject* window,QProcess* process):QObject(window),window_(window),process_(process){
        connect(process,&QProcess::stateChanged,this,[this](QProcess::ProcessState state){if(state==QProcess::Starting){requested_=false;allowed_=false;}});
        connect(process,&QProcess::finished,this,[this](int code,QProcess::ExitStatus status){
            if(!requested_)return;
            requested_=false;
            if(!current()){allowed_=false;return;}
            if(code==0&&status==QProcess::NormalExit){
                allowed_=true;
                QTimer::singleShot(0,this,[this]{if(allowed_&&current()&&process_&&process_->state()==QProcess::NotRunning&&onReady)onReady();});
                return;
            }
            allowed_=false;
            const auto reason=QString("Mining controller did not close normally (exit %1%2). Window kept open; retry after reviewing the mining card.").arg(code).arg(status==QProcess::CrashExit?QString(", crash"):QString{});
            abortMiningUpdateShutdown(window_,reason);
            if(onFailure)onFailure(reason);
        });
    }
    bool requestClose(bool networkMode){
        if(!current()){requested_=false;allowed_=false;}
        if(allowed_||!networkMode||!process_||process_->state()==QProcess::NotRunning)return true;
        if(!requested_){epoch_=window_->property("pgn_worker_close_epoch").toULongLong();requested_=true;process_->write("{\"action\":\"quit\"}\n");if(onWaiting)onWaiting();}
        return false;
    }
};
}

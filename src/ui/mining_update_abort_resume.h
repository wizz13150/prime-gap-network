#pragma once
#include <QDynamicPropertyChangeEvent>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariant>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>

namespace pgn::ui {
inline QJsonArray mergeMiningUpdateChoices(const QJsonArray& pending,const QJsonArray& live){
    auto result=pending;
    for(const auto& value:live){
        const auto id=value.toObject().value("device").toString();
        qsizetype found=-1;
        for(qsizetype i=0;i<result.size();++i)if(result.at(i).toObject().value("device").toString().compare(id,Qt::CaseInsensitive)==0){found=i;break;}
        if(found<0)result.append(value);else result[found]=value;
    }
    return result;
}
// Persist the latest requested GPU intent, including a recoverable paused or
// failed backend. Physical RUNNING is not permission to override a later OFF.
inline QJsonArray captureMiningUpdateChoices(const QJsonArray& pending,
                                            const QJsonArray& cpu,
                                            const QJsonObject& devices){
    auto result=mergeMiningUpdateChoices(pending,cpu);
    for(auto device=devices.begin();device!=devices.end();++device){
        const auto entry=device.value().toObject();
        const auto wanted=entry.value("wanted");
        if(!wanted.isBool())continue;
        for(qsizetype i=result.size();i-->0;){
            const auto saved=result.at(i).toObject();
            if(saved.value("kind")=="gpu"&&saved.value("device").toString().compare(device.key(),Qt::CaseInsensitive)==0)
                result.removeAt(i);
        }
        if(wanted.toBool())result.append(QJsonObject{{"kind","gpu"},{"device",device.key()}});
    }
    return result;
}
// An aborted update may restore only its captured mining choices. Ordinary
// closes, successful shutdowns and callbacks from an older attempt grant none.
class MiningUpdateAbortResume final:public QObject {
    QPointer<QObject> window_;
    bool updating_{};
    qulonglong generation_{};
public:
    std::function<void()> onAbort;
    explicit MiningUpdateAbortResume(QObject* window):QObject(window),window_(window){window->installEventFilter(this);}
    bool eventFilter(QObject* object,QEvent* event)override{
        if(object==window_&&event->type()==QEvent::DynamicPropertyChange
           &&static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName()=="pgn_update_shutdown"){
            if(window_->property("pgn_update_shutdown").toBool()){
                updating_=true;++generation_;
            }else if(updating_){
                updating_=false;
                const auto generation=generation_;
                const auto epoch=window_->property("pgn_worker_close_epoch").toULongLong();
                // Let the failure handler finish before considering any ON.
                QTimer::singleShot(0,this,[this,generation,epoch]{
                    if(!window_||generation!=generation_
                       ||epoch!=window_->property("pgn_worker_close_epoch").toULongLong()
                       ||window_->property("pgn_update_shutdown").toBool()
                       ||window_->property("pgn_closing").toBool()
                       ||window_->property("pgn_update_shutdown_error").toString().isEmpty())return;
                    if(onAbort)onAbort();
                });
            }
        }
        return QObject::eventFilter(object,event);
    }
};
}

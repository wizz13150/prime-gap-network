#pragma once
#include <QObject>
#include <QEvent>
#include <QCloseEvent>
#include <functional>
namespace pgn::ui {
// A close waits for the owned worker to save/release, never kills GPU work.
class MiningWorkerCloseGuard final:public QObject {
    std::function<bool()> ready_;
public:
    MiningWorkerCloseGuard(QObject* window,std::function<bool()> ready):QObject(window),ready_(std::move(ready)){window->installEventFilter(this);}
    bool eventFilter(QObject* object,QEvent* event)override{if(event->type()==QEvent::Close&&!ready_()){static_cast<QCloseEvent*>(event)->ignore();return true;}return QObject::eventFilter(object,event);}
};
}

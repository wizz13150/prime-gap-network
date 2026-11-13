#pragma once
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <functional>
#include <memory>
namespace pgn::social {
// Social-only FIFO per public script, round-robin between scripts. No miner/RPC
// admission quota. At most2 pipelines (one/script) and32 waiting jobs globally.
class RpcQueue final:public QObject {
public:
 using Done=std::function<void()>;using Job=std::function<void(Done)>;
 explicit RpcQueue(QObject* parent=nullptr):QObject(parent){}
 bool enqueue(QString script,Job job){
  if(script.size()>256||waiting_>=32)return false;
  if(!pending_.contains(script)){pending_.insert(script,{});turns_.enqueue(script);}
  pending_[script].enqueue(std::move(job));++waiting_;pump();return true;
 }
 int active()const{return active_.size();}
 int waiting()const{return waiting_;}
private:
 QMap<QString,QQueue<Job>> pending_;QQueue<QString> turns_;QSet<QString> active_;int waiting_{};
 void pump(){
  int searched=turns_.size();
  while(active_.size()<2&&!turns_.isEmpty()&&searched-->0){
   const auto script=turns_.dequeue();
   if(active_.contains(script)){turns_.enqueue(script);continue;}
   auto job=pending_[script].dequeue();--waiting_;
   if(pending_[script].isEmpty())pending_.remove(script);else turns_.enqueue(script);
   active_.insert(script);const auto completed=std::make_shared<bool>(false);const QPointer<RpcQueue> weak(this);
   job([weak,script,completed]{if(!weak||*completed)return;*completed=true;weak->active_.remove(script);QTimer::singleShot(0,weak,[weak]{if(weak)weak->pump();});});
  }
 }
};
}


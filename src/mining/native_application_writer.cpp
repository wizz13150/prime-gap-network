#include "mining/native_application_writer.h"
#include <QPointer>
#include <QThread>
#include <QUuid>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pgn::mining {
class NativeApplicationWriter::Impl {
public:
    struct Item {NativeApplicationOperation kind;Work work;Completion complete;bool barrier{};};
    struct Delivery {std::mutex mutex;NativeApplicationWriter* target{};};
    explicit Impl(NativeApplicationWriter* q):q(q),delivery(std::make_shared<Delivery>()){delivery->target=q;}
    NativeApplicationWriter* q;
    std::shared_ptr<Delivery> delivery;
    std::deque<Item> queue;
    std::vector<Completion> closeWaiters;
    QString closeId;
    bool running{},scheduled{},barrierDone{};
    quint64 serial{};
    void owner()const{if(QThread::currentThread()!=q->thread())throw std::logic_error("Native application writer requires owning thread");}
    static void notify(const Completion& complete,const NativeApplicationWriteResult& result){
        // A presentation callback cannot unwind Qt's dispatch or erase the
        // writer's next result. This never turns its exception into success.
        try{if(complete)complete(result);}catch(...){}
    }
    void schedule(){
        if(scheduled||running||queue.empty())return;
        scheduled=true;
        QMetaObject::invokeMethod(q,[this]{scheduled=false;drain();},Qt::QueuedConnection);
    }
    void finish(quint64 token,Completion complete,QString error){
        if(!running||token!=serial)return; // duplicate/late completion is NOT a retry
        running=false;
        const QPointer<NativeApplicationWriter> guard(q);
        notify(complete,{true,error.left(400)});
        if(guard)schedule();
    }
    void drain(){
        owner();if(running||queue.empty())return;
        auto item=std::move(queue.front());queue.pop_front();
        if(item.barrier){
            barrierDone=true;auto waiters=std::move(closeWaiters);closeWaiters.clear();
            const QPointer<NativeApplicationWriter> guard(q);
            for(const auto& waiter:waiters){notify(waiter,{true,{}});if(!guard)return;}
            schedule();return;
        }
        if(!closeId.isEmpty()&&item.kind==NativeApplicationOperation::Mutation){
            const QPointer<NativeApplicationWriter> guard(q);
            notify(item.complete,{false,"Application close freeze refused queued mutation; prepared state requires reconciliation"});
            if(guard)schedule();return;
        }
        running=true;const auto token=++serial;
        const QPointer<NativeApplicationWriter> guard(q);
        // Transport completions may arrive on a worker. Delivery always returns
        // to the writer thread and cannot reenter/overlap the current operation.
        auto done=[delivery=delivery,token,complete=item.complete](QString error){
            // Keep posting and owner destruction mutually exclusive; testing a
            // QPointer on another thread alone would leave a destruction race.
            std::lock_guard lock(delivery->mutex);auto* target=delivery->target;
            if(!target)return;
            QMetaObject::invokeMethod(target,[target,token,complete,error=std::move(error)]{
                target->d_->finish(token,complete,error);
            },Qt::QueuedConnection);
        };
        try{item.work(done);}
        catch(const std::exception& e){done(QString::fromUtf8(e.what()).left(400));}
        catch(...){done("Native operation threw; outcome unconfirmed, no automatic retry");}
    }
};
NativeApplicationWriter::NativeApplicationWriter(QObject* parent):QObject(parent),d_(std::make_unique<Impl>(this)){}
NativeApplicationWriter::~NativeApplicationWriter(){d_->owner();std::lock_guard lock(d_->delivery->mutex);d_->delivery->target=nullptr;}
bool NativeApplicationWriter::enqueue(NativeApplicationOperation kind,Work work,Completion complete){
    d_->owner();
    if(!work||!complete||d_->queue.size()>=size_t(PendingLimit)||
       (kind!=NativeApplicationOperation::Read&&kind!=NativeApplicationOperation::Mutation&&kind!=NativeApplicationOperation::Stop)||
       (!d_->closeId.isEmpty()&&kind==NativeApplicationOperation::Mutation))return false;
    d_->queue.push_back({kind,std::move(work),std::move(complete),false});d_->schedule();return true;
}
bool NativeApplicationWriter::prepareClose(const QString& id,Completion complete){
    d_->owner();const auto parsed=QUuid::fromString(id);
    if(!complete||parsed.isNull()||parsed.toString(QUuid::WithoutBraces)!=id||(!d_->closeId.isEmpty()&&d_->closeId!=id))return false;
    if(d_->closeId.isEmpty()){
        // Reserve the barrier independently of the bounded public command
        // queue: overload can refuse commands but must never lose close intent.
        d_->closeId=id;d_->closeWaiters.push_back(std::move(complete));
        d_->queue.push_back({NativeApplicationOperation::Read,{},{},true});d_->schedule();return true;
    }
    if(!d_->barrierDone){
        // No unbounded duplicate-waiter accumulation. Caller retains its first
        // correlation and may reconcile after the barrier completes.
        return false;
    }
    const QPointer<NativeApplicationWriter> guard(this);
    QMetaObject::invokeMethod(this,[guard,complete=std::move(complete)]{if(guard)Impl::notify(complete,{true,{}});},Qt::QueuedConnection);
    return true;
}
bool NativeApplicationWriter::frozen()const{d_->owner();return !d_->closeId.isEmpty();}
QString NativeApplicationWriter::closeRequestId()const{d_->owner();return d_->closeId;}
}

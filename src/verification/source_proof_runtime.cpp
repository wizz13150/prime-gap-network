#include "verification/source_proof_runtime.h"
#include <QFutureWatcher>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>

namespace pgn::verification {
namespace {
struct Turn {SourceProofPage page;SourceProofScan scan;bool scanned{};quint64 generation{};};
}
class SourceProofRuntime::Impl {
public:
    SourceProofRuntime* owner;QString source,journal,wantedRun,signature,continuation,scanAfter;
    SourceProofPolicy policy;QThreadPool pool;QFutureWatcher<Turn>* watcher;
    std::shared_ptr<std::atomic_bool> cancelled=std::make_shared<std::atomic_bool>(false);
    bool enabled{true},pauseCancelPending{},dirty{},scanPending{},scheduled{},busy{},stopping{},isStopped{},isHeld{};quint64 scanGeneration{};
    Impl(SourceProofRuntime* q,QString input,QString output,SourceProofPolicy p):owner(q),source(std::move(input)),journal(std::move(output)),policy(p),watcher(new QFutureWatcher<Turn>(q)){
        pool.setMaxThreadCount(1);pool.setThreadPriority(QThread::LowPriority);
        QObject::connect(watcher,&QFutureWatcher<Turn>::finished,q,[this]{finished();});
    }
    ~Impl(){cancelled->store(true,std::memory_order_relaxed);QObject::disconnect(watcher,nullptr,owner,nullptr);pool.waitForDone();}
    void thread()const{if(QThread::currentThread()!=owner->thread())throw std::logic_error("Proof runtime creating-thread API required");}
    void drained(){if(isStopped)return;isStopped=true;if(owner->onStopped)owner->onStopped();}
    void schedule(){
        if(!enabled||stopping||isHeld||scheduled||busy||(!dirty&&continuation.isEmpty()&&!scanPending))return;
        scheduled=true;QTimer::singleShot(0,owner,[this]{
            scheduled=false;if(stopping){if(!busy)drained();return;}if(!enabled||isHeld||busy)return;
            QString run;
            if(dirty){run=wantedRun;dirty=false;}else if(!continuation.isEmpty()){run=continuation;continuation.clear();}
            if(continuation==run)continuation.clear(); // A newer notification may select the same queued continuation.
            const bool scan=run.isEmpty();const auto after=scanAfter;const auto generation=scanGeneration;
            const auto sourceCopy=source,journalCopy=journal;const auto policyCopy=policy;const auto token=cancelled;
            busy=true; // Only finished() clears this, never an early isRunning() transition.
            watcher->setFuture(QtConcurrent::run(&pool,[sourceCopy,journalCopy,policyCopy,token,run,scan,after,generation]{
                Turn turn;turn.scanned=scan;turn.generation=generation;
                QString chosen=run;
                if(scan){turn.scan=scanNativeSourceProofWork(sourceCopy,journalCopy,policyCopy,after,token.get());chosen=turn.scan.run;
                    if(turn.scan.cancelled||!turn.scan.error.isEmpty()){turn.page.cancelled=turn.scan.cancelled;turn.page.error=turn.scan.error;return turn;}}
                if(!chosen.isEmpty())turn.page=certifyNativeSourcePage(sourceCopy,journalCopy,chosen,policyCopy,token.get());
                return turn;
            }));
        });
    }
    void finished(){
        const auto turn=watcher->result();busy=false;
        if(stopping){drained();return;}
        if(!enabled||(pauseCancelPending&&turn.page.cancelled)){
            pauseCancelPending=false;dirty=!wantedRun.isEmpty();if(!turn.page.run.isEmpty()&&turn.page.more)continuation=turn.page.run;
            if(enabled)schedule();return; // Durable cursor retains unfinished work; pause is not an audit failure.
        }
        pauseCancelPending=false;
        if(!turn.page.error.isEmpty()||turn.page.cancelled){isHeld=true;if(owner->onPage)owner->onPage(turn.page);return;}
        if(turn.scanned&&turn.generation==scanGeneration){
            // Do NOT move past a source selected by discovery until its pages
            // catch up. Preemption by a newer live run cannot lose this one.
            if(!turn.page.more)scanAfter=turn.scan.afterRun;
            scanPending=turn.scan.more||turn.page.more;
        }
        if(turn.page.more&&(turn.page.run==wantedRun||continuation.isEmpty()))continuation=turn.page.run;
        if(!turn.page.run.isEmpty()&&owner->onPage)owner->onPage(turn.page);
        // Callback may have called stop/retry. No unbounded wait or idle loop:
        // only actual more records, a new event, or a changing metadata key.
        if(!stopping)schedule();
    }
};
SourceProofRuntime::SourceProofRuntime(QString source,QString journal,SourceProofPolicy policy,QObject* parent):QObject(parent),d_(std::make_unique<Impl>(this,std::move(source),std::move(journal),policy)){}
SourceProofRuntime::~SourceProofRuntime()=default;
void SourceProofRuntime::observeNativeIndex(const QJsonObject& packet){
    d_->thread();if(d_->stopping||!packet.value("complete").toBool()||packet.value("archive_only").toBool())return;
    const auto current=packet.value("current").toObject();const auto run=current.value("scope").toString();
    static const QRegularExpression pattern("^[0-9a-f]{32}$");if(!pattern.match(run).hasMatch())return;
    const auto length=current.value("length").toObject();const auto revision=length.value("revision").toInteger(-1),points=length.value("points").toInteger(-1);
    if(revision<0||points<0)return;
    const auto key=run+"/"+QString::number(revision)+"/"+QString::number(points);if(key==d_->signature)return;
    if(run!=d_->wantedRun){d_->scanAfter.clear();d_->scanPending=true;++d_->scanGeneration;}
    d_->wantedRun=run;d_->signature=key;d_->dirty=true;d_->schedule();
}
void SourceProofRuntime::setEnabled(bool enabled){
 d_->thread();if(d_->stopping||d_->enabled==enabled)return;d_->enabled=enabled;
 if(!enabled){d_->pauseCancelPending=d_->busy;d_->cancelled->store(true,std::memory_order_relaxed);return;}
 d_->cancelled=std::make_shared<std::atomic_bool>(false);d_->dirty=!d_->wantedRun.isEmpty();d_->schedule();
}
void SourceProofRuntime::retry(){d_->thread();if(d_->stopping||!d_->isHeld)return;d_->isHeld=false;d_->dirty=!d_->wantedRun.isEmpty();d_->scanAfter.clear();d_->scanPending=true;++d_->scanGeneration;d_->schedule();}
void SourceProofRuntime::stop(){d_->thread();if(d_->stopping)return;d_->stopping=true;d_->cancelled->store(true,std::memory_order_relaxed);if(!d_->busy)d_->drained();}
bool SourceProofRuntime::stopped()const{return d_->isStopped;}
bool SourceProofRuntime::held()const{return d_->isHeld;}
} // namespace pgn::verification

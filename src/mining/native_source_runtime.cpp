#include "mining/native_source_runtime.h"
#include "mining/source_counter_grammar.h"
#include "mining/raw_counter_coalescing.h"
#include "mining/native_observation_session.h"
#include "mining/held_win32_lineage_sources.h"
#include "mining/source_history.h"
#include "mining/source_age.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace pgn::mining {
QJsonObject NativeSourceProgress::json() const {
    const auto name=[&]{switch(stage){case NativeSourceProgressStage::FrameReceived:return "frame_received";case NativeSourceProgressStage::SourceAdmitted:return "source_admitted";case NativeSourceProgressStage::IndexBegin:return "index_begin";case NativeSourceProgressStage::IndexDone:return "index_done";case NativeSourceProgressStage::IndexHeld:return "index_held";}return "unknown";};
    return {{"schema","pgn-native-source-progress-1"},{"type","source_progress"},{"stage",name()},
        {"request_id",requestId},{"run",run},{"source_epoch",QString::number(sourceEpoch)},
        {"offset",offset},{"next_offset",nextOffset},{"pending_bytes",pendingBytes?QJsonValue(*pendingBytes):QJsonValue(QJsonValue::Null)},
        {"elapsed_ms",elapsedMs},{"error",error.left(400)}};
}
QJsonObject NativeSourceTrace::json() const {
    const auto name=[&]{switch(stage){case NativeSourceTraceStage::InitializeBegin:return "initialize_begin";case NativeSourceTraceStage::InitializeDone:return "initialize_done";case NativeSourceTraceStage::InitializeHeld:return "initialize_held";case NativeSourceTraceStage::IndexBegin:return "index_begin";case NativeSourceTraceStage::IndexDone:return "index_done";case NativeSourceTraceStage::IndexHeld:return "index_held";case NativeSourceTraceStage::InventoryQueued:return "inventory_queued";case NativeSourceTraceStage::InventoryEntered:return "inventory_entered";case NativeSourceTraceStage::InventoryTransport:return "inventory_transport";case NativeSourceTraceStage::InventoryDelivered:return "inventory_delivered";}return "unknown";};
    return {{"schema","pgn-native-source-trace-1"},{"stage",name()},{"request_id",requestId},{"run",run},{"epoch",QString::number(epoch)},{"source_epoch",QString::number(sourceEpoch)},{"monotonic_ms",monotonicMs},{"error",error.left(400)},{"transport",transport?QJsonValue(transport->json()):QJsonValue(QJsonValue::Null)},{"offset",offset?QJsonValue(*offset):QJsonValue(QJsonValue::Null)},{"next_offset",nextOffset?QJsonValue(*nextOffset):QJsonValue(QJsonValue::Null)}};
}
namespace {
qint64 monotonicMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
[[noreturn]]void fail(const char* why){throw std::runtime_error(why);}
bool runValid(const QString& run){return run.size()==32&&QRegularExpression("^[0-9a-f]{32}$").match(run).hasMatch();}
bool uuidValid(const QString& id){return id.size()==36&&QRegularExpression("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$").match(id).hasMatch();}
NativeSourceAttemptStatus status(SshObservationStatus value){switch(value){case SshObservationStatus::Complete:return NativeSourceAttemptStatus::Complete;case SshObservationStatus::Failed:return NativeSourceAttemptStatus::Failed;case SshObservationStatus::TimedOut:return NativeSourceAttemptStatus::TimedOut;case SshObservationStatus::Cancelled:return NativeSourceAttemptStatus::Cancelled;}return NativeSourceAttemptStatus::Failed;}
struct IndexRequest {QString requestId,run;quint64 epoch{};std::optional<qint64> committed; qint64 offset{},nextOffset{},firstMonotonic{};std::optional<qint64> pending;};
struct IndexCompletion {IndexRequest request;std::optional<SourceIndexSnapshot> snapshot;QString error;};
struct State {
    std::atomic_bool stopping{},sourceCancelled{};
    std::atomic<quint64> cancelledLineageEpoch{};
    std::mutex mutex;
    std::optional<NativeSourcePublication> latest;
    QString sourceRun;std::optional<qint64> sourceNext; // actually paid session cursor only
    std::optional<qint64> sourceCommitted;
    std::atomic<quint64> indexEpoch{};
    std::optional<IndexRequest> latestIndex;bool indexBusy{};
    bool deliveryPosted{};
};
// Lifetime separate from UI owner. Worker queues only to this shared context;
// QPointer guards are inspected ONLY on its creating/UI thread, not raced from
// the worker against a UI QObject destructor.
struct InventoryAcquisition {
    QString requestId,expected;quint64 epoch{},sourceEpoch{};bool sourceCancelled{};
    SshObservationResult transport;std::optional<SshObservationFrame> frame;
};
struct Delivery final:QObject {
    std::function<void(const NativeSourcePublication&)> publication;
    std::function<void(const NativeSourceAttempt&)> source;
    std::function<void(const NativeSourceInventory&)> inventory;
    std::function<void(const NativeSourceLineage&)> lineage;
    std::function<void(const NativeSourceProgress&)> progress;
    std::function<void(const NativeSourceTrace&)> trace;
    std::function<void(InventoryAcquisition)> acquired;
    std::function<void()> indexWanted;
    std::function<void(IndexCompletion)> indexFinished;
    bool wantsIndex{};
    std::function<void()> stopped;
    unsigned unfinishedThreads{3}; // touched ONLY on creating thread
};
using DeliveryPtr=std::shared_ptr<Delivery>;
void trace(const DeliveryPtr& delivery,NativeSourceTrace value){
    if(!delivery->trace)return;
    QMetaObject::invokeMethod(delivery.get(),[delivery,value=std::move(value)]{if(delivery->trace)delivery->trace(value);},Qt::QueuedConnection);
}
void publish(const std::shared_ptr<State>& state,const DeliveryPtr& delivery,NativeSourcePublication value){
    bool post=false;{
        const std::lock_guard lock(state->mutex);if(state->stopping)return;
        if(state->latest){
            if(state->latest->coalescedFrames==std::numeric_limits<qint64>::max())fail("SOURCE UI coalescing counter overflow");
            value.coalescedFrames=state->latest->coalescedFrames+1;value.snapshot["native_display_coalesced"]=true;
            carryRawCounterPair(state->latest->snapshot,value.snapshot,NativeSourceRuntime::PublicationLimit-128);
        }
        state->latest=std::move(value);if(!state->deliveryPosted){state->deliveryPosted=true;post=true;}
    }
    if(post)QMetaObject::invokeMethod(delivery.get(),[state,delivery]{
        std::optional<NativeSourcePublication> value;{const std::lock_guard lock(state->mutex);value=std::move(state->latest);state->latest.reset();state->deliveryPosted=false;}
        if(value&&!state->stopping&&delivery->publication)delivery->publication(*value);
    },Qt::QueuedConnection);
}
QJsonObject projection(const SshObservationFrame& frame,const NativeObservation& admitted){
    auto packet=frame.packet;packet.remove("chunk");packet.remove("telemetry_lines"); // binding/provenance/time stay EXACT
    QList<QString> lines;for(const auto& line:admitted.displayLines)if(!line.startsWith("[pgn-counters] "))lines.append(QString::fromUtf8(line));
    const auto telemetry=frame.packet.value("telemetry_lines");
    if(!telemetry.isUndefined()){
        if(!telemetry.isArray())fail("SOURCE telemetry_lines is not an array");
        const auto& counter=sourceDisplayCounterGrammar();
        for(const auto& value:telemetry.toArray()){if(!value.isString()||value.toString().size()>32768||value.toString().contains('\n')||value.toString().contains('\r')||!counter.match(value.toString()).hasMatch())fail("SOURCE telemetry may contain exact counter rows only, never GAP records");lines.append(value.toString());}
    }else for(const auto& line:admitted.displayLines)if(line.startsWith("[pgn-counters] "))lines.append(QString::fromUtf8(line));
    packet["type"]="snapshot";packet["native_observation_only"]=true;packet["native_display_coalesced"]=false;packet["native_display_truncated"]=false;packet["lines"]=QJsonArray{};
    const auto base=QJsonDocument(packet).toJson(QJsonDocument::Compact).size();if(base>NativeSourceRuntime::PublicationLimit)fail("SOURCE metadata publication exceeds bound");
    qsizetype budget=NativeSourceRuntime::PublicationLimit-base-128;QList<QString> selected;
    for(auto it=lines.crbegin();it!=lines.crend()&&selected.size()<512;++it){const auto cost=QJsonDocument(QJsonArray{*it}).toJson(QJsonDocument::Compact).size()+1;if(cost>budget)break;budget-=cost;selected.prepend(*it);}
    QJsonArray view;for(const auto& line:selected)view.append(line);packet["lines"]=view;packet["native_display_truncated"]=selected.size()!=lines.size();
    if(QJsonDocument(packet).toJson(QJsonDocument::Compact).size()>NativeSourceRuntime::PublicationLimit)fail("SOURCE publication bound exceeded");return packet;
}
class Worker final:public QObject {
public:
    NativeSourceRuntimeConfig config;
    std::shared_ptr<State> state;DeliveryPtr delivery;
    std::unique_ptr<SourceHistory> history;std::unique_ptr<SourceMirror> mirror;std::unique_ptr<NativeObservationSession> session;
    std::unique_ptr<SshSnapshotTransport> source;
    QString run,attemptRun,sourceId;quint64 sourceEpoch{};
    QJsonObject expectedManifest;
    std::optional<qint64> next,committed;bool replay{};QString admissionError;
    NativeSourceReadKind sourceKind{NativeSourceReadKind::LiveStream};bool lastFrameOwned{},lastFrameAdvanced{};
    std::optional<qint64> lastPending;int readFrames{1};
    QElapsedTimer clock;
    Worker(NativeSourceRuntimeConfig c,std::shared_ptr<State> s,DeliveryPtr d):config(std::move(c)),state(std::move(s)),delivery(std::move(d)){clock.start();}
    void sourceTrace(NativeSourceTraceStage stage,const QString& error={},std::optional<qint64> offset={},std::optional<qint64> nextOffset={}){if(delivery->trace)trace(delivery,{stage,sourceId,attemptRun,error.left(400),sourceEpoch,sourceEpoch,monotonicMs(),{},offset,nextOffset});}
    void rememberCursor(){const std::lock_guard lock(state->mutex);state->sourceRun=run;state->sourceNext=next;state->sourceCommitted=committed;}
    void requestIndex(const SshObservationFrame& frame,qint64 firstMonotonic){
        if(!delivery->wantsIndex)return;
        bool post=false;{const std::lock_guard lock(state->mutex);if(state->stopping)return;state->latestIndex=IndexRequest{sourceId,run,sourceEpoch,committed,frame.offset,frame.nextOffset,firstMonotonic,frame.pendingBytes};if(!state->indexBusy){state->indexBusy=true;post=true;}}
        if(post){const auto d=delivery;QMetaObject::invokeMethod(d.get(),[d]{if(d->indexWanted)d->indexWanted();},Qt::QueuedConnection);}
    }
    void initialize(){
        if(session)return;
        const QFileInfo db(config.historyDatabase),directory(config.mirrorDirectory);
        if(config.historyDatabase.contains(QChar(0))||!db.isAbsolute()||!QFileInfo(db.absolutePath()).isDir()||config.mirrorDirectory.contains(QChar(0))||!directory.isAbsolute()||!directory.isDir())fail("Explicit SOURCE database parent/existing mirror directory required");
        CompletionAdvanceGate validateDevice(config.device);
        // These writer resources are constructed, used and destroyed ONLY on
        // this worker. Initial schema/index audits can be linear and are not
        // advertised as bounded-latency operations.
        auto h=std::make_unique<SourceHistory>(config.historyDatabase);auto m=std::make_unique<SourceMirror>(config.mirrorDirectory);auto s=std::make_unique<NativeObservationSession>(*h,*m,config.device);
        history=std::move(h);mirror=std::move(m);session=std::move(s);
    }
    SshObservationRequest request(SshObservationAction action,const QString& selected,qint64 offset)const{return {action,selected,offset,config.startupTimeoutMs,config.idleTimeoutMs,config.snapshotTimeoutMs};}
    void sourceDone(SshObservationResult result){
        if(sourceKind==NativeSourceReadKind::CompletedSnapshot&&result.status==SshObservationStatus::Complete&&result.exitCode==0&&lastPending&&*lastPending>0&&!lastFrameAdvanced){
            result.status=SshObservationStatus::Failed;result.error="Completed SOURCE has pending bytes but no cursor progress; raw retained, no automatic retry";
        }
        const bool liveLease=sourceKind==NativeSourceReadKind::LiveStream&&lastFrameOwned&&result.completeFrames==SshSnapshotTransport::StreamFrameLease;
        const bool completedPage=!lastFrameOwned&&lastFrameAdvanced&&lastPending&&*lastPending>0&&result.completeFrames==readFrames;
        if(result.status==SshObservationStatus::Complete&&result.exitCode==0&&(liveLease||completedPage)&&!replay&&admissionError.isEmpty()&&next&&committed&&
           !state->stopping&&!state->sourceCancelled){
            // Renew only this successful, finite READ lease at its conserved
            // cursor. Never retry a failed read or issue a physical control.
            const auto id=sourceId,selected=attemptRun;const auto epoch=sourceEpoch;const auto expected=expectedManifest;const auto kind=completedPage?NativeSourceReadKind::CompletedSnapshot:sourceKind;
            const int frames=completedPage?int(std::min<qint64>(SshSnapshotTransport::StreamFrameLease,1+(*lastPending-1)/SshSnapshotTransport::ChunkLimit)):0;
            QTimer::singleShot(0,this,[this,id,selected,epoch,expected,kind,frames]{
                if(epoch!=sourceEpoch)return;
                if(state->stopping||state->sourceCancelled){sourceDone({SshObservationStatus::Cancelled,"SOURCE lease renewal cancelled",{},0,0});return;}
                start(id,selected,epoch,expected,kind,frames);
            });return;
        }
        NativeSourceAttempt completed;completed.requestId=sourceId;completed.run=attemptRun;completed.epoch=sourceEpoch;completed.transport=std::move(result);completed.status=replay?NativeSourceAttemptStatus::ReplayRequired:status(completed.transport.status);if(run==attemptRun){completed.nextOffset=next;completed.committedOffset=committed;}completed.error=admissionError.isEmpty()?completed.transport.error:admissionError;
        if(state->sourceCancelled&&completed.status==NativeSourceAttemptStatus::Failed)completed.status=NativeSourceAttemptStatus::Cancelled;
        const auto d=delivery;QMetaObject::invokeMethod(d.get(),[d,value=std::move(completed)]{if(d->source)d->source(value);},Qt::QueuedConnection);
    }
    void start(QString id,QString selected,quint64 epoch,QJsonObject expected,NativeSourceReadKind kind,int drainFrames=0){
        expectedManifest=std::move(expected);
        sourceId=std::move(id);attemptRun=selected;sourceEpoch=epoch;replay=false;admissionError.clear();source.reset();
        sourceKind=kind;lastFrameOwned=false;lastFrameAdvanced=false;lastPending.reset();readFrames=kind==NativeSourceReadKind::CompletedSnapshot?(drainFrames>0?drainFrames:1):SshSnapshotTransport::StreamFrameLease;
        try{
            if(state->stopping||state->sourceCancelled)fail("SOURCE attempt cancelled before entry");
            const auto action=kind==NativeSourceReadKind::CompletedSnapshot&&drainFrames==0?SshObservationAction::Snapshot:SshObservationAction::Stream;
            const auto probe=request(action,selected,0);makeSshObservationCommand(config.endpoint,probe); // validate BEFORE storage
            if(!session){
                sourceTrace(NativeSourceTraceStage::InitializeBegin);
                try{initialize();}catch(const std::exception& e){sourceTrace(NativeSourceTraceStage::InitializeHeld,QString::fromUtf8(e.what()));throw;}
                sourceTrace(NativeSourceTraceStage::InitializeDone);
            }
            // Initialization can audit existing SQLite for a long time; a
            // cancellation received meanwhile must not launch a new client.
            if(state->stopping||state->sourceCancelled)fail("SOURCE cancelled during writer initialization");
            if(run!=selected||!next){
                // Do NOT carry A's last known tuple into a B lookup failure.
                // Missing/invalid B metadata stays absent, never old A or0.
                run=selected;next.reset();committed.reset();rememberCursor();
                const auto cursor=history->cursorRecord(run);next=cursor?std::optional<qint64>(cursor->offset):std::nullopt;committed=next;rememberCursor();
            }
            // Offset0 here is a request for discovery of an absent prefix,
            // NOT a fabricated observed/stored cursor0 in results.
            auto read=request(action,run,next.value_or(0));read.streamFrames=readFrames;source=std::make_unique<SshSnapshotTransport>();
            if(state->stopping||state->sourceCancelled)fail("SOURCE cancelled before client entry");
            struct FirstFrame {bool seen{};QElapsedTimer clock;qint64 began{};};
            const auto first=(delivery->progress||delivery->trace)?std::make_shared<FirstFrame>():std::shared_ptr<FirstFrame>{};
            source->start(config.endpoint,read,[this,epoch,first](const SshObservationFrame& frame){
                if(epoch!=sourceEpoch||state->stopping||state->sourceCancelled)return false;
                const auto resourceReceiptNs=receiptMonotonicNs();
                const bool trace=first&&!first->seen;
                if(first)first->seen=true;
                if(trace){first->clock.start();first->began=monotonicMs();}
                const auto progress=[&](NativeSourceProgressStage stage,const QString& error=QString{}){
                    if(!trace||!delivery->progress)return;
                    NativeSourceProgress value{stage,sourceId,attemptRun,error.left(400),epoch,frame.offset,frame.nextOffset,first->clock.elapsed(),frame.pendingBytes};
                    const auto d=delivery;QMetaObject::invokeMethod(d.get(),[d,value=std::move(value)]{if(d->progress)d->progress(value);},Qt::QueuedConnection);
                };
                progress(NativeSourceProgressStage::FrameReceived);
                try{
                    if(!expectedManifest.isEmpty()&&frame.packet.value("manifest")!=expectedManifest)fail("SOURCE frame differs from immutable fresh requested manifest before admission");
                    // Validate telemetry before any SOURCE admission, so it
                    // cannot inject a GAP through the UI side channel.
                    projection(frame,{});
                    const auto admitted=session->consume(frame,run,clock.elapsed(),false,true,[state=state]{return state->stopping||state->sourceCancelled;});
                    if(admitted.state!=NativeObservationState::Cold){next=admitted.nextOffset;const auto cursor=history->cursorRecord(run);committed=cursor?std::optional<qint64>(cursor->offset):std::nullopt;rememberCursor();}
                    if(admitted.state==NativeObservationState::ReplayRequired){replay=true;admissionError="SOURCE replay required at exact native offset";return false;}
                    if(admitted.state==NativeObservationState::Source)progress(NativeSourceProgressStage::SourceAdmitted);
                    if(state->stopping||state->sourceCancelled)return false;
                    NativeSourcePublication value;value.requestId=sourceId;value.epoch=epoch;value.run=admitted.run;value.snapshot=projection(frame,admitted);value.conservation=session->conservation(frame,run).json();
                    value.resourceReceiptNs=resourceReceiptNs;
                    if(admitted.state==NativeObservationState::Source){
                        requestIndex(frame,first?first->began:0);
                    }
                    lastFrameOwned=frame.packet.value("owned")==true;
                    lastFrameAdvanced=frame.nextOffset>frame.offset;lastPending=frame.pendingBytes;
                    publish(state,delivery,std::move(value));return true;
                }catch(const std::exception& e){admissionError=QString::fromUtf8(e.what()).left(400);return false;}
            },[this](const SshObservationResult& result){sourceDone(result);});
        }catch(const std::exception& e){admissionError=QString::fromUtf8(e.what()).left(400);sourceDone({SshObservationStatus::Failed,admissionError,{},std::nullopt,0});}
    }
    void conserveInventory(InventoryAcquisition acquisition){
        NativeSourceInventory value;value.requestId=acquisition.requestId;value.epoch=acquisition.epoch;value.sourceEpoch=acquisition.sourceEpoch;value.transport=std::move(acquisition.transport);value.error=value.transport.error;
        SourceConservation unknown;unknown.error="SOURCE has no admitted epoch; no read-side attachment";value.conservation=unknown.json();
        const auto unchanged=[&]{return !state->stopping&&sourceEpoch==acquisition.sourceEpoch&&state->sourceCancelled.load()==acquisition.sourceCancelled;};
        if(unchanged()&&value.transport.status==SshObservationStatus::Complete&&value.transport.exitCode==0&&acquisition.frame){value.frame=std::move(acquisition.frame);try{if(session)value.conservation=session->conservation(*value.frame,acquisition.expected.isEmpty()?run:acquisition.expected).json();}catch(const std::exception& e){unknown.error=value.error=QString::fromUtf8(e.what()).left(400);value.conservation=unknown.json();}}
        if(!unchanged()){value.frame.reset();unknown.error=value.error="SOURCE epoch/cancellation changed during inventory acquisition";value.conservation=unknown.json();}
        const auto d=delivery;QMetaObject::invokeMethod(d.get(),[d,value=std::move(value)]{if(d->inventory)d->inventory(value);},Qt::QueuedConnection);
    }
    void cancel(){if(source)source->cancel();}
    void verifyLineage(QString id,QString booking,QJsonObject snapshot,quint64 epoch,quint64 expectedSourceEpoch){
        NativeSourceLineage value;value.requestId=std::move(id);value.epoch=epoch;value.sourceEpoch=expectedSourceEpoch;value.run=snapshot.value("manifest").toObject().value("run").toString();
        value.proof={{"schema","pgn-source-resume-lineage-1"},{"ready",false},{"error","SOURCE has no matching admitted handle epoch"}};
        try{
            const auto cancelled=[state=state,epoch]{return state->stopping.load()||state->cancelledLineageEpoch.load()==epoch;};
            if(cancelled())fail("SOURCE lineage cancelled");
            if(!session||!mirror||run!=value.run||!runValid(run)||sourceEpoch!=expectedSourceEpoch)fail("SOURCE lineage snapshot does not match this worker source epoch");
            // No mirror reference escapes its owner. Reader/opener/verifier
            // all unwind BEFORE publishing result or releasing stopped().
            HeldWin32LineageSources held(*mirror);
            SourceResumeLineageVerifier verifier(std::move(booking),config.historyDatabase,held.opener(),cancelled);
            value.proof=verifier.verify(snapshot);
            if(cancelled())fail("SOURCE lineage cancelled");
        }catch(const std::exception& e){value.proof={{"schema","pgn-source-resume-lineage-1"},{"ready",false},{"error",QString::fromUtf8(e.what()).left(400)}};}
        const auto d=delivery;QMetaObject::invokeMethod(d.get(),[d,value=std::move(value)]{if(d->lineage)d->lineage(value);},Qt::QueuedConnection);
    }
    void shutdown(){
        state->sourceCancelled=true;if(source)source->cancel();source.reset();session.reset();mirror.reset();history.reset();thread()->quit();
    }
};
// No storage lives on this actor. In particular the transport (including its
// unparented QTimers/QProcess) is CREATED here, never moved across threads.
class InventoryActor final:public QObject {
    NativeSourceRuntimeConfig config;std::shared_ptr<State> state;DeliveryPtr delivery;
    std::unique_ptr<SshSnapshotTransport> client;std::optional<SshObservationFrame> frame;
    InventoryAcquisition current;bool active{};
    void completed(SshObservationResult result){
        active=false;current.transport=std::move(result);
        if(current.transport.status==SshObservationStatus::Complete&&current.transport.exitCode==0)current.frame=std::move(frame);else frame.reset();
        const auto d=delivery;QMetaObject::invokeMethod(d.get(),[d,value=std::move(current)]()mutable{if(d->acquired)d->acquired(std::move(value));},Qt::QueuedConnection);
    }
public:
    InventoryActor(NativeSourceRuntimeConfig c,std::shared_ptr<State> s,DeliveryPtr d):config(std::move(c)),state(std::move(s)),delivery(std::move(d)){}
    void inspect(InventoryAcquisition value,qint64 offset){
        if(active)fail("ONE inventory acquisition required");current=std::move(value);frame.reset();client.reset();active=true;
        if(delivery->trace)trace(delivery,{NativeSourceTraceStage::InventoryEntered,current.requestId,current.expected,{},current.epoch,current.sourceEpoch,monotonicMs(),{}});
        try{
            if(state->stopping)fail("Inventory cancelled before entry");
            client=std::make_unique<SshSnapshotTransport>();
            SshSnapshotTransport::Trace diagnostic;if(delivery->trace)diagnostic=[this,id=current.requestId,expected=current.expected,epoch=current.epoch,sourceEpoch=current.sourceEpoch](const SshObservationTrace& value){trace(delivery,{NativeSourceTraceStage::InventoryTransport,id,expected,{},epoch,sourceEpoch,value.monotonicMs,value});};
            const SshObservationRequest request{SshObservationAction::Snapshot,current.expected,offset,config.startupTimeoutMs,config.idleTimeoutMs,config.snapshotTimeoutMs};
            client->start(config.endpoint,request,[this](const auto& value){if(state->stopping)return false;frame=value;return true;},[this](const auto& value){completed(value);},std::move(diagnostic));
        }catch(const std::exception& e){completed({SshObservationStatus::Failed,QString::fromUtf8(e.what()).left(400),{},std::nullopt,0});}
    }
    void shutdown(){if(client)client->cancel();client.reset();frame.reset();thread()->quit();}
};
class IndexActor final:public QObject {
    QString database;std::shared_ptr<State> state;DeliveryPtr delivery;
    std::unique_ptr<SourceHistoryIndexReader> reader;quint64 tracedEpoch{};
public:
    IndexActor(QString file,std::shared_ptr<State> s,DeliveryPtr d):database(std::move(file)),state(std::move(s)),delivery(std::move(d)){}
    void read(IndexRequest request){
        const bool first=tracedEpoch!=request.epoch;tracedEpoch=request.epoch;
        const auto progress=[&](NativeSourceProgressStage stage,NativeSourceTraceStage traceStage,const QString& error=QString{}){
            if(!first)return;const auto now=monotonicMs();
            if(delivery->trace)trace(delivery,{traceStage,request.requestId,request.run,error.left(400),request.epoch,request.epoch,now,{},request.offset,request.nextOffset});
            if(delivery->progress){const NativeSourceProgress value{stage,request.requestId,request.run,error.left(400),request.epoch,request.offset,request.nextOffset,request.firstMonotonic?now-request.firstMonotonic:0,request.pending};const auto d=delivery;QMetaObject::invokeMethod(d.get(),[d,value]{if(d->progress)d->progress(value);},Qt::QueuedConnection);}
        };
        IndexCompletion completed{request,{}, {}};
        try{
            const auto cancelled=[state=state,epoch=request.epoch]{return state->stopping.load()||state->indexEpoch.load()!=epoch;};
            if(cancelled())fail("Native display audit cancelled");if(!request.committed)fail("Native display committed cursor absent");
            progress(NativeSourceProgressStage::IndexBegin,NativeSourceTraceStage::IndexBegin);
            if(!reader)reader=std::make_unique<SourceHistoryIndexReader>(database);
            completed.snapshot=reader->read(request.run,*request.committed,cancelled);
            progress(NativeSourceProgressStage::IndexDone,NativeSourceTraceStage::IndexDone);
        }catch(const std::exception& e){completed.error=QString::fromUtf8(e.what()).left(400);progress(NativeSourceProgressStage::IndexHeld,NativeSourceTraceStage::IndexHeld,completed.error);}
        const auto d=delivery;QMetaObject::invokeMethod(d.get(),[d,value=std::move(completed)]()mutable{if(d->indexFinished)d->indexFinished(std::move(value));},Qt::QueuedConnection);
    }
    void shutdown(){reader.reset();thread()->quit();}
};
}
class NativeSourceRuntime::Impl {
public:
    NativeSourceRuntime* owner;QThread* const creatingThread=QThread::currentThread();
    std::shared_ptr<State> state=std::make_shared<State>();DeliveryPtr delivery;
    Worker* worker{};InventoryActor* inventoryActor{};IndexActor* indexActor{};NativeSourceRuntimeCallbacks callbacks;bool sourceBusy{},inventoryBusy{},lineageBusy{},inventorySourceCancelled{};quint64 sourceEpoch{},inventoryEpoch{},lineageEpoch{},inventorySourceEpoch{},lastIndexEpoch{};QString inventoryExpected;std::optional<qint64> lastIndexCursor;QJsonObject lastIndex;
    Impl(NativeSourceRuntime* o,NativeSourceRuntimeConfig config,NativeSourceRuntimeCallbacks c):owner(o),callbacks(std::move(c)){
        delivery=DeliveryPtr(new Delivery,[](Delivery* value){value->deleteLater();});delivery->wantsIndex=bool(callbacks.nativeIndex);const QPointer<NativeSourceRuntime> guard(owner);
        delivery->publication=[guard](const NativeSourcePublication& value){if(!guard)return;auto& self=*guard->impl_;if(value.epoch!=self.sourceEpoch||self.state->stopping)return;const auto callback=self.callbacks.publication;if(callback)callback(value);};
        delivery->source=[guard](const NativeSourceAttempt& value){if(!guard)return;auto& self=*guard->impl_;if(value.epoch!=self.sourceEpoch)return;self.sourceBusy=false;const auto callback=self.callbacks.sourceFinished;if(callback)callback(value);};
        delivery->inventory=[guard](const NativeSourceInventory& value){if(!guard)return;auto& self=*guard->impl_;if(value.epoch!=self.inventoryEpoch)return;self.inventoryBusy=false;auto result=value;
            if(self.state->stopping||self.sourceEpoch!=self.inventorySourceEpoch||self.state->sourceCancelled.load()!=self.inventorySourceCancelled){result.frame.reset();SourceConservation unknown;unknown.error=result.error="SOURCE epoch/cancellation changed during inventory acquisition";result.conservation=unknown.json();}
            if(self.delivery->trace)trace(self.delivery,{NativeSourceTraceStage::InventoryDelivered,value.requestId,self.inventoryExpected,{},value.epoch,value.sourceEpoch,monotonicMs(),{}});const auto callback=self.callbacks.inventory;if(callback)callback(result);};
        delivery->acquired=[guard](InventoryAcquisition value){
            if(!guard)return;auto& self=*guard->impl_;if(value.epoch!=self.inventoryEpoch)return;
            if(self.state->stopping){NativeSourceInventory cancelled;cancelled.requestId=value.requestId;cancelled.epoch=value.epoch;cancelled.sourceEpoch=value.sourceEpoch;cancelled.transport=std::move(value.transport);cancelled.error="Inventory cancelled during runtime shutdown";self.delivery->inventory(cancelled);return;}
            QMetaObject::invokeMethod(self.worker,[w=self.worker,value=std::move(value)]()mutable{w->conserveInventory(std::move(value));},Qt::QueuedConnection);
        };
        delivery->indexWanted=[guard]{if(guard)guard->impl_->dispatchIndex();};
        delivery->indexFinished=[guard](IndexCompletion value){
            if(!guard)return;auto& self=*guard->impl_;self.dispatchIndex();
            if(self.state->stopping||value.request.epoch!=self.sourceEpoch)return;
            std::optional<qint64> current;QString currentRun;{const std::lock_guard lock(self.state->mutex);current=self.state->sourceCommitted;currentRun=self.state->sourceRun;}
            if(currentRun!=value.request.run)return;
            QJsonObject packet{{"type","native_index"},{"complete",false},{"source_epoch",QString::number(value.request.epoch)},{"source_run",value.request.run},{"request_id",value.request.requestId},{"error",value.error}};
            if(value.snapshot){
                const auto& snapshot=*value.snapshot;bool valid=current&&snapshot.committedOffset<=*current;
                if(self.lastIndexEpoch==value.request.epoch&&self.lastIndexCursor){
                    valid=valid&&snapshot.committedOffset>=*self.lastIndexCursor;
                    for(const auto scope:{"all","current"})for(const auto axis:{"length","time"})valid=valid&&snapshot.publication.value(scope).toObject().value(axis).toObject().value("revision").toInteger(-1)>=self.lastIndex.value(scope).toObject().value(axis).toObject().value("revision").toInteger(-1);
                }
                if(valid){packet=snapshot.publication;packet.insert("source_epoch",QString::number(value.request.epoch));packet.insert("source_run",value.request.run);packet.insert("request_id",value.request.requestId);packet.insert("snapshot_committed_offset",snapshot.committedOffset);packet.insert("reader_data_version",snapshot.dataVersion);packet.insert("snapshot_scope","COHERENT_PAST_READ_NOT_LATEST_OR_SOURCE_READY");self.lastIndexEpoch=value.request.epoch;self.lastIndexCursor=snapshot.committedOffset;self.lastIndex=snapshot.publication;}
                else packet.insert("error","Native display cursor/revision regressed or exceeds conserved SOURCE");
            }
            if(packet.value("complete")!=true&&packet.value("error").toString().isEmpty())packet.insert("error","Native display audit unavailable");
            if(QJsonDocument(packet).toJson(QJsonDocument::Compact).size()>NativeSourceRuntime::PublicationLimit)packet={{"type","native_index"},{"complete",false},{"error","Native display publication exceeds bound"},{"source_epoch",QString::number(value.request.epoch)},{"source_run",value.request.run}};
            const auto callback=self.callbacks.nativeIndex;if(callback)try{callback(packet);}catch(...){} // display sink has no SOURCE authority; no access after sink
        };
        delivery->lineage=[guard](const NativeSourceLineage& value){if(!guard)return;auto& self=*guard->impl_;if(value.epoch!=self.lineageEpoch||value.sourceEpoch!=self.sourceEpoch)return;self.lineageBusy=false;const auto callback=self.callbacks.lineage;auto result=value;
            // A cancel can arrive after worker computation but before owner
            // delivery. It still revokes this epoch, never its successor.
            if(self.state->cancelledLineageEpoch.load()==value.epoch)result.proof={{"schema","pgn-source-resume-lineage-1"},{"ready",false},{"error","SOURCE lineage cancelled"}};
            if(callback)callback(result);};
        if(callbacks.progress)delivery->progress=[guard](const NativeSourceProgress& value){
            if(!guard||value.sourceEpoch!=guard->impl_->sourceEpoch)return;
            const auto callback=guard->impl_->callbacks.progress;
            // A diagnostics sink must never reject/alter the SOURCE attempt.
            if(callback)try{callback(value);}catch(...){}
        };
        if(callbacks.trace)delivery->trace=[guard](const NativeSourceTrace& value){
            if(!guard)return;
            // Immutable diagnostic spans may finish delivery after the consumer
            // admits its successor. Preserve their ORIGINAL IDs/epoch instead
            // of dropping the terminal event; operational/proof guards above
            // are deliberately unchanged.
            const auto callback=guard->impl_->callbacks.trace;if(callback)try{callback(value);}catch(...){}
        };
        delivery->stopped=[guard]{if(!guard)return;const auto callback=guard->impl_->callbacks.stopped;if(callback)callback();};
        auto thread=new QThread;worker=new Worker(config,state,delivery);worker->moveToThread(thread);
        auto acquisitionThread=new QThread;inventoryActor=new InventoryActor(std::move(config),state,delivery);inventoryActor->moveToThread(acquisitionThread);
        auto indexThread=new QThread;indexActor=new IndexActor(worker->config.historyDatabase,state,delivery);indexActor->moveToThread(indexThread);
        const auto stopped=[d=delivery]{if(--d->unfinishedThreads==0&&d->stopped)d->stopped();};
        QObject::connect(thread,&QThread::finished,worker,&QObject::deleteLater);
        QObject::connect(acquisitionThread,&QThread::finished,inventoryActor,&QObject::deleteLater);
        QObject::connect(indexThread,&QThread::finished,indexActor,&QObject::deleteLater);
        QObject::connect(thread,&QThread::finished,delivery.get(),stopped,Qt::QueuedConnection);
        QObject::connect(acquisitionThread,&QThread::finished,delivery.get(),stopped,Qt::QueuedConnection);
        QObject::connect(indexThread,&QThread::finished,delivery.get(),stopped,Qt::QueuedConnection);
        QObject::connect(thread,&QThread::finished,thread,&QObject::deleteLater);QObject::connect(acquisitionThread,&QThread::finished,acquisitionThread,&QObject::deleteLater);QObject::connect(indexThread,&QThread::finished,indexThread,&QObject::deleteLater);thread->start();acquisitionThread->start();indexThread->start();
    }
    void requireOwner()const{if(QThread::currentThread()!=creatingThread||owner->thread()!=creatingThread)fail("NativeSourceRuntime creating-thread API required");}
    void dispatchIndex(){
        std::optional<IndexRequest> request;{const std::lock_guard lock(state->mutex);if(state->stopping){state->latestIndex.reset();state->indexBusy=false;return;}request=std::move(state->latestIndex);state->latestIndex.reset();if(!request){state->indexBusy=false;return;}}
        QMetaObject::invokeMethod(indexActor,[actor=indexActor,value=std::move(*request)]()mutable{actor->read(std::move(value));},Qt::QueuedConnection);
    }
    bool start(const QString& id,const QString& run,const QJsonObject& expected,NativeSourceReadKind kind){
        if(kind!=NativeSourceReadKind::LiveStream&&kind!=NativeSourceReadKind::CompletedSnapshot)return false;
        requireOwner();if(state->stopping||sourceBusy||lineageBusy||!uuidValid(id)||!runValid(run)||sourceEpoch==std::numeric_limits<quint64>::max())return false;
        if(!expected.isEmpty()&&expected.value("run")!=run)return false;
        sourceBusy=true;state->sourceCancelled=false;const auto epoch=++sourceEpoch;state->indexEpoch=epoch;{const std::lock_guard lock(state->mutex);state->latest.reset();}
        QMetaObject::invokeMethod(worker,[w=worker,id,run,epoch,expected,kind]{w->start(id,run,epoch,expected,kind);},Qt::QueuedConnection);return true;
    }
    bool inspect(const QString& id,const QString& expected){
        requireOwner();if(state->stopping||inventoryBusy||lineageBusy||!uuidValid(id)||(!expected.isEmpty()&&!runValid(expected))||inventoryEpoch==std::numeric_limits<quint64>::max())return false;
        inventoryBusy=true;inventoryExpected=expected;inventorySourceEpoch=sourceEpoch;inventorySourceCancelled=state->sourceCancelled.load();const auto epoch=++inventoryEpoch;
        qint64 offset{};{const std::lock_guard lock(state->mutex);if(expected==state->sourceRun&&state->sourceNext)offset=*state->sourceNext;}
        if(delivery->trace)trace(delivery,{NativeSourceTraceStage::InventoryQueued,id,expected,{},epoch,sourceEpoch,monotonicMs(),{}});
        InventoryAcquisition value{id,expected,epoch,inventorySourceEpoch,inventorySourceCancelled,{}, {}};QMetaObject::invokeMethod(inventoryActor,[a=inventoryActor,value=std::move(value),offset]()mutable{a->inspect(std::move(value),offset);},Qt::QueuedConnection);return true;
    }
    bool cancel(){requireOwner();if(state->stopping||!sourceBusy)return false;state->sourceCancelled=true;QMetaObject::invokeMethod(worker,[w=worker]{w->cancel();},Qt::QueuedConnection);return true;}
    bool verify(const QString& id,const QString& booking,const QJsonObject& snapshot){
        requireOwner();if(state->stopping||sourceEpoch==0||inventoryBusy||lineageBusy||!uuidValid(id)||lineageEpoch==std::numeric_limits<quint64>::max())return false;
        lineageBusy=true;const auto epoch=++lineageEpoch,expected=sourceEpoch;QMetaObject::invokeMethod(worker,[w=worker,id,booking,snapshot,epoch,expected]{w->verifyLineage(id,booking,snapshot,epoch,expected);},Qt::QueuedConnection);return true;
    }
    bool cancelLineage(){requireOwner();if(state->stopping||!lineageBusy)return false;state->cancelledLineageEpoch.store(lineageEpoch);return true;}
    void stop(){requireOwner();if(state->stopping.exchange(true))return;state->sourceCancelled=true;{const std::lock_guard lock(state->mutex);state->latest.reset();state->latestIndex.reset();}QMetaObject::invokeMethod(indexActor,[a=indexActor]{a->shutdown();},Qt::QueuedConnection);QMetaObject::invokeMethod(inventoryActor,[a=inventoryActor]{a->shutdown();},Qt::QueuedConnection);QMetaObject::invokeMethod(worker,[w=worker]{w->shutdown();},Qt::QueuedConnection);}
};
NativeSourceRuntime::NativeSourceRuntime(NativeSourceRuntimeConfig config,NativeSourceRuntimeCallbacks callbacks,QObject* parent):QObject(parent),impl_(std::make_unique<Impl>(this,std::move(config),std::move(callbacks))){}
NativeSourceRuntime::~NativeSourceRuntime(){impl_->stop();}
bool NativeSourceRuntime::startSource(const QString& id,const QString& run,const QJsonObject& expected,NativeSourceReadKind kind){return impl_->start(id,run,expected,kind);}
bool NativeSourceRuntime::inspectInventory(const QString& id,const QString& expected){return impl_->inspect(id,expected);}
bool NativeSourceRuntime::verifyLineage(const QString& id,const QString& booking,const QJsonObject& snapshot){return impl_->verify(id,booking,snapshot);}
bool NativeSourceRuntime::cancelLineage(){return impl_->cancelLineage();}
bool NativeSourceRuntime::cancelSource(){return impl_->cancel();}
void NativeSourceRuntime::stop(){impl_->stop();}
}

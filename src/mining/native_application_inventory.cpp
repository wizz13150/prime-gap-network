#include "mining/native_application_inventory.h"
#include "mining/source_decimal.h"
#include "mining/staged_successor.h"
#include <QJsonArray>
#include <QDateTime>
#include <QPointer>
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <chrono>
#include <stdexcept>
#include <limits>

namespace pgn::mining {
namespace {
qint64 monotonicMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
const QString Constant="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
const QString Finite=QString::fromLatin1(Finite3060Device);
bool canonical(const QString& id){const auto u=QUuid::fromString(id);return !u.isNull()&&u.toString(QUuid::WithoutBraces)==id;}
bool run(const QString& id){static const QRegularExpression p("^[0-9a-f]{32}$");return id.size()==32&&p.match(id).hasMatch();}
bool utc(const QJsonValue& time){
    static const QRegularExpression stamp("^([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2})(?:\\.[0-9]{1,9})?Z$");
    const auto match=stamp.match(time.toString());
    if(!time.isString()||!match.hasMatch()||match.capturedLength()!=time.toString().size())return false;
    const auto date=QDateTime::fromString(match.captured(1)+'Z',Qt::ISODate);
    return date.isValid()&&date.toUTC().toString("yyyy-MM-dd'T'HH:mm:ss")==match.captured(1);
}
bool constantKnown(const QJsonObject& packet,bool inactiveClose=false){
    const bool driverForPurpose=packet.value("driver_valid")==true||
        (inactiveClose&&packet.value("driver_valid")==false&&packet.value("owned")==false&&packet.value("state")=="OFF");
    if(!utc(packet.value("observed_utc"))||
       packet.value("schema")!="pgn-runtime-snapshot-1"||!driverForPurpose||!packet.value("owned").isBool()||
       packet.value("binding").toObject().value("uuid").toString().compare(Constant,Qt::CaseInsensitive)!=0)return false;
    const auto manifest=packet.value("manifest");if(manifest.isNull())return packet.value("owned")==false&&packet.value("state")=="OFF";
    if(!manifest.isObject())return false;const auto m=manifest.toObject();
    const auto pid=m.value("pid"),first=m.value("start"),last=m.value("end");const auto binding=packet.value("binding").toObject();
    const bool owned=packet.value("owned").toBool();const auto state=packet.value("state").toString();
    return m.value("schema")=="pgn-native-process-1"&&run(m.value("run").toString())&&m.value("uuid").toString().compare(Constant,Qt::CaseInsensitive)==0&&
        pid.toVariant().metaType().id()==QMetaType::LongLong&&pid.toInteger()>0&&pid.toInteger()<=4294967295LL&&utc(m.value("birth"))&&
        first.isString()&&last.isString()&&first.toString().size()<=160&&last.toString().size()<=160&&source_decimal::canonical(first.toString())&&source_decimal::canonical(last.toString())&&source_decimal::less(first.toString(),last.toString())&&
        ((binding.value("start")==first&&binding.value("end")==last)||stagedSuccessor(packet))&&(owned?(state=="RUNNING"||state=="STOPPING"):state=="OFF");
}
bool sameProofBoundary(const SshObservationFrame& before,const SshObservationFrame& after){
    if(before.offset!=after.offset||before.nextOffset!=after.nextOffset||before.pendingBytes!=after.pendingBytes||before.chunk!=after.chunk)return false;
    // These are every physical identity and completion/checkpoint input of
    // the verifier. Driver/observation timestamps may really change. A newly
    // observed operator STOP is preserved and remains an advance veto.
    for(const auto* key:{"schema","manifest","binding","last_exit","owned","state","has_checkpoint","checkpoint_evidence",
                         "completion","completion_candidate","source_completion_observation"})
        if(before.packet.value(key)!=after.packet.value(key))return false;
    return true;
}
void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
class NativeApplicationInventory::Impl {
public:
    NativeApplicationInventory* owner;
    NativeApplicationWriter& writer;
    NativeApplicationInventoryConfig config;
    NativeApplicationInventoryCallbacks callbacks;
    std::unique_ptr<NativeSourceRuntime> runtime;
    QPointer<NativeControlTransport> finite;
    struct Pending {
        QString request,inventory,phase,phaseId;bool close{},boundary{},refreshing{},refreshGuardPending{},constantKnown{},finiteKnown{},lineageCancelled{},driverUnavailable{};
        quint64 sourceEpoch{},acquisitionEpoch{};qint64 beganMonotonicMs{};
        std::optional<SshObservationFrame> frame;
        QJsonObject proof;
        QString constantError,finiteError;
        QJsonObject snapshot,conservation,finiteSnapshot;
        std::optional<NativeFiniteObservation> finiteObservation;
        NativeApplicationWriter::Done done;
        QString sourceRequest;QJsonObject sourceManifest;bool waitingSource{},sourceReacquire{};qint64 sourceProgress{-1};
    };
    std::optional<Pending> pending;
    QSet<QString> usedInventories;
    QString admittedSourceRun;
    QString observedCloseRequest;QTimer sourceWait;
    bool stopping{};
    Impl(NativeApplicationInventory* o,NativeApplicationWriter& w,NativeApplicationInventoryConfig c,NativeApplicationInventoryCallbacks cb)
        :owner(o),writer(w),config(std::move(c)),callbacks(std::move(cb)){
        requireOwner();need(writer.thread()==owner->thread(),"Inventory requires same-thread ONE writer");
        need(!config.bookingDatabase.isEmpty(),"Explicit same application SQLite required");
        need(config.finiteTimeouts.startupMs>0&&config.finiteTimeouts.startupMs<=NativeControlTransport::MaximumStartupMs&&
             config.finiteTimeouts.deadlineMs>=config.finiteTimeouts.startupMs&&config.finiteTimeouts.deadlineMs<=NativeControlTransport::MaximumDeadlineMs,"Finite observation timeouts invalid");
        need(config.source.device==Constant,"Inventory source profile is the qualified constant device only");
        const QPointer<NativeApplicationInventory> guard(owner);
        NativeSourceRuntimeCallbacks sourceCallbacks;
        sourceCallbacks.publication=[guard](const auto& value){if(guard)guard->d_->sourcePublication(value);};
        if(callbacks.nativeIndex)sourceCallbacks.nativeIndex=[guard](const QJsonObject& value){if(guard){const auto cb=guard->d_->callbacks.nativeIndex;if(cb)cb(value);}};
        sourceCallbacks.sourceFinished=[guard](const auto& value){if(guard)guard->d_->sourceFinished(value);};
        sourceCallbacks.inventory=[guard](const auto& value){if(guard)guard->d_->inventory(value);};
        sourceCallbacks.lineage=[guard](const auto& value){if(guard)guard->d_->lineage(value);};
        sourceCallbacks.stopped=[guard]{if(guard){const auto cb=guard->d_->callbacks.stopped;if(cb)cb();}};
        if(config.timingAudit&&callbacks.timing){
            const auto timing=[guard](const QJsonObject& value){if(!guard)return;const auto callback=guard->d_->callbacks.timing;if(callback)try{callback(value);}catch(...){}};
            sourceCallbacks.trace=[timing](const NativeSourceTrace& value){timing(value.json());};
            sourceCallbacks.progress=[timing](const NativeSourceProgress& value){timing(value.json());};
        }
        runtime=std::make_unique<NativeSourceRuntime>(config.source,std::move(sourceCallbacks));
        sourceWait.setParent(owner);sourceWait.setSingleShot(true);sourceWait.setInterval(config.source.snapshotTimeoutMs);
        QObject::connect(&sourceWait,&QTimer::timeout,owner,[guard]{if(guard&&guard->d_->pending&&guard->d_->pending->waitingSource)guard->d_->finish("SOURCE boundary wait expired; raw data retained, no conservation inferred");});
    }
    ~Impl(){if(pending&&pending->done)pending->done("Inventory owner destroyed; no physical outcome inferred");}
    void requireOwner()const{need(QThread::currentThread()==owner->thread(),"Inventory requires creating thread");}
    void publish(const QJsonObject& packet){const auto cb=callbacks.packet;if(cb)cb(packet);}
    void sourcePublication(const NativeSourcePublication& value){
        const QPointer<NativeApplicationInventory> guard(owner);
        if(run(value.run))admittedSourceRun=value.run; // Cold UI state cannot erase an admitted source key.
        if(pending&&pending->waitingSource&&pending->sourceRequest==value.requestId){
            if(value.run!=pending->sourceManifest.value("run")||value.snapshot.value("manifest")!=pending->sourceManifest){finish("SOURCE repair publication changed the pinned physical manifest");if(!guard)return;}
            else if(value.conservation.value("run")==value.run&&value.conservation.value("source")=="native/"+value.run+".stdout"){
                const auto committed=value.conservation.value("committed_offset").toInteger(-1);
                // An IDLE deadline, not an arbitrary total-file-size limit.
                // Only actual same-worker committed-byte advance renews it;
                // duplicate/stale frames, heartbeats and received bytes cannot.
                if(committed>pending->sourceProgress){pending->sourceProgress=committed;sourceWait.start();}
                if(value.conservation.value("ready")!=true){const auto cb=callbacks.publication;if(cb)cb(value);return;}
                sourceWait.stop();pending->waitingSource=false;pending->sourceReacquire=true;pending->refreshing=false;pending->proof={};pending->frame.reset();pending->conservation={};
                if(!phase(false))return;
                if(!phase(true,"snapshot"))return;
                if(!runtime->inspectInventory(pending->inventory,admittedSourceRun))finish("Fresh acquisition after genuine SOURCE admission unavailable");
                if(!guard)return;
            }
        }
        const auto cb=callbacks.publication;if(cb)cb(value);
    }
    void sourceFinished(const NativeSourceAttempt& value){
        const QPointer<NativeApplicationInventory> guard(owner);
        const auto callback=callbacks.sourceFinished;if(callback)callback(value);
        if(!guard||!pending||!pending->waitingSource||pending->sourceRequest!=value.requestId)return;
        // A successful finite live read can end before the producer's last
        // write. Continue only bytes ALREADY observed by this stopped inventory,
        // never retry a failure or infer EOF from a successful SSH exit.
        const auto& frame=pending->frame;
        const bool observedSuffix=value.status==NativeSourceAttemptStatus::Complete&&value.nextOffset&&frame&&frame->pendingBytes&&
            *frame->pendingBytes>=0&&frame->offset<=*value.nextOffset&&
            frame->nextOffset<=std::numeric_limits<qint64>::max()-*frame->pendingBytes&&
            frame->nextOffset+*frame->pendingBytes>*value.nextOffset;
        if(observedSuffix&&callbacks.sourceBoundaryRead){
            const auto read=callbacks.sourceBoundaryRead;
            const auto captured=*frame;
            const auto request=read(pending->request,pending->inventory,captured);
            if(!guard)return;
            if(!pending||!pending->waitingSource||pending->sourceRequest!=value.requestId)return;
            if(canonical(request)&&request!=value.requestId){pending->sourceRequest=request;return;}
        }
        finish("SOURCE boundary read ended before a genuinely conserved publication; no automatic retry");
    }
    bool phase(bool active,const QString& kind={}){
        if(!pending)return false;
        if(!active&&pending->phaseId.isEmpty())return true;
        if(active){pending->phase=kind;pending->phaseId=QUuid::createUuid().toString(QUuid::WithoutBraces);}
        const auto packet=QJsonObject{{"type","application_inventory_phase"},{"request_id",pending->request},{"inventory_id",pending->inventory},
            {"purpose",pending->close?"close":pending->boundary?"boundary":"start"},{"phase",pending->phase},{"phase_id",pending->phaseId},{"active",active}};
        if(!active){pending->phase.clear();pending->phaseId.clear();}
        const QPointer<NativeApplicationInventory> guard(owner);
        if(config.timingAudit&&callbacks.timing){auto value=packet;value.insert("schema","pgn-native-inventory-timing-1");value.insert("stage",active?"phase_begin":"phase_end");value.insert("monotonic_ms",monotonicMs());const auto timing=callbacks.timing;try{timing(value);}catch(...){}if(!guard)return false;}
        const auto cb=callbacks.phase;if(cb)cb(packet);return bool(guard);
    }
    QJsonObject envelope(const QString& request,const QString& inventoryId,bool close,bool boundary=false)const{
        return {{"type",close?"application_close_inventory":boundary?"native_work_boundary_inventory":"application_start_inventory"},{"request_id",request},{"inventory_id",inventoryId},{"accepted",false},{"snapshot",QJsonValue::Null}};
    }
    void refuse(const QString& request,const QString& inventoryId,bool close,const QString& error,bool boundary=false){
        auto packet=envelope(request,inventoryId,close,boundary);packet.insert("error",error.left(400));
        if(close){packet.insert("constant_known",false);packet.insert("inventory_known",false);packet.insert("unknown_devices",QJsonArray{Constant,Finite});
            packet.insert("finite_device",QJsonObject{{"device",Finite},{"status","unknown"},{"error","No accepted fresh finite observation"}});}
        publish(packet);
    }
    bool inspect(const QString& request,const QString& inventoryId,bool close,bool boundary=false){
        requireOwner();const QPointer<NativeApplicationInventory> guard(owner);
        if(stopping||!canonical(request)||!canonical(inventoryId)||pending||usedInventories.contains(inventoryId)||
           (close?(!writer.frozen()||writer.closeRequestId()!=request):writer.frozen())){
            refuse(request,inventoryId,close,"Fresh correlated inventory unavailable, consumed, busy or frozen",boundary);return false;
        }
        usedInventories.insert(inventoryId);
        pending=Pending{};pending->request=request;pending->inventory=inventoryId;pending->close=close;pending->boundary=boundary;pending->beganMonotonicMs=monotonicMs();
        const bool admitted=writer.enqueue(NativeApplicationOperation::Read,[guard](auto done){
            if(!guard){done("Inventory owner unavailable");return;}auto& self=*guard->d_;
            if(!self.pending){done("Inventory no longer pending");return;}
            self.pending->done=std::move(done);
            if(self.stopping){self.finish("Inventory cancelled before acquisition");return;}
            // The request identity is also the lower transport correlation.
            if(!self.phase(true,"snapshot"))return;
            if(!self.runtime->inspectInventory(self.pending->inventory,self.admittedSourceRun))self.finish("SOURCE worker busy; no acquisition performed");
        },[guard,request,inventoryId,close,boundary](const auto& result){
            if(!guard||result.entered)return;auto& self=*guard->d_;
            if(self.pending&&self.pending->inventory==inventoryId)self.pending.reset();
            self.refuse(request,inventoryId,close,result.error,boundary);
        });
        if(!admitted&&guard){if(pending&&pending->inventory==inventoryId)pending.reset();refuse(request,inventoryId,close,"ONE writer refused inventory admission; no acquisition performed",boundary);}
        return admitted;
    }
    void inventory(const NativeSourceInventory& value){
        if(!pending||value.requestId!=pending->inventory)return;
        if(!phase(false))return;
        auto& p=*pending;
        p.acquisitionEpoch=value.epoch;
        // Closing a typed OFF/unowned process does not require CUDA telemetry.
        // Start/boundary still require a positive driver; all manifest, raw
        // conservation, lineage, post-proof refresh and final STOP gates stay.
        p.constantKnown=value.transport.status==SshObservationStatus::Complete&&value.transport.exitCode==0&&value.frame&&constantKnown(value.frame->packet,p.close);
        p.constantError=value.error;
        p.driverUnavailable=value.frame&&value.transport.status==SshObservationStatus::Complete&&value.transport.exitCode==0&&
            value.frame->packet.value("schema")=="pgn-runtime-snapshot-1"&&value.frame->packet.value("driver_valid").isBool()&&
            value.frame->packet.value("driver_valid")==false;
        if(p.driverUnavailable&&p.constantError.isEmpty())p.constantError="NVIDIA driver observation unavailable; completed SOURCE and mining intent retained";
        if(!p.constantKnown&&p.constantError.isEmpty())p.constantError="Constant observation identity/ownership fields unavailable";
        if(!p.constantKnown){p.conservation=value.conservation;afterConstant();return;}
        if(p.sourceReacquire&&value.frame->packet.value("manifest")!=p.sourceManifest){finish("Fresh post-SOURCE snapshot changed the pinned physical manifest");return;}
        if(p.refreshing){
            if(value.sourceEpoch!=p.sourceEpoch||!p.frame||!sameProofBoundary(*p.frame,*value.frame)||p.conservation!=value.conservation){
                p.constantKnown=false;finish("Actual post-lineage acquisition changed SOURCE epoch or proof boundary; no proof reuse");return;
            }
        }else{p.sourceEpoch=value.sourceEpoch;}
        p.conservation=value.conservation;p.frame=value.frame;
        p.snapshot=value.frame->packet;
        if(p.refreshing){p.snapshot.insert("resume_lineage",p.proof);p.frame->packet=p.snapshot;}
        const QPointer<NativeApplicationInventory> guard(owner);
        if(!p.close&&!p.boundary&&callbacks.startObserved){const auto cb=callbacks.startObserved;const auto request=p.request,id=p.inventory;cb(request,id,*p.frame);if(!guard)return;}
        if(stopping){finish("Inventory cancelled after acquisition");return;}
        if(p.refreshing){afterConstant();return;}
        // Initial close is a PRE-STOP physical/finite observation only. SOURCE
        // may still advance; no lineage proof or post-proof refresh belongs to
        // this packet. The later inventory keeps all final proof requirements.
        if(p.close&&observedCloseRequest!=p.request){afterConstant();return;}
        const auto epoch=p.snapshot.value("manifest").toObject().value("run").toString();
        // Initial Close returned above. A previously admitted source may need
        // its later stopped suffix before Start, work-boundary or Final Close.
        // A cold observation must still reach the ordinary attachment path;
        // it cannot request a continuation of an epoch it never admitted.
        if(run(epoch)&&(p.close||admittedSourceRun==epoch)&&p.snapshot.value("owned")==false&&p.snapshot.value("state")=="OFF"&&p.snapshot.value("manifest").isObject()&&p.conservation.value("ready")!=true&&callbacks.sourceBoundaryRead){
            if(!phase(true,"source_read"))return;
            const auto cb=callbacks.sourceBoundaryRead;const auto request=cb(p.request,p.inventory,*p.frame);if(!guard)return;
            if(!canonical(request)){finish("SOURCE boundary read unavailable or already consumed; no conservation inferred");return;}
            p.sourceRequest=request;p.sourceManifest=p.snapshot.value("manifest").toObject();p.sourceProgress=p.conservation.value("committed_offset").toInteger(-1);p.waitingSource=true;sourceWait.start();return;
        }
        // A running initial inventory must not scan historical bytes before
        // STOP can enter. Only a stopped boundary needs the lineage verifier.
        if(run(epoch)&&p.snapshot.value("owned")==false&&p.snapshot.value("state")=="OFF"){
            if(!phase(true,"lineage"))return;
            if(runtime->verifyLineage(p.inventory,config.bookingDatabase,p.snapshot))return;
            if(!phase(false))return;
            p.snapshot.insert("resume_lineage",QJsonObject{{"schema","pgn-source-resume-lineage-1"},{"ready",false},{"error","Same-worker lineage boundary unavailable"}});
        }
        afterConstant();
    }
    void lineage(const NativeSourceLineage& value){
        if(!pending||value.requestId!=pending->inventory)return;
        if(!phase(false))return;
        const auto expected=pending->snapshot.value("manifest").toObject().value("run").toString();
        if(value.run!=expected||value.sourceEpoch!=pending->sourceEpoch){finish("SOURCE lineage returned a different epoch");return;}
        if(stopping||pending->lineageCancelled){finish("Inventory lineage cancelled; no refreshed proof or admission");return;}
        pending->proof=value.proof;pending->refreshing=true;
        if(!pending->close&&callbacks.beforeRefresh){
            pending->refreshGuardPending=true;const auto request=pending->request,id=pending->inventory;
            if(!phase(true,"profile"))return;
            const QPointer<NativeApplicationInventory> guard(owner);const auto qualify=callbacks.beforeRefresh;
            try{qualify(request,id,[guard,id](QString error){
                if(!guard||!guard->d_->pending||guard->d_->pending->inventory!=id||!guard->d_->pending->refreshGuardPending)return;
                auto& self=*guard->d_;self.pending->refreshGuardPending=false;
                if(!self.phase(false))return;
                if(!error.isEmpty()||self.stopping){self.finish(error.isEmpty()?"Inventory cancelled during profile qualification":error.left(400));return;}
                self.refresh();
            });}catch(const std::exception& error){if(guard&&pending&&pending->inventory==id)finish("Profile qualification failed: "+QString::fromUtf8(error.what()).left(300));}
            return;
        }
        refresh();
    }
    void refresh(){
        if(!pending)return;
        // Expensive verification cannot renew old evidence. Pay one NEW
        // acquisition, and retain the proof only at the identical boundary.
        if(!phase(true,"snapshot"))return;
        if(!runtime->inspectInventory(pending->inventory,admittedSourceRun))finish("Actual post-lineage acquisition unavailable; old proof not published");
    }
    void afterConstant(){
        if(!pending)return;
        if(stopping){finish("Inventory cancelled before finite acquisition");return;}
        if(!pending->close||!config.finiteManaged){finish();return;}
        const QPointer<NativeApplicationInventory> guard(owner);
        if(!phase(true,"snapshot"))return;
        try{
            auto* transport=new NativeControlTransport(owner);finite=transport;
            const auto command=NativeControlCommand::finite3060Snapshot(config.source.endpoint);
            transport->start(command,config.finiteTimeouts,[guard,transport](const auto& result){
                transport->deleteLater();if(!guard)return;auto& self=*guard->d_;self.finite.clear();if(!self.pending)return;
                if(!self.phase(false))return;
                auto& p=*self.pending;p.finiteKnown=result.status==NativeControlTransportStatus::CompleteResponse&&
                    result.control.outcome==NativeControlOutcome::ObservationReported&&result.control.finiteObservation.has_value();
                if(p.finiteKnown){p.finiteSnapshot=result.control.finiteObservation->packet;p.finiteObservation=result.control.finiteObservation;}
                else p.finiteError=!result.error.isEmpty()?result.error:result.control.error;
                self.finish();
            });
        }catch(const std::exception& error){if(finite){finite->deleteLater();finite.clear();}pending->finiteError=QString::fromUtf8(error.what());finish();}
    }
    void finish(const QString& localError={}){
        if(!pending||!phase(false))return;auto p=std::move(*pending);pending.reset();
        const bool initial=p.close&&observedCloseRequest!=p.request;
        sourceWait.stop();if(p.close)observedCloseRequest=p.request;
        auto packet=envelope(p.request,p.inventory,p.close,p.boundary);
        packet.insert("accepted",localError.isEmpty()&&p.constantKnown&&(!p.close||p.finiteKnown||!config.finiteManaged));
        packet.insert("snapshot",p.constantKnown?QJsonValue(p.snapshot):QJsonValue(QJsonValue::Null));
        packet.insert("source_conservation",p.conservation);
        packet.insert("retry_read_after_driver_recovery",p.driverUnavailable);
        if(config.timingAudit&&callbacks.timing){const QPointer<NativeApplicationInventory> guard(owner);const auto timing=callbacks.timing;
            try{timing({{"schema","pgn-native-inventory-timing-1"},{"stage","inventory_done"},{"request_id",p.request},{"inventory_id",p.inventory},{"monotonic_ms",monotonicMs()},{"accepted",packet.value("accepted")},{"constant_known",p.constantKnown},{"finite_known",p.close?QJsonValue(p.finiteKnown):QJsonValue(QJsonValue::Null)}});}catch(...){}if(!guard)return;}
        if(p.close){
            packet.insert("constant_known",p.constantKnown);packet.insert("inventory_known",p.constantKnown&&(p.finiteKnown||!config.finiteManaged));
            if(!config.finiteManaged){packet.insert("managed_devices",QJsonArray{Constant});packet.insert("finite_device",QJsonObject{{"device",Finite},{"status","unmanaged"}});}
            else packet.insert("finite_device",p.finiteKnown?QJsonObject{{"device",Finite},{"status","observed"},{"snapshot",p.finiteSnapshot}}:
                QJsonObject{{"device",Finite},{"status","unknown"},{"error",p.finiteError.left(400)}});
            QJsonArray unknown;if(!p.constantKnown)unknown.append(Constant);if(config.finiteManaged&&!p.finiteKnown)unknown.append(Finite);packet.insert("unknown_devices",unknown);
        }
        const auto error=!localError.isEmpty()?localError:!p.constantKnown?p.constantError:p.close&&config.finiteManaged&&!p.finiteKnown?p.finiteError:QString{};
        if(!error.isEmpty())packet.insert("error",error.left(400));
        // Settle writer before outward callback. That callback may delete us;
        // never dereference an owner after calling it. ACK is not Ready.
        if(p.frame)p.frame->packet=p.snapshot;
        const auto boundary=callbacks.boundary;
        const auto packetCallback=callbacks.packet;const QPointer<NativeApplicationInventory> guard(owner);
        if(initial&&localError.isEmpty()&&p.constantKnown&&p.finiteKnown&&p.frame&&p.finiteObservation&&callbacks.closeInitialObserved){
            const auto callback=callbacks.closeInitialObserved;
            callback({p.request,p.inventory,p.acquisitionEpoch,p.beganMonotonicMs,*p.frame,*p.finiteObservation});
            if(!guard){if(p.done)p.done("Inventory owner destroyed during Initial observation; no close permission");return;}
        }
        if(p.done)p.done(error);if(!guard)return;if(packetCallback)packetCallback(packet);
        if(guard&&p.boundary&&localError.isEmpty()&&p.constantKnown&&p.frame&&boundary)boundary(p.request,p.inventory,*p.frame,p.conservation);
    }
};
NativeApplicationInventory::NativeApplicationInventory(NativeApplicationWriter& writer,NativeApplicationInventoryConfig config,NativeApplicationInventoryCallbacks callbacks,QObject* parent)
    :QObject(parent),d_(std::make_unique<Impl>(this,writer,std::move(config),std::move(callbacks))){}
NativeApplicationInventory::~NativeApplicationInventory(){d_->requireOwner();}
bool NativeApplicationInventory::prepareClose(const QString& id){
    d_->requireOwner();if(d_->stopping||!canonical(id))return false;const QPointer<NativeApplicationInventory> guard(this);
    const bool accepted=d_->writer.prepareClose(id,[guard,id](const auto& result){if(guard){
        // An accepted retry begins a new physical Initial, not a new SOURCE
        // repair allowance. No target, uncertainty or proof is cleared here.
        if(result.entered)guard->d_->observedCloseRequest.clear();
        guard->d_->publish({{"type","application_close_frozen"},{"request_id",id},{"accepted",result.entered},{"error",result.error}});
    }});
    if(accepted&&guard)cancelLineage();return accepted;
}
bool NativeApplicationInventory::inspectStart(const QString& request,const QString& inventory){return d_->inspect(request,inventory,false);}
bool NativeApplicationInventory::inspectBoundary(const QString& request,const QString& inventory){return d_->inspect(request,inventory,false,true);}
bool NativeApplicationInventory::inspectClose(const QString& request,const QString& inventory){return d_->inspect(request,inventory,true);}
NativeSourceRuntime& NativeApplicationInventory::source(){d_->requireOwner();return *d_->runtime;}
bool NativeApplicationInventory::cancelLineage(){
    d_->requireOwner();if(!d_->pending||!d_->runtime->cancelLineage())return false;
    d_->pending->lineageCancelled=true;return true;
}
void NativeApplicationInventory::stop(){d_->requireOwner();if(d_->stopping)return;d_->stopping=true;if(d_->finite)d_->finite->cancel();d_->runtime->stop();}
}

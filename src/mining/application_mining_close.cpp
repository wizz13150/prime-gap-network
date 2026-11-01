#include "mining/run_source_profile.h"
#include "mining/application_mining_close.h"
#include "mining/native_bridge.h"
#include "mining/mining_shutdown_gate.h"
#include "mining/source_stop_checkpoint_packet.h"
#include "mining/completion_advance_gate.h"
#include "mining/source_decimal.h"
#include "mining/source_queued_identity.h"
#include "mining/native_finite_device.h"
#include "booking/booking_store.h"
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <utility>
#include <QUuid>
#include <QWidget>
#include <sqlite3.h>
#include <stdexcept>
#include <tuple>

namespace pgn::mining {
namespace {
const QString Constant="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",Finite="GPU-33f44d5c-91e4-6ab6-1e45-8c93135fd265";
const QString ConservationScope="LOGICAL_SOURCE_EPOCH_CONSERVATION_NOT_GLOBAL_POWER_CUT_DURABILITY";
QString uuid(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
bool flag(const QJsonObject& o,const char* name,bool expected){const auto v=o.value(name);return v.isBool()&&v.toBool()==expected;}
bool device(const QJsonValue& v,const QString& expected){return v.isString()&&v.toString().compare(expected,Qt::CaseInsensitive)==0;}
bool run(const QJsonValue& v){static const QRegularExpression p("^[0-9a-f]{32}$");return v.isString()&&v.toString().size()==32&&p.match(v.toString()).hasMatch();}
bool decimal(const QJsonValue& v){return v.isString()&&v.toString().size()<=160&&source_decimal::canonical(v.toString());}
bool utc(const QJsonValue& v){
    if(!v.isString())return false;static const QRegularExpression p("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}(?:\\.\\d{1,9})?Z$");
    return p.match(v.toString()).hasMatch()&&QDateTime::fromString(v.toString(),Qt::ISODate).isValid();
}
std::optional<qint64> offset(const QJsonValue& v){
    if(!v.isDouble())return {};const auto n=v.toInteger(-1);
    if(n<0||n>9007199254740991LL||v.toDouble()!=double(n))return {};return n;
}
bool exitZero(const QJsonObject& o,const QString& expected){return o.value("run")==expected&&offset(o.value("exit_code"))==std::optional<qint64>(0);}
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
auto fields(const SourceCheckpointResume& p){return std::tie(p.run,p.start,p.end,p.length,p.positions,p.nextStart,p.remaining,p.tests,p.gaps,p.batches,p.counterTimeNs,p.checkpointLabel,p.checkpointSha256);}
}
class ApplicationMiningClose::Impl {
public:
    enum class Phase {Idle,Freezing,Initial,Final,Held,Draining,Ready,MaintenanceDraining,MaintenanceReady};
    QString drainNonce;
    ApplicationMiningClose& owner;QPointer<QWidget> window;QPointer<QCoreApplication> application;QPointer<NativeBridge> bridge;
    QString database,label,closeId,inventoryId,message;Phase phase{Phase::Idle};QTimer deadline,poll;
    QString lineagePhaseId;int remainingBudget{};
    MiningShutdownGate gate;QString gateId;std::optional<MiningStopTarget> target;
    bool pinned{},finiteWasOwned{},finiteUnknownAtStop{},finiteKnownBeforeStop{},inFlight{},stopsSent{};int inventories{};QHash<QString,QString> controls;
    QJsonObject finiteBeforeStopProof;
    bool finiteManaged{true};
    Impl(ApplicationMiningClose& o,QWidget* w,QCoreApplication* a,NativeBridge* b,QString db,QString checkpoint)
        :owner(o),window(w),application(a),bridge(b),database(std::move(db)),label(std::move(checkpoint)),closeId(uuid()){
        deadline.setSingleShot(true);poll.setSingleShot(true);
        deadline.setParent(&o);deadline.setObjectName("application-close-deadline");
        QObject::connect(&deadline,&QTimer::timeout,&owner,[this]{hold("Close held:45s deadline expired; explicit retry required");});
        QObject::connect(&poll,&QTimer::timeout,&owner,[this]{inspect();});
        if(w&&w->thread()==o.thread())w->installEventFilter(&o);
        if(a&&a->thread()==o.thread())a->installEventFilter(&o);
        if(!w||!a||w->thread()!=o.thread()||a->thread()!=o.thread()||!b||b->thread()!=o.thread()||QThread::currentThread()!=o.thread()){
            phase=Phase::Held;message="Close unavailable: same-thread window/application/controller required";return;
        }
        const auto weak=QPointer<ApplicationMiningClose>(&o);
        const auto oldPhase=b->onApplicationInventoryPhase;
        b->onApplicationInventoryPhase=[weak,oldPhase](const NativeApplicationInventoryPhase& event){if(weak)weak->d_->inventoryPhase(event);if(oldPhase)oldPhase(event);};
        const auto oldControl=b->onControl;
        b->onControl=[weak,oldControl](const QString& gpu,const QString& id,bool applied,const QString& detail){
            if(oldControl)oldControl(gpu,id,applied,detail);if(weak)weak->d_->control(gpu,id,applied,detail);
        };
        const auto oldFrozen=b->onApplicationCloseFrozen;
        b->onApplicationCloseFrozen=[weak,oldFrozen](const QString& id,bool accepted,const QString& error){
            if(oldFrozen)oldFrozen(id,accepted,error);if(weak)weak->d_->frozen(id,accepted,error);
        };
        const auto oldInventory=b->onApplicationCloseInventory;
        b->onApplicationCloseInventory=[weak,oldInventory](const QString& id,const QJsonObject& packet){
            if(oldInventory)oldInventory(id,packet);if(weak)weak->d_->inventory(id,packet);
        };
        QObject::connect(b,&QObject::destroyed,&o,[this]{if(phase!=Phase::Ready)hold("Close held: controller owner lost");});
    }
    ~Impl(){if(window)window->removeEventFilter(&owner);if(application)application->removeEventFilter(&owner);}
    void notify(){const auto cb=owner.onStatus;if(cb)cb(message);}
    bool resumeInventoryBudget(){
        if(!lineagePhaseId.isEmpty()){
            lineagePhaseId.clear();const int budget=std::exchange(remainingBudget,0);
            if(budget<=0)return false;deadline.start(budget);
        }
        return deadline.isActive()&&deadline.remainingTime()>0;
    }
    void inventoryPhase(const NativeApplicationInventoryPhase& event){
        if(!inFlight||(phase!=Phase::Initial&&phase!=Phase::Final)||event.purpose!=NativeInventoryPurpose::Close||event.requestId!=closeId||event.inventoryId!=inventoryId)return;
        if(event.phase==NativeInventoryPhaseKind::Lineage||event.phase==NativeInventoryPhaseKind::SourceRead){
            if(event.active){
                if(!lineagePhaseId.isEmpty()||!deadline.isActive()||deadline.remainingTime()<=0)return;
                remainingBudget=deadline.remainingTime();lineagePhaseId=event.phaseId;deadline.stop();
                message="Closing: native RAW read/lineage pending; UI budget suspended, transport idle deadlines unchanged";notify();
            }else if(lineagePhaseId==event.phaseId){
                if(!resumeInventoryBudget())return hold("Close held: UI deadline expired before lineage phase");
                message="Closing: native lineage phase ended; fresh acquisition still required";notify();
            }
        }else if(event.active&&lineagePhaseId.isEmpty()&&deadline.isActive()&&deadline.remainingTime()>0)deadline.start(45000);
    }
    bool stopBoth(){
        if(stopsSent)return true;stopsSent=true;
        const auto a=uuid(),b=uuid();controls.insert(a,Constant);if(finiteManaged)controls.insert(b,Finite);
        const QPointer<ApplicationMiningClose> weak(&owner);const auto controller=bridge;
        if(!controller)return true;controller->control(Constant,a,false);if(!weak)return false;
        if(bridge&&finiteManaged)bridge->control(Finite,b,false);return bool(weak);
    }
    void hold(QString error){
        if(phase==Phase::MaintenanceDraining||phase==Phase::MaintenanceReady){
            deadline.stop();poll.stop();phase=Phase::Held;message="Maintenance detach held: "+error.left(400);notify();return;
        }
        if(phase==Phase::Ready)return;
        // A frozen but UNKNOWN inventory still requests both existing safe
        // STOP endpoints. Never manufacture a verified target or a Ready gate.
        if(phase==Phase::Initial&&!stopsSent){if(finiteManaged)finiteUnknownAtStop|=!finiteKnownBeforeStop;gate.begin({},false,false);if(!stopBoth())return;}
        phase=Phase::Held;inFlight=false;deadline.stop();poll.stop();lineagePhaseId.clear();remainingBudget=0;
        if(bridge)bridge->retireApplicationInventoryPhases(NativeInventoryPurpose::Close,closeId,inventoryId);
        message=std::move(error);notify();
    }
    void begin(){
        if(phase!=Phase::Idle&&phase!=Phase::Held)return;
        if(!bridge||!window||!application||QThread::currentThread()!=owner.thread()||bridge->thread()!=owner.thread())return hold("Close held: required owner/thread unavailable");
        if(!QFileInfo(database).isAbsolute()||database.contains(QChar(0))||label.isEmpty()||label.toUtf8().size()>4096||label.contains(QChar(0))||label.contains('\r')||label.contains('\n'))
            return hold("Close held: explicit database/checkpoint profile unavailable");
        gate.reset();gateId.clear();controls.clear();inFlight=false;stopsSent=false;finiteKnownBeforeStop=!finiteBeforeStopProof.isEmpty();inventories=0;inventoryId.clear();lineagePhaseId.clear();remainingBudget=0;phase=Phase::Freezing;deadline.start(45000);
        message="Closing: freezing new work; mining stop/storage not yet confirmed";
        const QPointer<ApplicationMiningClose> weak(&owner);const auto b=bridge;
        if(!b->prepareApplicationClose(closeId)&&weak&&phase==Phase::Freezing)hold("Close held: admission freeze unconfirmed; explicit retry required");
        else if(weak&&phase==Phase::Freezing)notify();
    }
    void frozen(const QString& id,bool accepted,const QString& error){
        if(id!=closeId||phase!=Phase::Freezing)return;
        if(!accepted)return hold("Close held: "+error.left(400));phase=Phase::Initial;inspect();
    }
    void inspect(){
        if(phase!=Phase::Initial&&phase!=Phase::Final)return;if(inFlight)return;
        if(inventories>=4)return hold("Close held: bounded inventory attempts exhausted; explicit retry required");
        if(!bridge)return hold("Close held: controller unavailable");
        ++inventories;inventoryId=uuid();inFlight=true;
        if(!bridge->inspectApplicationClose(closeId,inventoryId)){inFlight=false;hold("Close held: inventory submission unconfirmed; explicit retry required");}
    }
    void retry(QString why){
        if(inventories>=4)return hold("Close held: "+why+"; explicit retry required");
        message="Closing: "+why+"; bounded fresh inventory pending";poll.start(300);notify();
    }
    void control(const QString& gpu,const QString& id,bool applied,const QString& detail){
        if(phase!=Phase::Final||!controls.contains(id)||controls.value(id).compare(gpu,Qt::CaseInsensitive)!=0)return;
        if(gpu.compare(Constant,Qt::CaseInsensitive)==0&&!gateId.isEmpty())gate.acknowledgeStop(gateId,applied,detail);
        // A transport ACK, positive OR negative, never commits storage or
        // defeats a later stronger stopped proof. Inspect is separately bounded.
    }
    void requireNoPrepared(){
        sqlite3* connection{};
        const auto opened=sqlite3_open_v2(database.toUtf8().constData(),&connection,SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr);
        if(opened!=SQLITE_OK){if(connection)sqlite3_close(connection);throw std::runtime_error("Same SQLite dispatch reconciliation unavailable");}
        sqlite3_busy_timeout(connection,200);sqlite3_stmt* query{};
        const auto prepared=sqlite3_prepare_v2(connection,"SELECT state FROM dispatch_requests WHERE typeof(state)<>'text' OR state='PREPARED' LIMIT 1",-1,&query,nullptr);
        const auto result=prepared==SQLITE_OK?sqlite3_step(query):SQLITE_ERROR;
        if(query)sqlite3_finalize(query);sqlite3_close(connection);
        require(result==SQLITE_DONE,"Same SQLite dispatch table absent/unknown or PREPARED transition unresolved");
    }
    void drained(const QString& token){
        if(phase!=Phase::Draining||token!=drainNonce)return;
        requireNoPrepared();
        phase=Phase::Ready;message="Ready to close: observed stop, logical source conservation, local storage and observer cleanup confirmed";
        const QPointer<ApplicationMiningClose> weak(&owner);notify();if(!weak)return;
        QTimer::singleShot(0,&owner,[weak]{if(!weak||!weak->readyToQuit())return;const auto w=weak->d_->window;if(w)w->close();if(weak&&weak->d_->application)QCoreApplication::quit();});
    }
    void ready(){
        requireNoPrepared(); // Freeze is not a substitute for SAME-store reconciliation.
        phase=Phase::Draining;drainNonce=uuid();const auto token=drainNonce;deadline.stop();poll.stop();message="Closing: physical/storage gates passed; awaiting native observer cleanup";
        const QPointer<ApplicationMiningClose> weak(&owner);const auto drain=owner.onDrainBeforeQuit;
        notify();if(!weak)return;
        if(!drain){drained(token);return;}
        try{drain([weak,token]{if(weak)QMetaObject::invokeMethod(weak,[weak,token]{
            if(!weak)return;try{weak->d_->drained(token);}catch(const std::exception& e){weak->d_->hold("Close held: native cleanup reconciliation failed: "+QString::fromUtf8(e.what()).left(300));}
        },Qt::QueuedConnection);});}
        catch(const std::exception& e){if(weak)hold("Close held: native observer cleanup failed: "+QString::fromUtf8(e.what()).left(300));}
        catch(...){if(weak)hold("Close held: native observer cleanup failed; no quit permission");}
    }
    void rememberFiniteBeforeStop(const QJsonObject& packet){
        if(!finiteManaged)return;
        if(phase!=Phase::Initial||stopsSent||packet.value("type")!="application_close_inventory")return;
        const auto f=packet.value("finite_device").toObject(),s=f.value("snapshot").toObject();
        if(f.value("status")!="observed"||!device(f.value("device"),Finite)||
           s.value("schema")!="pgn-device-control-observation-1"||!device(s.value("device"),Finite)||s.value("scope")!="BOUNDED_SCIENCE_TEST"||
           !s.value("owned").isBool()||!s.value("desired_enabled").isBool()||!utc(s.value("observed_utc")))return;
        finiteWasOwned|=s.value("owned").toBool();
        const auto unknown=packet.value("unknown_devices");if(!unknown.isArray())return;
        for(const auto& entry:unknown.toArray())if(!entry.isString()||device(entry,Finite))return;
        // A clean finite observation can precede our FIRST STOP even when the
        // independent constant acquisition failed. Causal ordering, not a
        // remote-vs-local wall-clock comparison, pays this distinction.
        // A genuinely unknown earlier STOP can NEVER be excused by a retry.
        if(finiteUnknownAtStop||!finiteBeforeStopProof.isEmpty()||!flag(s,"owned",false)||!flag(s,"desired_enabled",false))return;
        const auto m=s.value("manifest");if(!m.isNull()){
            if(!m.isObject())return;const auto manifest=m.toObject(),ended=s.value("last_exit").toObject();const auto pid=offset(manifest.value("pid"));
            if(!run(manifest.value("run"))||!device(manifest.value("uuid"),Finite)||!pid||*pid<=0||*pid>4294967295LL||!utc(manifest.value("birth"))||
               manifest.value("image_sha256")!=Finite3060ImageSha256||manifest.value("start")!=Finite3060Start||manifest.value("length")!=Finite3060Length||
               !exitZero(ended,manifest.value("run").toString())||!utc(ended.value("utc")))return;
        }
        finiteBeforeStopProof=s;finiteKnownBeforeStop=true;
    }
    void inventory(const QString& id,const QJsonObject& packet){
        if(!inFlight||id!=closeId||(phase!=Phase::Initial&&phase!=Phase::Final)||packet.value("request_id")!=closeId||packet.value("inventory_id")!=inventoryId)return;
        if(!resumeInventoryBudget())return hold("Close held: UI deadline expired; inventory cannot resurrect it");
        inFlight=false;
        try{
            rememberFiniteBeforeStop(packet);
            require(packet.value("type")=="application_close_inventory"&&flag(packet,"accepted",true)&&flag(packet,"inventory_known",true)&&flag(packet,"constant_known",true),"Physical inventory is unknown/refused");
            require(packet.value("unknown_devices").isArray()&&packet.value("unknown_devices").toArray().isEmpty(),"Unknown device inventory cannot be ignored");
            require(packet.value("snapshot").isObject()&&packet.value("finite_device").isObject(),"Both typed physical observations required");
            const auto snapshot=packet.value("snapshot").toObject(),binding=snapshot.value("binding").toObject(),finite=packet.value("finite_device").toObject();
            const bool driverForClose=flag(snapshot,"driver_valid",true)||
                (flag(snapshot,"driver_valid",false)&&flag(snapshot,"owned",false)&&snapshot.value("state")=="OFF");
            require(snapshot.value("schema")=="pgn-runtime-snapshot-1"&&driverForClose&&snapshot.value("owned").isBool()&&utc(snapshot.value("observed_utc"))&&device(binding.value("uuid"),Constant),"Constant-device observation identity/types invalid");
            const auto fs=finite.value("snapshot").toObject();
            if(finiteManaged){
                require(finite.value("status")=="observed"&&device(finite.value("device"),Finite)&&finite.value("snapshot").isObject(),"Finite-device observation unavailable");
                require(fs.value("schema")=="pgn-device-control-observation-1"&&device(fs.value("device"),Finite)&&fs.value("scope")=="BOUNDED_SCIENCE_TEST"&&fs.value("owned").isBool()&&fs.value("desired_enabled").isBool()&&utc(fs.value("observed_utc")),"Finite-device observation identity/types invalid");
                if(phase==Phase::Initial)finiteKnownBeforeStop=true;finiteWasOwned|=fs.value("owned").toBool();
            }else require(finite.value("status")=="unmanaged"&&device(finite.value("device"),Finite)&&packet.value("managed_devices")==QJsonArray{Constant},"Unmanaged device domain differs from trusted local configuration");
            const auto manifestValue=snapshot.value("manifest");require(manifestValue.isNull()||manifestValue.isObject(),"Manifest absence must be explicit null");
            std::optional<MiningStopTarget> observed;
            if(manifestValue.isObject()){
                const auto m=manifestValue.toObject();
                require(m.value("schema")=="pgn-native-process-1"&&device(m.value("uuid"),Constant)&&run(m.value("run"))&&decimal(m.value("start"))&&decimal(m.value("end"))&&source_decimal::less(m.value("start").toString(),m.value("end").toString()),"Constant run/range is ambiguous");
                require(binding.value("start")==m.value("start")&&binding.value("end")==m.value("end"),"Binding target differs from manifest");
                observed=MiningStopTarget{Constant,m.value("run").toString(),m.value("start").toString(),m.value("end").toString()};
            }else require(flag(snapshot,"owned",false)&&snapshot.value("state")=="OFF","Null manifest cannot hide a physical owner");
            if(pinned){
                require(bool(observed)==bool(target),"Run appeared/disappeared during close; no retarget");
                if(target)require(std::tie(observed->run,observed->start,observed->end)==std::tie(target->run,target->start,target->end),"Run/target changed during close; no retarget");
            }
            if(phase==Phase::Initial){
                if(!pinned){target=observed;pinned=true;}
                requireNoPrepared();
                const auto requests=gate.begin(target?QVector<MiningStopTarget>{*target}:QVector<MiningStopTarget>{},true,true);
                require(gate.phase()!=MiningShutdownPhase::Held,"Stop gate rejected target inventory");if(target){require(requests.size()==1,"Stop gate request absent");gateId=requests[0].id;}
                phase=Phase::Final;if(!stopBoth())return;inspect();return; // NEVER qualify the initial observation.
            }
            if(!flag(snapshot,"owned",false)||!flag(snapshot,"operator_stop",true)||snapshot.value("state")!="OFF"||(finiteManaged&&(!flag(fs,"owned",false)||!flag(fs,"desired_enabled",false))))
                return retry("fresh STOP/disable observations not yet confirmed");
            if(finiteManaged){
            require(!finiteWasOwned,"Finite device was active: resumable artifact contract is unavailable; close held");
            const auto fm=fs.value("manifest");
            require(finiteBeforeStopProof.isEmpty()||(fm==finiteBeforeStopProof.value("manifest")&&fs.value("last_exit")==finiteBeforeStopProof.value("last_exit")),"Finite pre-STOP historical tuple changed; artifacts cannot be retargeted");
            require(!finiteUnknownAtStop||fm.isNull(),"Finite STOP occurred under unknown inventory; old run artifacts remain unqualified");
            if(!fm.isNull())require(fm.isObject()&&run(fm.toObject().value("run"))&&exitZero(fs.value("last_exit").toObject(),fm.toObject().value("run").toString()),"Finite old physical run lacks a typed successful exit");
            }
            if(!target)return ready(); // Both known idle, STOP markers fresh, no run invented.
            require(exitZero(snapshot.value("last_exit").toObject(),target->run),"Exact constant successful run exit absent");
            const auto c=packet.value("source_conservation").toObject();
            require(c.value("schema")=="pgn-source-conservation-1"&&c.value("scope")==ConservationScope&&c.value("run")==target->run&&c.value("source")=="native/"+target->run+".stdout","Source conservation epoch/source mismatch");
            const auto next=offset(snapshot.value("next_offset")),pending=offset(snapshot.value("pending_bytes")),end=offset(c.value("required_end"));
            const auto committed=offset(c.value("committed_offset")),mirror=offset(c.value("mirror_size")),transport=offset(c.value("transport_offset"));
            require(next&&pending&&end&&committed&&mirror&&transport&&*next<=9007199254740991LL-*pending&&*next+*pending==*end,"Final source boundary is not exact");
            if(!flag(c,"ready",true)||*committed!=*end||*mirror!=*end||*transport!=*end)return retry("final source conservation boundary is not committed exactly");
            QJsonObject completionView=snapshot;
            if(snapshot.value("source_completion_observation").isObject())completionView.insert("completion",snapshot.value("source_completion_observation")); // exact receipt alias only
            const auto done=completionView.value("completion").toObject().value("run")==target->run?completionView.value("completion").toObject():completionView.value("completion_candidate").toObject();
            if(done.value("run")==target->run){
                // A current claimed SOURCE completion with unavailable RAW
                // lineage is Held, not permission to save an older partial
                // checkpoint and quietly declare CloseReady instead.
                require(completeSourceMatches(completionView,target->run),"Current SOURCE completion requires exact ready RAW lineage and receipt; no checkpoint fallback");
                require(flag(done,"independent_coverage",false)&&done.value("proof_status")=="SOURCE_PRP_INDEPENDENT_PROOF_PENDING","Completion is not source-only pending-proof metadata");
                booking::BookingStore store(database);require(store.valid(),"Existing application SQLite unavailable");
                const auto intent=source_queued_identity::queued(snapshot.value("manifest").toObject());
                require(intent==source_queued_identity::queued(snapshot.value("binding").toObject()),"SOURCE manifest/binding queued identity differs");
                // The native caller, not a Node inventory side effect, owns
                // this one RUNTIME + LOCAL_INTENT + dispatch transaction.
                require(store.recordQueuedSourceCompletion(target->run,target->start,target->end,Constant,intent).isEmpty(),"Source/local intent/dispatch SQLite commit failed");
                bool saved=false;for(const auto& row:store.completedRanges())if(row.device.compare(Constant,Qt::CaseInsensitive)==0&&!source_decimal::less(target->start,row.start)&&!source_decimal::less(row.end,target->end))saved=true;
                require(saved,"Source completion readback mismatch");return ready();
            }
            const auto decoded=decodeSourceStopCheckpointPacket(snapshot,target->run,Constant,label,inspectRunSourceProfile(snapshot.value("manifest").toObject()).engineJob);
            require(decoded.evidence.has_value(),"Stopped checkpoint packet invalid/unavailable");
            require(gate.observeCheckpoint(gateId,*decoded.evidence),"Stop gate did not validate checkpoint");const auto proof=gate.checkpoint(gateId);
            require(bool(proof),"Validated checkpoint tuple unavailable");booking::BookingStore store(database);require(store.valid(),"Existing application SQLite unavailable");const auto error=store.saveResumeCheckpoint(Constant,*proof,decoded.evidence->checkpointBytes);
            if(!error.isEmpty()){gate.storageCommitted(gateId,*proof,false);return hold("Close held: resume SQLite commit failed: "+error.left(300));}
            const auto readback=store.savedResumeCheckpoint(Constant,target->run);
            require(readback&&readback->checkpointBytes==decoded.evidence->checkpointBytes&&fields(readback->resume)==fields(*proof),"Resume SQLite readback differs from validated proof");
            require(gate.storageCommitted(gateId,readback->resume,true)&&gate.phase()==MiningShutdownPhase::Ready,"Stop gate storage receipt is not Ready");ready();
        }catch(const std::exception& e){hold("Close held: "+QString::fromUtf8(e.what()).left(400)+"; explicit retry required");}
    }
};
ApplicationMiningClose::ApplicationMiningClose(QWidget* w,QCoreApplication* a,NativeBridge* b,QString db,QString label,QObject* parent,bool finiteManaged)
    :QObject(parent?parent:w),d_(std::make_unique<Impl>(*this,w,a,b,std::move(db),std::move(label))){d_->finiteManaged=finiteManaged;}
ApplicationMiningClose::~ApplicationMiningClose()=default;
void ApplicationMiningClose::requestClose(){if(d_->phase!=Impl::Phase::MaintenanceDraining&&d_->phase!=Impl::Phase::MaintenanceReady)d_->begin();}
bool ApplicationMiningClose::requestMaintenanceDetach(){
    if(QThread::currentThread()!=thread())return false;
    if(d_->phase==Impl::Phase::MaintenanceDraining||d_->phase==Impl::Phase::MaintenanceReady)return true;
    const auto drain=onDrainBeforeQuit;
    if(d_->phase!=Impl::Phase::Idle||!drain)return false;
    d_->phase=Impl::Phase::MaintenanceDraining;d_->message="Maintenance: draining UI observers only; no physical STOP/checkpoint permission";
    const QPointer<ApplicationMiningClose> weak(this);d_->notify();if(!weak)return false;
    try{drain([weak]{if(weak)QMetaObject::invokeMethod(weak,[weak]{
        if(!weak||weak->d_->phase!=Impl::Phase::MaintenanceDraining)return;
        weak->d_->phase=Impl::Phase::MaintenanceReady;weak->d_->message="Maintenance UI observers drained; not physical CloseReady";
        weak->d_->notify();if(!weak)return;
        QTimer::singleShot(0,weak,[weak]{if(weak&&weak->d_->phase==Impl::Phase::MaintenanceReady&&weak->d_->application)QCoreApplication::quit();});
    },Qt::QueuedConnection);});}
    catch(const std::exception& e){if(weak)weak->d_->hold(QString::fromUtf8(e.what()));return false;}
    catch(...){if(weak)weak->d_->hold("Observer drain failed");return false;}
    return true;
}
bool ApplicationMiningClose::readyToQuit()const{return d_->phase==Impl::Phase::Ready;}
QString ApplicationMiningClose::status()const{return d_->message;}
bool ApplicationMiningClose::eventFilter(QObject* object,QEvent* event){
    if((object==d_->window&&event->type()==QEvent::Close)||(object==d_->application&&event->type()==QEvent::Quit)){
        if(d_->phase==Impl::Phase::MaintenanceReady)return false;
        if(d_->phase==Impl::Phase::MaintenanceDraining){event->ignore();return true;}
        if(readyToQuit())return false;event->ignore();requestClose();return true;
    }return QObject::eventFilter(object,event);
}
}

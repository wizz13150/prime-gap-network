#include "mining/run_source_profile.h"
#include "mining/application_mining_start.h"
#include "mining/native_bridge.h"
#include "mining/source_queued_identity.h"
#include "mining/completion_advance_gate.h"
#include "mining/source_decimal.h"
#include "mining/source_stop_checkpoint_packet.h"
#include "booking/booking_store.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <utility>
#include <QUuid>
#include <sqlite3.h>
#include <stdexcept>

namespace pgn::mining {
namespace {
const QString Constant="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
const QString ConservationScope="LOGICAL_SOURCE_EPOCH_CONSERVATION_NOT_GLOBAL_POWER_CUT_DURABILITY";
constexpr qint64 MaxSafe=9007199254740991LL;
QString uuid(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
bool canonicalUUID(const QString& s){const auto id=QUuid::fromString(s);return !id.isNull()&&id.toString(QUuid::WithoutBraces)==s;}
void require(bool b,const char* error){if(!b)throw std::runtime_error(error);}
bool flag(const QJsonObject& o,const char* key,bool value){const auto v=o.value(key);return v.isBool()&&v.toBool()==value;}
bool device(const QJsonValue& v){return v.isString()&&v.toString().compare(Constant,Qt::CaseInsensitive)==0;}
bool run(const QJsonValue& v){static const QRegularExpression p("^[0-9a-f]{32}$");return v.isString()&&v.toString().size()==32&&p.match(v.toString()).hasMatch();}
bool decimal(const QJsonValue& v){return v.isString()&&v.toString().size()<=160&&source_decimal::canonical(v.toString());}
bool utc(const QJsonValue& v){
    if(!v.isString())return false;static const QRegularExpression p("^(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2})(?:\\.\\d{1,9})?Z$");
    const auto match=p.match(v.toString());if(!match.hasMatch()||match.capturedLength()!=v.toString().size())return false;
    const auto date=QDateTime::fromString(match.captured(1)+'Z',Qt::ISODate);
    return date.isValid()&&date.toUTC().toString("yyyy-MM-dd'T'HH:mm:ss")==match.captured(1);
}
std::optional<qint64> offset(const QJsonValue& v){
    if(!v.isDouble())return {};const auto n=v.toInteger(-1);
    if(n<0||n>MaxSafe||v.toDouble()!=double(n))return {};return n;
}
using source_queued_identity::queued;
void conserved(const QJsonObject& snapshot,const QJsonObject& c,const QString& epoch){
    require(c.value("schema")=="pgn-source-conservation-1"&&c.value("scope")==ConservationScope&&flag(c,"ready",true)&&
        c.value("run")==epoch&&c.value("source")=="native/"+epoch+".stdout","Exact logical source conservation unavailable");
    const auto next=offset(snapshot.value("next_offset")),pending=offset(snapshot.value("pending_bytes")),end=offset(c.value("required_end"));
    const auto committed=offset(c.value("committed_offset")),mirror=offset(c.value("mirror_size")),transport=offset(c.value("transport_offset"));
    require(next&&pending&&end&&committed&&mirror&&transport&&*next<=MaxSafe-*pending&&*next+*pending==*end&&
        *committed>=*end&&*mirror>=*end&&*transport>=*end,"Source conservation does not cover the exact inventory boundary");
}
QJsonObject sourceReceipt(const QJsonObject& s,const QJsonObject& manifest,const QJsonObject& binding){
    const auto epoch=manifest.value("run").toString();QJsonObject receipt;bool sourceOnly=false;
    for(const auto* key:{"completion","source_completion_observation","completion_candidate"}){
        if(!s.value(key).isObject())continue;const auto candidate=s.value(key).toObject();
        if(candidate.value("run")==epoch){receipt=candidate;sourceOnly=QString::fromLatin1(key)=="source_completion_observation";break;}
    }
    require(!receipt.isEmpty(),"Ready requires the exact current full SOURCE receipt; resume remains required");
    auto view=s;view.insert("completion",receipt); // SOURCE identity alias; never an automatic-permission projection.
    if(!sourceLineageMatches(view,"complete")){
        const auto proof=s.value("resume_lineage").toObject();
        const auto reason=proof.value("error").toString().left(280);
        throw std::runtime_error((QString("SOURCE RAW lineage not admitted: ")+(reason.isEmpty()?QString("missing or inconsistent logical proof/profile"):reason)).toStdString());
    }
    require(s.value("last_exit").isObject()&&s.value("last_exit").toObject().value("run")==epoch&&completeSourceMatches(view,epoch),"SOURCE receipt range/count/successful exit is not exact");
    require(flag(receipt,"independent_coverage",false)&&receipt.value("proof_status")=="SOURCE_PRP_INDEPENDENT_PROOF_PENDING", "SOURCE proof scope is not independent coverage");
    require(queued(manifest)==queued(binding)&&queued(receipt)==queued(manifest),"SOURCE queued intent differs from manifest/binding");
    if(sourceOnly)require(receipt.value("evidence_scope")=="VERIFIED_SNAPSHOT_SOURCE_COMPLETION"&&receipt.value("source_observed_operator_stop").isBool()&&
        receipt.value("source_observed_operator_stop")==s.value("operator_stop")&&receipt.value("source_observed_stop_file").isBool(),"SOURCE-only observed STOP/scope is unknown");
    return receipt;
}
QString text(sqlite3_stmt* q,int column){
    require(sqlite3_column_type(q,column)==SQLITE_TEXT,"Admission readback contains a non-text identity");
    const QByteArray raw(reinterpret_cast<const char*>(sqlite3_column_text(q,column)),sqlite3_column_bytes(q,column));
    const auto s=QString::fromUtf8(raw);require(s.toUtf8()==raw&&!s.contains(QChar(0)),"Admission readback text encoding changed");return s;
}
struct Database {
    sqlite3* db{};sqlite3_stmt* query{};
    explicit Database(const QString& file){const auto rc=sqlite3_open_v2(file.toUtf8().constData(),&db,SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr);if(rc!=SQLITE_OK){if(db)sqlite3_close(db);db=nullptr;throw std::runtime_error("Same SQLite admission readback unavailable");}sqlite3_busy_timeout(db,200);}
    ~Database(){if(query)sqlite3_finalize(query);if(db)sqlite3_close(db);}
    void prepare(const char* sql){require(sqlite3_prepare_v2(db,sql,-1,&query,nullptr)==SQLITE_OK,"Same SQLite admission readback schema unavailable");}
    void bind(int column,const QString& value){const auto bytes=value.toUtf8();require(sqlite3_bind_text(query,column,bytes.constData(),bytes.size(),SQLITE_TRANSIENT)==SQLITE_OK,"Admission readback bind failed");}
};
struct RetainedResume {QString run,observation;};
std::optional<RetainedResume> retainedResume(const QString& file,const QString& request){
    Database db(file);db.prepare("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='dispatch_resume_epochs'");
    require(sqlite3_step(db.query)==SQLITE_ROW,"Resume origin schema unavailable");
    if(sqlite3_column_int(db.query,0)==0)return {};
    sqlite3_finalize(db.query);db.query=nullptr;
    db.prepare("SELECT run FROM dispatch_resume_epochs WHERE request_id=?");db.bind(1,request);
    const auto row=sqlite3_step(db.query);if(row==SQLITE_DONE)return {};
    require(row==SQLITE_ROW,"Retained resume origin unavailable");const auto next=text(db.query,0);
    require(sqlite3_step(db.query)==SQLITE_DONE,"Retained resume request ambiguous");
    booking::BookingStore store(file);const auto saved=store.savedCheckpointResumeEpoch(Constant,next);
    require(saved.status==booking::CheckpointResumeReadStatus::Present&&saved.epoch&&saved.epoch->requestUUID==request&&canonicalUUID(saved.epoch->observationUUID),"Retained resume origin differs from immutable epoch");
    return RetainedResume{next,saved.epoch->observationUUID};
}
std::optional<QString> retainedObservation(const QString& file,const QString& request){
    Database db(file);db.prepare("SELECT value FROM booking_metadata WHERE key=?");db.bind(1,"mining-initial-start/v1/"+request);
    const auto row=sqlite3_step(db.query);if(row==SQLITE_DONE){const auto resume=retainedResume(file,request);return resume?std::optional<QString>(resume->observation):std::nullopt;}
    require(row==SQLITE_ROW,"Retained initial receipt unavailable");const auto raw=text(db.query,0).toUtf8();
    require(raw.size()<=8192,"Retained initial receipt exceeds bound");QJsonParseError error;const auto json=QJsonDocument::fromJson(raw,&error);
    require(error.error==QJsonParseError::NoError&&json.isObject()&&json.toJson(QJsonDocument::Compact)==raw&&sqlite3_step(db.query)==SQLITE_DONE,"Retained initial receipt noncanonical or ambiguous");
    const auto receipt=json.object();const auto observation=receipt.value("observation_id").toString();
    require(receipt.value("schema")=="pgn-initial-start-receipt-1"&&receipt.value("request_id")==request&&canonicalUUID(observation),"Retained initial origin identity differs");
    // This is an immutable ORIGIN label, never a recycled snapshot. The
    // caller must acquire a NEW physical inventory and pay all current gates.
    return observation;
}
void readback(const QString& file,const QString& request,const QString& observation,const booking::InitialStartScope& scope,
              const booking::SourceRegistry& registry,const booking::InitialStartResult& result){
    require(result.dispatch.has_value()&&result.dispatch->automatic.has_value(),"Committed admission lacks a known dispatch origin");const auto& d=*result.dispatch;
    require(d.state=="PREPARED"&&d.origin=="INITIAL_START"&&!d.predecessor&&!d.run&&device(d.device)&&canonicalUUID(d.bookingId)&&decimal(d.start)&&decimal(d.end)&&source_decimal::less(d.start,d.end),"Prepared result is not an initial launch identity");
    require((*d.automatic&&result.mode==MiningSessionMode::Automatic)||(!*d.automatic&&result.mode==MiningSessionMode::ManualReservations),"Prepared result mode/origin disagreement");
    QString digest;const auto fingerprint=booking::initialStartFingerprint(request,Constant.toLower(),scope,registry,{WorkBoundary::Ready,observation},&digest);
    const QJsonObject expected{{"schema","pgn-initial-start-receipt-1"},{"origin","INITIAL_START"},{"request_id",request},{"observation_id",observation},{"device",Constant},
        {"mode",*d.automatic?"automatic":"manual-reservations"},{"booking_id",d.bookingId},{"start",d.start},{"end",d.end},{"input_fingerprint",fingerprint},
        {"scope",QJsonObject{{"execution_start",scope.executionStart},{"execution_end",scope.executionEnd},{"auto_start",scope.autoStart},{"auto_end",scope.autoEnd},{"block_anchor",scope.blockAnchor},{"block_width",scope.blockWidth}}},
        {"registry_provenance",QJsonObject{{"url",registry.url},{"sha256",registry.sha256},{"observed_utc",registry.observedUtc.toUTC().toString(Qt::ISODateWithMs)},{"semantic_digest",digest}}}};
    Database db(file);db.prepare("SELECT m.value,d.id,d.start,d.end,d.device,d.state,d.predecessor,d.run,b.id,b.start,b.end,b.device,b.state,b.kind,b.start_key,b.end_key FROM booking_metadata m JOIN dispatch_requests d ON d.id=? JOIN bookings b ON b.id=d.id WHERE m.key=?");
    db.bind(1,d.bookingId);db.bind(2,"mining-initial-start/v1/"+request);require(sqlite3_step(db.query)==SQLITE_ROW,"Committed initial command receipt is absent");
    const auto raw=text(db.query,0).toUtf8();require(raw.size()<=8192&&raw==QJsonDocument(expected).toJson(QJsonDocument::Compact),"Committed initial command receipt differs from admission");
    const auto key=[](const QString& n){return QString::number(n.size()).rightJustified(3,'0')+n;};
    const QStringList values{d.bookingId,d.start,d.end,Constant,"PREPARED"};for(int i=0;i<values.size();++i)require(text(db.query,i+1)==values[i],"Committed dispatch identity/state changed");
    require(sqlite3_column_type(db.query,6)==SQLITE_NULL&&sqlite3_column_type(db.query,7)==SQLITE_NULL,"Initial dispatch acquired a predecessor/run before submission");
    const QStringList bookingValues{d.bookingId,d.start,d.end,Constant,"DISPATCHING","LOCAL_INTENT",key(d.start),key(d.end)};
    for(int i=0;i<bookingValues.size();++i)require(text(db.query,i+8)==bookingValues[i],"Committed local intent identity/state changed");
    require(sqlite3_step(db.query)==SQLITE_DONE,"Initial command receipt readback is ambiguous");
}
}
class ApplicationMiningStart::Impl {
public:
    enum class Phase {Idle,Inspecting,Physical};
    ApplicationMiningStart& owner;QPointer<NativeBridge> bridge;QString database,runtime,request,nonce,message;Phase phase{Phase::Idle};QTimer deadline;bool resuming{};
    QString nativeReadPhaseId;int remainingBudget{};bool retainedOrigin{};
    QString checkpointLabel{QStringLiteral("C:\\PGN\\runtime\\current-job\\checkpoint")};
    Impl(ApplicationMiningStart& o,NativeBridge* b,QString db,QString directory):owner(o),bridge(b),database(std::move(db)),runtime(std::move(directory)){
        deadline.setSingleShot(true);deadline.setParent(&o);deadline.setObjectName("initial-start-deadline");
        QObject::connect(&deadline,&QTimer::timeout,&owner,[this]{
            if(phase==Phase::Idle)return;
            if(phase==Phase::Physical){
                // Submission may already have created the real worker. This
                // UI budget cannot cancel that fact or erase its correlation.
                // Native transport/freshness deadlines stay unchanged; a
                // failure, STOP, Close or owner loss still cancels normally.
                deadline.stop();message="Initial start:45s UI budget elapsed; physical outcome unconfirmed; same request retained, no retry";notify();return;
            }
            fail("Initial start held:45s deadline expired; physical outcome unconfirmed; reconcile before another start");
        });
        if(!b)return;const auto weak=QPointer<ApplicationMiningStart>(&owner);
        const auto oldPhase=b->onApplicationInventoryPhase;
        b->onApplicationInventoryPhase=[weak,oldPhase](const NativeApplicationInventoryPhase& event){if(weak)weak->d_->inventoryPhase(event);if(oldPhase)oldPhase(event);};
        const auto oldInventory=b->onApplicationStartInventory;
        b->onApplicationStartInventory=[weak,oldInventory](const QString& id,const QJsonObject& packet){
            // Admission is synchronous. Do not let an unrelated prior callback
            // re-enter Close between validation and the prepared-frame check.
            if(weak)weak->d_->inventory(id,packet);if(oldInventory)oldInventory(id,packet);
        };
        const auto oldSourceSnapshot=b->onSourceSnapshot;
        b->onSourceSnapshot=[weak,oldSourceSnapshot](const QJsonObject& packet){
            if(oldSourceSnapshot)oldSourceSnapshot(packet);if(weak)weak->d_->preserveManifest(packet);
        };
        const auto oldControl=b->onControl;
        b->onControl=[weak,oldControl](const QString& gpu,const QString& id,bool applied,const QString& detail){
            if(oldControl)oldControl(gpu,id,applied,detail);if(weak)weak->d_->observed(gpu,id,applied,detail);
        };
        const auto oldFrozen=b->onApplicationCloseFrozen;
        b->onApplicationCloseFrozen=[weak,oldFrozen](const QString& id,bool accepted,const QString& error){
            if(weak&&weak->d_->phase!=Phase::Idle)weak->d_->fail("Initial start held: application close superseded local admission; prepared intent retained");
            if(oldFrozen)oldFrozen(id,accepted,error);
        };
        QObject::connect(b,&QObject::destroyed,&owner,[this]{if(phase!=Phase::Idle)fail("Initial start held: controller owner lost; outcome unconfirmed");});
    }
    ~Impl(){if(bridge&&phase!=Phase::Idle)bridge->cancelApplicationStart(request);}
    void notify(){const auto callback=owner.onStatus;if(callback)callback(message);}
    bool resumeInventoryBudget(){
        if(!nativeReadPhaseId.isEmpty()){
            nativeReadPhaseId.clear();const int budget=std::exchange(remainingBudget,0);
            if(budget<=0)return false;deadline.start(budget);
        }
        return deadline.isActive()&&deadline.remainingTime()>0;
    }
    void inventoryPhase(const NativeApplicationInventoryPhase& event){
        if(phase!=Phase::Inspecting||event.purpose!=NativeInventoryPurpose::Start||event.requestId!=request||event.inventoryId!=nonce)return;
        if(event.phase==NativeInventoryPhaseKind::Lineage||event.phase==NativeInventoryPhaseKind::SourceRead){
            if(event.active){
                if(!nativeReadPhaseId.isEmpty()||!deadline.isActive()||deadline.remainingTime()<=0)return;
                remainingBudget=deadline.remainingTime();nativeReadPhaseId=event.phaseId;deadline.stop();
                message=event.phase==NativeInventoryPhaseKind::SourceRead?"Initial start: collecting final mining results":"Initial start: verifying saved mining progress";notify();
            }else if(nativeReadPhaseId==event.phaseId){
                if(!resumeInventoryBudget())return fail("Initial start held: UI deadline expired before native read phase");
                message="Initial start: refreshing device status";notify();
            }
        }else if(event.active&&nativeReadPhaseId.isEmpty()&&deadline.isActive()&&deadline.remainingTime()>0){
            deadline.start(45000); // A NEW native profile/snapshot operation, never a DTO-age renewal.
        }
    }
    void fail(QString detail){
        const auto id=request;phase=Phase::Idle;deadline.stop();nativeReadPhaseId.clear();remainingBudget=0;if(bridge)bridge->cancelApplicationStart(id);message=std::move(detail);const auto reported=message.left(512);
        const QPointer<ApplicationMiningStart> weak(&owner);notify();if(!weak)return;
        const auto b=bridge;if(b&&b->onControl)b->onControl(Constant,id,false,reported);
    }
    void observed(const QString& gpu,const QString& id,bool applied,const QString& detail){
        if(phase!=Phase::Physical||id!=request||gpu.compare(Constant,Qt::CaseInsensitive)!=0)return;
        phase=Phase::Idle;deadline.stop();if(bridge)bridge->cancelApplicationStart(request);
        const QString operation=resuming?"Checkpoint resume":"Initial start";
        message=applied?operation+": exact native process ownership observed":operation+" held: "+detail.left(400)+"; prepared intent retained for reconciliation";notify();
    }
    void control(const QString& gpu,const QString& id,bool enabled){
        if(!enabled&&gpu.compare(Constant,Qt::CaseInsensitive)==0&&phase!=Phase::Idle){
            const QPointer<ApplicationMiningStart> weak(&owner);fail("Initial start cancelled by explicit STOP; prepared intent, if committed, retained");if(!weak)return;
        }
        if(!bridge)return;
        if(!enabled||gpu.compare(Constant,Qt::CaseInsensitive)!=0){bridge->control(gpu,id,enabled);return;}
        if(phase!=Phase::Idle){if(id==request){message="Initial start already awaiting its correlated observation; no duplicate submitted";notify();return;}
            if(bridge->onControl)bridge->onControl(gpu,id,false,"Another explicit initial start is already pending");return;}
        request=id;nonce=uuid();resuming=false;nativeReadPhaseId.clear();remainingBudget=0;
        if(QThread::currentThread()!=owner.thread()||bridge->thread()!=owner.thread()||!canonicalUUID(id)||!QFileInfo(database).isAbsolute()||!QFileInfo(runtime).isAbsolute()||database.contains(QChar(0))||runtime.contains(QChar(0)))return fail("Initial start held: canonical request and same-thread explicit SQLite/runtime paths required");
        try{const auto previous=retainedObservation(database,request);retainedOrigin=previous.has_value();if(previous)nonce=*previous;}catch(const std::exception& e){return fail("Initial start held: "+QString::fromUtf8(e.what()).left(400));}
        phase=Phase::Inspecting;deadline.start(45000);message="Initial start: fresh physical inventory pending; no work selected";
        const QPointer<ApplicationMiningStart> weak(&owner);if(!bridge->inspectApplicationStart(request,nonce)){if(weak&&phase==Phase::Inspecting)fail("Initial start held: fresh inventory unavailable or application closing");return;}if(weak)notify();
    }
    void preserveManifest(const QJsonObject& s){
        // This is source provenance, never a sample refresh or launch token.
        // Missing physical rows during first publication are retried only on
        // the next genuine snapshot; raw ingestion and metrics are untouched.
        if(s.value("schema")!="pgn-runtime-snapshot-1"||!flag(s,"driver_valid",true)||!utc(s.value("observed_utc"))||!s.value("owned").isBool()||
            !s.value("manifest").isObject()||!s.value("binding").isObject())return;
        const auto m=s.value("manifest").toObject(),b=s.value("binding").toObject();
        if(!device(m.value("uuid"))||m.value("uuid")!=b.value("uuid")||m.value("start")!=b.value("start")||m.value("end")!=b.value("end")||m.value("queued_intent")!=b.value("queued_intent"))return;
        if(s.value("owned").toBool()){
            if(s.value("state")!="RUNNING"&&s.value("state")!="STOPPING")return;
        }else{
            const auto exit=s.value("last_exit").toObject();
            if(s.value("state")!="OFF"||exit.value("run")!=m.value("run")||!exit.value("exit_code").isDouble()||exit.value("exit_code").toDouble()!=0)return;
        }
        booking::BookingStore store(database);if(store.valid())store.saveSourceRunManifest(Constant,m);
    }
    void resumeCheckpoint(const QJsonObject& s,const QJsonObject& envelope,const QJsonObject& manifest){
        // Capability is producer code protocol, not generic GPU compatibility.
        // Old controllers MUST hold before any durable PREPARED epoch exists.
        require(s.value("checkpoint_resume_protocol")=="pgn-checkpoint-resume-admission-1","ResumeRequired: controller lacks qualified checkpoint-resume protocol; no new epoch allocated");
        const auto oldRun=manifest.value("run").toString();
        conserved(s,envelope.value("source_conservation").toObject(),oldRun);
        const auto packet=decodeSourceStopCheckpointPacket(s,oldRun,Constant,checkpointLabel,inspectRunSourceProfile(s.value("manifest").toObject()).engineJob);
        require(packet.evidence.has_value(),"ResumeRequired: fresh stopped checkpoint capture unavailable");
        const auto checked=validateSourceStopCheckpoint(*packet.evidence);
        require(checked.status==SourceStopCheckpointStatus::Ready&&checked.resume.has_value(),"ResumeRequired: fresh partial checkpoint/source footer differs");
        const auto intent=queued(manifest);require(intent.has_value()&&canonicalUUID(*intent),"ResumeRequired: exact original local booking identity unavailable");
        booking::BookingStore store(database);require(store.valid(),"ResumeRequired: same application SQLite unavailable");
        require(store.saveSourceRunManifest(Constant,manifest).isEmpty(),"ResumeRequired: exact original source manifest unavailable/corrupt");
        if(manifest.contains("resume_previous_run")||manifest.contains("checkpoint_input_sha256")){
            require(sourceResumeLineageMatches(s,"stopped"),("ResumeRequired: all previous stopped raw sources/counter deltas must be conserved; "+s.value("resume_lineage").toObject().value("error").toString().left(300)).toUtf8().constData());
        }
        require(store.saveResumeCheckpoint(Constant,*checked.resume,packet.evidence->checkpointBytes).isEmpty(),"ResumeRequired: exact saved checkpoint commit failed");
        const auto retained=retainedResume(database,request);
        const auto next=retained?retained->run:uuid().remove('-');
        const auto prepared=store.prepareCheckpointResume(request,Constant,*intent,oldRun,next,checked.resume->checkpointSha256,nonce);
        if((prepared.status!=booking::CheckpointResumeAdmissionStatus::Prepared&&!(retained&&prepared.status==booking::CheckpointResumeAdmissionStatus::ReconcilePrepared&&prepared.replayed))||!prepared.epoch)return fail("ResumeRequired: "+prepared.error.left(400)+"; existing prepared identity must reconcile, never relaunch");
        const auto read=store.savedCheckpointResumeEpoch(Constant,next);
        require(read.status==booking::CheckpointResumeReadStatus::Present&&read.epoch&&read.receiptUtf8==prepared.receiptUtf8&&
            read.epoch->requestUUID==request&&read.epoch->observationUUID==nonce&&read.epoch->previousRun==oldRun&&read.epoch->run==next&&
            read.epoch->checkpoint.checkpointBytes==packet.evidence->checkpointBytes,"ResumeRequired: committed checkpoint lineage readback differs");
        const QJsonObject base{{"base_run",oldRun},{"base_birth",manifest.value("birth")},{"base_start",manifest.value("start")},{"base_end",manifest.value("end")}};
        resuming=true;phase=Phase::Physical;message="Checkpoint resume: exact saved epoch committed; awaiting new process ownership (ACK is insufficient)";
        const QPointer<ApplicationMiningStart> weak(&owner);
        if(!bridge||!bridge->controlPreparedResume(Constant,request,nonce,base,*intent,next,checked.resume->checkpointSha256)){
            if(weak&&phase==Phase::Physical)fail("Checkpoint resume held: submit outcome unconfirmed; prepared lineage retained");return;
        }
        if(weak)notify();
    }
    void inventory(const QString& id,const QJsonObject& envelope){
        if(phase!=Phase::Inspecting||id!=request||envelope.value("request_id")!=request||envelope.value("inventory_id")!=nonce)return;
        if(!resumeInventoryBudget())return fail("Initial start held: UI deadline expired; inventory cannot resurrect it");
        try{
            require(envelope.value("type")=="application_start_inventory"&&flag(envelope,"accepted",true)&&envelope.value("snapshot").isObject(),"Fresh initial inventory is unknown/refused");
            const auto s=envelope.value("snapshot").toObject();require(s.value("binding").isObject(),"Initial binding is absent");const auto binding=s.value("binding").toObject();
            require(s.value("schema")=="pgn-runtime-snapshot-1"&&flag(s,"driver_valid",true)&&utc(s.value("observed_utc"))&&device(binding.value("uuid"))&&
                s.value("owned").isBool()&&s.value("operator_stop").isBool()&&s.value("has_checkpoint").isBool(),"Initial physical identity or typed owner/checkpoint/STOP is unknown");
            require(decimal(binding.value("start"))&&decimal(binding.value("end"))&&source_decimal::less(binding.value("start").toString(),binding.value("end").toString()),"Initial configured binding range is invalid");
            const auto value=s.value("manifest");require(value.isNull()||value.isObject(),"Manifest absence must be explicit null");QJsonObject manifest;
            if(value.isObject()){
                manifest=value.toObject();const auto pid=offset(manifest.value("pid"));
                require(manifest.value("schema")=="pgn-native-process-1"&&run(manifest.value("run"))&&utc(manifest.value("birth"))&&pid&&*pid>0&&device(manifest.value("uuid"))&&
                    manifest.value("uuid")==binding.value("uuid")&&manifest.value("start")==binding.value("start")&&manifest.value("end")==binding.value("end"),"Initial manifest does not exactly match current binding/device/epoch");
                require(queued(manifest)==queued(binding),"Initial manifest queued identity differs from binding");
            }
            if(flag(s,"owned",true)){
                require(!manifest.isEmpty()&&s.value("state")=="RUNNING","Owned initial epoch is not exactly RUNNING");
                phase=Phase::Idle;deadline.stop();bridge->cancelApplicationStart(request);message="Initial start: exact native process already running observed; no start submitted";
                const auto completedId=request,reported=message;const QPointer<ApplicationMiningStart> weak(&owner);notify();if(weak&&bridge&&bridge->onControl)bridge->onControl(Constant,completedId,true,reported);return;
            }
            require(s.value("state")=="OFF","Initial process state is unknown/interrupted");
            if(s.value("has_checkpoint").toBool()){
                require(!manifest.isEmpty(),"ResumeRequired: checkpoint has no exact original manifest");
                return resumeCheckpoint(s,envelope,manifest);
            }
            if(manifest.isEmpty())require(s.value("last_exit").isNull(),"Empty initial epoch requires explicit absent last_exit");
            else{
                sourceReceipt(s,manifest,binding);conserved(s,envelope.value("source_conservation").toObject(),manifest.value("run").toString());
            }
            require(binding.value("test_scope").isObject(),"Explicit initial execution scope unavailable");const auto scopeValue=binding.value("test_scope").toObject();
            require(scopeValue.value("order")=="ascending"&&decimal(scopeValue.value("start"))&&decimal(scopeValue.value("end"))&&source_decimal::less(scopeValue.value("start").toString(),scopeValue.value("end").toString()),"Initial test scope/order is not exact ascending authority");
            const auto first=scopeValue.value("start").toString(),last=scopeValue.value("end").toString();
            require(!source_decimal::less(binding.value("start").toString(),first)&&!source_decimal::less(last,binding.value("end").toString()),"Configured binding lies outside its explicit execution scope");
            const booking::InitialStartScope scope{first,last,first,last,first,"1000000000000000"};
            const auto registry=booking::loadSourceRegistry(runtime);require(registry.valid(),"Fresh local source registry/provenance unavailable");
            booking::BookingStore store(database);require(store.valid(),"Same application booking SQLite unavailable");
            if(!manifest.isEmpty()){
                const auto error=store.recordQueuedSourceCompletion(manifest.value("run").toString(),manifest.value("start").toString(),manifest.value("end").toString(),Constant,queued(manifest));
                require(error.isEmpty(),"Exact SOURCE/local intent/dispatch SQLite completion commit failed");
            }
            auto prepared=store.prepareInitialStart(request,Constant,scope,registry,{WorkBoundary::Ready,nonce});
            if(retainedOrigin&&prepared.status==booking::InitialStartStatus::ReconcilePrepared&&prepared.replayed){
                // A retained intent alone is NEVER permission. This branch is
                // reached only after NEW exact OFF/base/RAW/profile acquisition;
                // readback must still be exact PREPARED+NULL, same range, scope,
                // registry and immutable request. Controller checks the fresh
                // observed base again and cannot schedule a second owned epoch.
                std::optional<booking::InitialStartObservedBase> old;
                if(!manifest.isEmpty())old=booking::InitialStartObservedBase{manifest.value("run").toString(),manifest.value("birth").toString(),manifest.value("start").toString(),manifest.value("end").toString()};
                const auto saved=store.savedInitialStartAdmission(request,Constant,nonce,old);
                if(saved.status!=booking::InitialStartAdmissionReadStatus::Present)return fail("Initial reconciliation held: "+saved.error.left(320));
                prepared=saved.admission;
            }
            if(prepared.status!=booking::InitialStartStatus::Prepared){
                const auto reason=prepared.status==booking::InitialStartStatus::ReconcilePrepared?"existing PREPARED requires reconciliation, never another launch":prepared.status==booking::InitialStartStatus::ResumeRequired?"ResumeRequired: existing interrupted provenance retained":prepared.error;
                return fail("Initial start held: "+reason.left(400));
            }
            readback(database,request,nonce,scope,registry,prepared);
            QJsonObject base{{"base_run",QJsonValue(QJsonValue::Null)},{"base_birth",QJsonValue(QJsonValue::Null)},{"base_start",QJsonValue(QJsonValue::Null)},{"base_end",QJsonValue(QJsonValue::Null)}};
            if(!manifest.isEmpty()){base.insert("base_run",manifest.value("run"));base.insert("base_birth",manifest.value("birth"));base.insert("base_start",manifest.value("start"));base.insert("base_end",manifest.value("end"));}
            phase=Phase::Physical;message="Initial start: native PREPARED committed; awaiting exact physical ownership (command ACK is insufficient)";
            const auto d=*prepared.dispatch;const QPointer<ApplicationMiningStart> weak(&owner);
            if(!bridge||!bridge->controlPreparedInitial(Constant,request,nonce,base,d.bookingId,d.start,d.end)){
                if(weak&&phase==Phase::Physical)fail("Initial start held: prepared command submission unconfirmed; reconcile retained identity");return;
            }if(weak&&phase==Phase::Physical)notify();
        }catch(const std::exception& e){fail("Initial start held: "+QString::fromUtf8(e.what()).left(400));}
    }
};
ApplicationMiningStart::ApplicationMiningStart(NativeBridge* b,QString db,QString runtime,QObject* parent,QString label)
    :QObject(parent),d_(std::make_unique<Impl>(*this,b,std::move(db),std::move(runtime))){if(!label.isEmpty())d_->checkpointLabel=std::move(label);}
ApplicationMiningStart::~ApplicationMiningStart()=default;
void ApplicationMiningStart::control(const QString& device,const QString& request,bool enabled){d_->control(device,request,enabled);}
QString ApplicationMiningStart::status()const{return d_->message;}
}

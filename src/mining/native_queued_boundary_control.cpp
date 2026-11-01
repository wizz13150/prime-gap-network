#include "mining/native_queued_boundary_control.h"
#include "mining/completion_advance_gate.h"
#include "mining/source_decimal.h"
#include "mining/source_queued_identity.h"
#include "mining/staged_successor.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>
#include <QUuid>
#include <limits>
#include <stdexcept>

namespace pgn::mining {
namespace {
const QString Device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
constexpr qint64 Safe=9007199254740991LL;
void need(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
bool uuid(const QString& s){const auto u=QUuid::fromString(s);return !u.isNull()&&u.toString(QUuid::WithoutBraces)==s;}
bool run(const QJsonValue& v){if(!v.isString()||v.toString().size()!=32)return false;for(auto c:v.toString())if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
bool flag(const QJsonObject& o,const char* k,bool expected){const auto v=o.value(QLatin1String(k));return v.isBool()&&v.toBool()==expected;}
QString text(const QJsonObject& o,const char* k){const auto v=o.value(QLatin1String(k));need(v.isString()&&!v.toString().contains(QChar(0))&&v.toString().toUtf8().size()<=16384&&QString::fromUtf8(v.toString().toUtf8())==v.toString(),"Boundary exact text missing or invalid");return v.toString();}
bool decimal(const QJsonValue& v){return v.isString()&&v.toString().size()<=160&&source_decimal::canonical(v.toString());}
QString utc(const QJsonValue& v){
    need(v.isString(),"Boundary UTC must be exact text");
    static const QRegularExpression p("^(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2})(?:\\.(\\d{1,9}))?Z$");
    const auto m=p.match(v.toString());need(m.hasMatch()&&m.capturedLength()==v.toString().size(),"Boundary UTC full match required");
    const auto d=QDateTime::fromString(m.captured(1)+'Z',Qt::ISODate);
    need(d.isValid()&&d.toUTC().toString("yyyy-MM-dd'T'HH:mm:ss")==m.captured(1),"Boundary UTC invalid date");
    return m.captured(1)+'.'+m.captured(2).leftJustified(9,'0')+'Z';
}
qint64 number(const QJsonValue& v){need(v.isDouble(),"Boundary byte offset must be typed safe integer");const auto n=v.toInteger(-1);need(n>=0&&n<=Safe&&v.toDouble()==double(n),"Boundary byte offset outside exact safe integer domain");return n;}
void conservation(const SshObservationFrame& f,const QJsonObject& c,const QString& epoch){
    const auto& s=f.packet;
    need(c.value("schema")=="pgn-source-conservation-1"&&flag(c,"ready",true)&&c.value("run")==epoch&&c.value("source")=="native/"+epoch+".stdout"&&c.value("scope")=="LOGICAL_SOURCE_EPOCH_CONSERVATION_NOT_GLOBAL_POWER_CUT_DURABILITY","Boundary exact source conservation unavailable");
    const auto next=number(s.value("next_offset")),pending=number(s.value("pending_bytes"));
    need(next<=Safe-pending,"Boundary byte end overflow");const auto end=next+pending;
    need(f.nextOffset==next&&f.pendingBytes&&*f.pendingBytes==pending&&number(c.value("required_end"))==end&&number(c.value("committed_offset"))>=end&&number(c.value("mirror_size"))>=end&&number(c.value("transport_offset"))>=end,"Boundary conservation does not cover exact final byte end");
}
void scopeMatches(const QJsonObject& b,const booking::InitialStartScope& scope,const booking::SourceRegistry& r){
    const auto t=b.value("test_scope").toObject();
    // The actual legacy ROOT379 scope has no enabled member. Absence is not
    // a disabled scope; a PRESENT member must remain the exact bool true.
    need((!t.contains("enabled")||flag(t,"enabled",true))&&t.value("order")=="ascending"&&decimal(t.value("start"))&&decimal(t.value("end")),"Boundary explicit ascending test scope unavailable");
    const auto a=t.value("start").toString(),z=t.value("end").toString();
    need(source_decimal::less(a,z)&&scope.executionStart==a&&scope.executionEnd==z&&scope.autoStart==a&&scope.autoEnd==z&&scope.blockAnchor==a&&scope.blockWidth=="1000000000000000","Boundary scope differs from actual Zone1000 contract");
    need(r.valid(),"Boundary current source registry unavailable");int matches=0;
    // SourceRange.row is the CSV LINE (header included), not display Zone.
    // Bind authority to the exact qualified scope interval, never that ordinal.
    for(const auto& row:r.ranges)if(row.start==a&&row.end==z){++matches;need(row.status=="Open"&&row.owner.isEmpty(),"Boundary Zone1000 differs from current registry");}
    need(matches==1&&!source_decimal::less(b.value("start").toString(),a)&&!source_decimal::less(z,b.value("end").toString()),"Boundary configured binding outside unique Zone1000");
}
QJsonObject sourceReceipt(const QJsonObject& s,const QJsonObject& m,const QJsonObject& b){
    const auto epoch=m.value("run").toString();const auto e=s.value("last_exit").toObject();
    need(e.value("run")==epoch&&e.value("exit_code").isDouble()&&e.value("exit_code").toDouble()==0,"Boundary exact current successful last_exit unavailable");
    const auto exitKey=utc(e.value("utc"));need(utc(m.value("birth"))<=exitKey&&exitKey<=utc(s.value("observed_utc")),"Boundary SOURCE exit lies outside current observed epoch");
    QJsonObject selected;std::optional<QJsonObject> semantics;
    for(const auto* key:{"completion","source_completion_observation","completion_candidate"}){
        const auto value=s.value(QLatin1String(key));if(value.isUndefined()||value.isNull())continue;
        need(value.isObject(),"Boundary SOURCE receipt is malformed");const auto receipt=value.toObject();
        need(run(receipt.value("run")),"Boundary SOURCE receipt run malformed");if(receipt.value("run")!=epoch)continue; // retained history, never relabelled
        auto view=s;view["completion"]=receipt;
        need(completeSourceMatches(view,epoch)&&flag(receipt,"independent_coverage",false)&&receipt.value("proof_status")=="SOURCE_PRP_INDEPENDENT_PROOF_PENDING","Boundary current SOURCE quantity/scope/lineage is incomplete");
        need(receipt.value("source_exit_utc")==e.value("utc")&&receipt.value("source")==m.value("stdout")&&decimal(receipt.value("source_tests"))&&decimal(receipt.value("source_session_gaps")),"Boundary SOURCE counters/exit/source differ");
        const auto q=source_queued_identity::queued(m);
        need((stagedSuccessor(s)||q==source_queued_identity::queued(b))&&q==source_queued_identity::queued(receipt)&&(!q||uuid(*q)),"Boundary SOURCE queued intent differs");
        if(QString::fromLatin1(key)=="source_completion_observation")need(receipt.value("evidence_scope")=="VERIFIED_SNAPSHOT_SOURCE_COMPLETION"&&receipt.value("source_observed_operator_stop").isBool()&&receipt.value("source_observed_operator_stop")==s.value("operator_stop")&&receipt.value("source_observed_stop_file").isBool(),"Boundary SOURCE-only observed STOP/scope is unknown");
        QJsonObject tuple;for(const auto* field:{"schema","run","start","end","source_range_complete","source_positions","source_tests","source_session_gaps","source_exit_code","source_exit_utc","source","queued_intent","proof_status","independent_coverage","source_position_evidence","source_periodic_positions","source_tests_complete"})tuple[QLatin1String(field)]=receipt.value(QLatin1String(field));
        need(!semantics||*semantics==tuple,"Boundary contradictory current SOURCE receipts");semantics=tuple;if(selected.isEmpty())selected=receipt;
    }
    need(!selected.isEmpty(),"Boundary exact current SOURCE receipt unavailable");return selected;
}
struct Admission {
    booking::NextBoundarySource base;
    QJsonObject receipt,manifest,binding;
    booking::InitialStartScope scope;
    booking::SourceRegistry registry;
    QElapsedTimer age;
    quint64 generation{};
    bool automaticAllowed{};
    bool staged{};
    bool reconcileUnsent{};
};
struct Outcome {QJsonObject packet;std::optional<QJsonObject> source;};
}
QJsonObject verifiedSourceCompletionAtBoundary(const SshObservationFrame& frame,const QJsonObject& conserved){
    need(flag(frame.packet,"owned",false)&&frame.packet.value("state")=="OFF","Completed source requires actual OFF observation");
    const auto manifest=frame.packet.value("manifest").toObject();
    conservation(frame,conserved,manifest.value("run").toString());
    return sourceReceipt(frame.packet,manifest,frame.packet.value("binding").toObject());
}
NativeQueuedBoundaryControl::NativeQueuedBoundaryControl(NativeApplicationWriter& w,booking::BookingStore& s,SshObservationEndpoint e,NativeControlTimeouts t,int age,QObject* p)
    :QObject(p),writer_(w),store_(s),endpoint_(std::move(e)),timeouts_(t),maximumAge_(age){
    owner();need(w.thread()==thread()&&s.valid(),"Boundary requires ONE owning writer and SAME valid SQLite");need(age>0&&age<=30000,"Boundary bounded monotonic TTL required");
    need(t.startupMs>0&&t.startupMs<=NativeControlTransport::MaximumStartupMs&&t.deadlineMs>=t.startupMs&&t.deadlineMs<=NativeControlTransport::MaximumDeadlineMs,"Boundary transport timeouts invalid");
    makeSshObservationCommand(endpoint_,{});
}
NativeQueuedBoundaryControl::~NativeQueuedBoundaryControl(){owner();if(pendingDone_)pendingDone_("Boundary owner destroyed; reconcile retained SQLite and remote outcome");}
void NativeQueuedBoundaryControl::owner()const{need(QThread::currentThread()==thread(),"Boundary requires creating thread");}
void NativeQueuedBoundaryControl::publish(const QJsonObject& p){if(onPacket)onPacket(p);}
void NativeQueuedBoundaryControl::setDesiredPaused(bool value){owner();if(value!=paused_){need(generation_!=std::numeric_limits<quint64>::max(),"Boundary pause generation exhausted");++generation_;paused_=value;}}
bool NativeQueuedBoundaryControl::submit(const QString& request,const QString& observation,const SshObservationFrame& f,const QJsonObject& c,const booking::InitialStartScope& scope,const booking::SourceRegistry& registry){
    owner();QJsonObject packet{{"type","queued_boundary"},{"request_id",request.left(128)},{"observation_id",observation.left(128)},{"device",Device},{"accepted",false},{"applied",false},{"transport_attempted",false},{"status","Held"}};
    try{
        Admission a;a.age.start();a.generation=generation_;
        need(!busy_&&uuid(request)&&uuid(observation)&&!consumedRequests_.contains(request)&&!consumedObservations_.contains(observation),"Boundary busy or correlation consumed");
        need(QJsonDocument(f.packet).toJson(QJsonDocument::Compact).size()<=SshSnapshotTransport::FrameLimit&&QJsonDocument(c).toJson(QJsonDocument::Compact).size()<=4096,"Boundary observation/conservation exceeds bounded acquisition ABI");
        const auto& s=f.packet;const auto m=s.value("manifest").toObject(),b=s.value("binding").toObject();
        // has_checkpoint is the actual PS snapshot boolean supplied to
        // Get-PgnSourceCompletion, which refuses checkpointed completion.
        // A typed STOP may conserve a full SOURCE fact but never allow next.
        need(s.value("schema")=="pgn-runtime-snapshot-1"&&s.value("state")=="OFF"&&flag(s,"owned",false)&&s.value("operator_stop").isBool()&&flag(s,"driver_valid",true)&&flag(s,"has_checkpoint",false),"Boundary requires explicit current OFF/not-owned/known-STOP/no-checkpoint");
        a.staged=stagedSuccessor(s);
        need(m.value("schema")=="pgn-native-process-1"&&run(m.value("run"))&&m.value("uuid")==Device&&b.value("uuid")==Device&&decimal(m.value("start"))&&decimal(m.value("end"))&&source_decimal::less(m.value("start").toString(),m.value("end").toString())&&(a.staged||(m.value("start")==b.value("start")&&m.value("end")==b.value("end"))),"Boundary manifest/binding identity differs");
        const auto pid=number(m.value("pid"));need(pid>0&&pid<=4294967295LL,"Boundary manifest PID must be positive uint32");
        a.base={text(m,"run"),text(m,"birth"),text(m,"start"),text(m,"end"),source_queued_identity::queued(m)};
        need(!text(m,"stdout").isEmpty(),"Boundary source unavailable");
        a.receipt=sourceReceipt(s,m,b);conservation(f,c,a.base.run);a.manifest=m;a.binding=b;a.scope=scope;a.registry=registry;a.automaticAllowed=flag(s,"operator_stop",false);
        a.reconcileUnsent=unsentPreparedRuns_.contains(a.base.run);
        consumedRequests_.insert(request);consumedObservations_.insert(observation);busy_=true;
        auto outcome=std::make_shared<Outcome>();outcome->packet=packet;outcome->packet["run"]=a.base.run;const QPointer<NativeQueuedBoundaryControl> weak(this);
        const auto settle=[weak,outcome](const NativeApplicationWriteResult& result){
            if(!weak)return;weak->pendingDone_={};weak->busy_=false;
            if(!result.error.isEmpty()){outcome->packet["accepted"]=false;outcome->packet["status"]="Held";outcome->packet["error"]=result.error.left(400);outcome->packet.remove("result");}
            weak->publish(outcome->packet);
        };
        const auto advance=[weak,a,observation,outcome,settle]{
            if(!weak)return;
            const bool queued=weak->writer_.enqueue(NativeApplicationOperation::Mutation,[weak,a,observation,outcome](auto done){
            if(!weak){done("Boundary owner vanished before entry");return;}weak->pendingDone_=done;
            const auto allowed=[&]{need(a.automaticAllowed&&!weak->paused_&&!weak->writer_.frozen()&&weak->generation_==a.generation,"Boundary STOP/local pause/close invalidated future admission");need(a.age.elapsed()<=weak->maximumAge_,"Boundary monotonic observation expired; reacquire and reconcile");};
            allowed();need(!weak->consumedRuns_.contains(a.base.run)||a.reconcileUnsent,"Boundary predecessor already prepared/submitted; actual reconciliation required");
            // Registry/scope failure cannot erase the separately committed
            // SOURCE fact. Optional enabled preserves the actual legacy ABI.
            scopeMatches(a.binding,a.scope,a.registry);allowed();
            if(a.staged){
                const auto retained=weak->store_.savedPreparedNextAdmission(Device,a.binding.value("queued_intent").toString(),a.base);
                need(retained.status==booking::PreparedNextReadStatus::Present&&retained.admission.dispatch.has_value(),"Staged reconciliation requires existing SAME SQLite PREPARED; never allocate");
                const auto& d=*retained.admission.dispatch;
                need(d.start==a.binding.value("start")&&d.end==a.binding.value("end")&&d.state=="PREPARED"&&!d.run&&d.predecessor&&*d.predecessor==a.base.run,"Staged reconciliation tuple differs from retained PREPARED");
            }
            const auto next=weak->store_.prepareNextAtBoundary(Device,a.base,a.scope,a.registry,{WorkBoundary::Ready,observation});
            if(next.status==booking::NextBoundaryStatus::Prepared||next.status==booking::NextBoundaryStatus::ReconcilePrepared)weak->consumedRuns_.insert(a.base.run);
            if(next.status==booking::NextBoundaryStatus::EndSession||next.status==booking::NextBoundaryStatus::Exhausted){
                outcome->packet["status"]=next.status==booking::NextBoundaryStatus::EndSession?"EndSession":"Exhausted";outcome->packet["accepted"]=true;
                outcome->packet["detail"]=next.status==booking::NextBoundaryStatus::EndSession?"Manual reservations complete; automatic continuation OFF":next.error;weak->pendingDone_={};done({});return;
            }
            need(next.status!=booking::NextBoundaryStatus::ReconcilePrepared||a.staged||a.reconcileUnsent,"Existing PREPARED requires actual reconciliation, not retry permission");
            need((next.status==booking::NextBoundaryStatus::Prepared||next.status==booking::NextBoundaryStatus::ReconcilePrepared)&&next.dispatch.has_value(),"Boundary next preparation held; committed SOURCE retained");
            const auto& selected=*next.dispatch;outcome->packet["booking_id"]=selected.bookingId;outcome->packet["reconcile_required"]=true;
            if(a.staged)need(next.status==booking::NextBoundaryStatus::ReconcilePrepared&&a.binding.value("queued_intent")==selected.bookingId&&a.binding.value("start")==selected.start&&a.binding.value("end")==selected.end,"Staged successor must match SAME existing PREPARED dispatch exactly");
            const auto saved=weak->store_.savedPreparedNextAdmission(Device,selected.bookingId,a.base);
            need(saved.status==booking::PreparedNextReadStatus::Present&&saved.admission.status==booking::NextBoundaryStatus::Prepared&&saved.admission.dispatch.has_value(),"Boundary SAME SQLite prepared readback held");
            const auto& d=*saved.admission.dispatch;
            need(d.bookingId==selected.bookingId&&d.start==selected.start&&d.end==selected.end&&d.device==Device&&d.origin=="QUEUED_ADVANCE"&&d.state=="PREPARED"&&!d.run&&d.predecessor&&*d.predecessor==a.base.run&&d.automatic==selected.automatic&&saved.admission.mode==next.mode,"Boundary prepared tuple changed before encoding");
            const auto encoded=NativeControlCommand::advanceQueued(weak->endpoint_,{Device,d.bookingId,d.start,d.end,a.base.run,a.base.start,a.base.end,a.base.queuedIntent});
            // SQL/readback/encoding never renew the acquisition age. Keep this
            // check immediately adjacent to the sole transport submission.
            if(a.age.elapsed()>weak->maximumAge_){
                weak->unsentPreparedRuns_.insert(a.base.run);
                outcome->packet["reacquire_prepared_unsent"]=true;
                need(false,"Boundary observation expired during next SQL/readback/encode; PREPARED retained unsent");
            }
            allowed();auto transport=std::make_unique<NativeControlTransport>(weak);auto* raw=transport.get();outcome->packet["transport_attempted"]=true;
            weak->unsentPreparedRuns_.remove(a.base.run);
            raw->start(encoded,weak->timeouts_,[weak,raw,outcome,done](const auto& result){
                raw->deleteLater();if(!weak){done("Boundary owner lost; remote outcome unknown");return;}
                const bool reported=result.status==NativeControlTransportStatus::CompleteResponse&&result.control.outcome==NativeControlOutcome::RequestReported;
                outcome->packet["accepted"]=reported;outcome->packet["status"]=reported?"RequestReported":"Held";
                if(reported)outcome->packet["result"]=result.control.packet;
                else outcome->packet["error"]=(!result.error.isEmpty()?result.error:!result.control.error.isEmpty()?result.control.error:QString("Queued transport uncertain; reconcile, never blind retry")).left(400);
                weak->pendingDone_={};done({});
            });transport.release();
            },settle);
            if(!queued)settle({false,"Boundary writer freeze/overload refused next; committed SOURCE retained"});
        };
        // Observational commit is deliberately Read: freeze/desired pause
        // blocks advancement, NOT preservation of an already validated fact.
        const bool queued=writer_.enqueue(NativeApplicationOperation::Read,[weak,a,outcome](auto done){
            if(!weak){done("Boundary owner vanished before SOURCE commit");return;}weak->pendingDone_=done;
            const auto old=weak->store_.savedSourceRunManifest(Device,a.base.run);
            need(old.status==booking::SourceRunManifestReadStatus::Present&&old.manifest==a.manifest,"Boundary immutable actual manifest unavailable or changed");
            const auto error=weak->store_.recordQueuedSourceCompletion(a.base.run,a.base.start,a.base.end,Device,a.base.queuedIntent);
            need(error.isEmpty(),"Boundary SOURCE/local/dispatch transaction failed; no next claim");
            auto receipt=a.receipt;receipt["evidence_scope"]="VERIFIED_SNAPSHOT_SOURCE_COMPLETION";receipt["community_updated"]=false;
            outcome->source=QJsonObject{{"type","source_completion"},{"receipt",receipt}};weak->pendingDone_={};done({});
        },[weak,outcome,advance,settle](const auto& result){
            if(!weak)return;weak->pendingDone_={};
            if(outcome->source){
                try{weak->publish(*outcome->source);}
                catch(...){if(weak)settle({true,"Boundary SOURCE notification failed; committed fact retained, future admission held"});return;}
                if(!weak)return;
            }
            if(!result.error.isEmpty()){settle(result);return;}advance();
        });
        if(!queued){busy_=false;need(false,"Boundary writer refused admission; no retry permission");}return true;
    }catch(const std::exception& e){packet["error"]=QString::fromUtf8(e.what()).left(400);publish(packet);return false;}
}
}

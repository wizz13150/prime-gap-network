#include "mining/completion_advance_gate.h"
#include "mining/run_source_profile.h"
#include "mining/source_decimal.h"
#include "mining/native_scientific_context.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QUuid>
#include <limits>
#include <stdexcept>

namespace pgn::mining {
namespace {
bool zero(const QJsonValue& value){return value.isDouble()&&value.toDouble()==0;}
bool exactDecimal(const QJsonValue& value){
    if(!value.isString())return false;const auto s=value.toString();
    return s.size()<=160&&source_decimal::canonical(s);
}
bool runIdentity(const QString& run){
    if(run.size()!=32)return false;
    for(const auto c:run)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;
}
std::optional<QString> utcOrderKey(const QJsonValue& value){
    if(!value.isString())return {};
    static const QRegularExpression pattern("^(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2})(?:\\.(\\d{1,9}))?Z$");
    const auto match=pattern.match(value.toString());if(!match.hasMatch()||match.capturedLength()!=value.toString().size())return {};
    const auto seconds=match.captured(1);const auto date=QDateTime::fromString(seconds+'Z',Qt::ISODate);
    if(!date.isValid()||date.toUTC().toString("yyyy-MM-dd'T'HH:mm:ss")!=seconds)return {};
    return seconds+'.'+match.captured(2).leftJustified(9,'0')+'Z';
}
}
bool sourceLineageMatches(const QJsonObject& packet,const QString& terminal){
    const auto manifest=packet.value("manifest").toObject();
    const auto run=manifest.value("run").toString();
    if((terminal!="complete"&&terminal!="stopped")||!runIdentity(run)||!exactDecimal(manifest.value("start"))||!exactDecimal(manifest.value("end"))||
        !source_decimal::less(manifest.value("start").toString(),manifest.value("end").toString()))return false;
        // A footer is NOT conserved RAW evidence, even without a resume.
        // Require the exact peer-verifier ABI for ALL operational completions.
        // This is logical source conservation, never image/prime authority.
        const auto proof=packet.value("resume_lineage").toObject();const auto epochs=proof.value("epochs");
        const auto hash=proof.value("chain_sha256");static const QRegularExpression sha("^[0-9a-f]{64}$");
        const auto booking=proof.value("booking_id").toString();const auto bookingUUID=QUuid::fromString(booking);
         bool nativeOrigin=false;
         try{nativeOrigin=manifest.value("queued_intent").isNull()&&nativeScientificSearchOrigin(manifest);}catch(...){return false;}
         const bool identityMatches=nativeOrigin?
             proof.value("booking_id").isNull()&&proof.value("source_origin")=="native_search"&&epochs.toInteger()==1:
             proof.value("booking_id")==manifest.value("queued_intent")&&proof.value("booking_id").isString()&&!bookingUUID.isNull()&&bookingUUID.toString(QUuid::WithoutBraces)==booking&&manifest.value("queued_intent").isString();
        if(!proof.value("ready").isBool()||!proof.value("ready").toBool()||proof.value("schema")!="pgn-source-resume-lineage-1"||proof.value("terminal")!=terminal||
            !proof.value("error").isString()||!proof.value("error").toString().isEmpty()||
            proof.value("scope")!="LOCAL_RAW_LINEAGE_COUNTER_DELTAS_NOT_INDEPENDENT_PRIME_COVERAGE"||proof.value("run")!=run||
            !identityMatches||
            proof.value("start")!=manifest.value("start")||proof.value("end")!=manifest.value("end")||!epochs.isDouble()||epochs.toInteger()<1||
            epochs.toInteger()>9007199254740991LL||epochs.toDouble()!=double(epochs.toInteger())||!hash.isString()||hash.toString().size()!=64||!sha.match(hash.toString()).hasMatch()||
            (!nativeOrigin&&!manifest.value("queued_intent").isString()))return false;
        // Optional grammar evidence, NOT image attestation. Never invent a
        // legacy default; partial/unknown v2 metadata cannot downgrade itself.
        if(proof.contains("source_boundary_protocol")||proof.contains("source_boundary_groups")){
            const auto groups=proof.value("source_boundary_groups");bool ok=false;
            const auto count=groups.toString().toULongLong(&ok);
            const auto protocol=proof.value("source_boundary_protocol").toString();
            if((protocol!="pgn-source-boundary-v2"&&protocol!="PGN_NATIVE_GAP_RUNTIME_V1")||!groups.isString()||
                groups.toString().size()>20||!source_decimal::canonical(groups.toString())||!ok||
                (terminal=="complete"&&count==0))return false;
        }
    // Check the ORIGINAL run manifest as well as the peer DTO. A grammar-only
    // v2 result, malformed present profile or current binding cannot license it.
    return runSourceProtocolAdmitted(inspectRunSourceProfile(manifest),proof.value("source_boundary_protocol").toString());
}
bool sourceResumeLineageMatches(const QJsonObject& packet,const QString& terminal){
    if(!sourceLineageMatches(packet,terminal))return false;
    const auto manifest=packet.value("manifest").toObject(),proof=packet.value("resume_lineage").toObject();
    const auto previous=manifest.value("resume_previous_run"),input=manifest.value("checkpoint_input_sha256");
    static const QRegularExpression sha("^[0-9a-f]{64}$");
    return previous.isString()&&runIdentity(previous.toString())&&previous!=manifest.value("run")&&
        input.isString()&&input.toString().size()==64&&sha.match(input.toString()).hasMatch()&&proof.value("epochs").toInteger()>=2;
}
bool completeSourceMatches(const QJsonObject& packet,const QString& run){
    const auto manifest=packet.value("manifest").toObject();
    if(!runIdentity(run)||manifest.value("run")!=run||!sourceLineageMatches(packet,"complete"))return false;
    if((manifest.contains("resume_previous_run")||manifest.contains("checkpoint_input_sha256"))&&!sourceResumeLineageMatches(packet,"complete"))return false;
    const auto committed=packet.value("completion").toObject();
    const auto completion=committed.value("run")==run?committed:packet.value("completion_candidate").toObject();
    if(completion.value("schema")!="pgn-source-range-completion-1"||completion.value("run")!=run||
       !completion.value("source_range_complete").isBool()||!completion.value("source_range_complete").toBool()||
       !zero(completion.value("source_exit_code"))||!zero(packet.value("last_exit").toObject().value("exit_code"))||
       completion.value("start")!=manifest.value("start")||completion.value("end")!=manifest.value("end")||
       !exactDecimal(manifest.value("start"))||!exactDecimal(manifest.value("end"))||!exactDecimal(completion.value("source_positions")))return false;
    try{
        const auto start=manifest.value("start").toString(),end=manifest.value("end").toString();
        return source_decimal::less(start,end)&&source_decimal::difference(end,start)==completion.value("source_positions").toString();
    }catch(const std::invalid_argument&){return false;}
}
CompletionAdvanceGate::CompletionAdvanceGate(QString device,qint64 delay,int maximum):device_(std::move(device)),delay_(delay),maximum_(maximum){
    static const QRegularExpression identity("^GPU-[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}$");
    if(!identity.match(device_).hasMatch()||delay<1000||maximum<1||maximum>3)throw std::invalid_argument("Invalid bounded completion gate configuration");
}
void CompletionAdvanceGate::reset(){
    if(generation_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Advance ticket generation exhausted");
    ++generation_;run_.clear();observed_.clear();retryAt_=0;attempts_=0;inFlight_=false;held_=false;
}
std::optional<AdvanceTicket> CompletionAdvanceGate::claim(const QJsonObject& packet,const QString& run,qint64 now,bool enabled,bool paused,bool fragment){
    if(now<0||!enabled||paused||fragment||!runIdentity(run))return {};
    const auto manifest=packet.value("manifest").toObject(),lastExit=packet.value("last_exit").toObject();
    // Explicit false is required; absent/mistyped ownership or STOP is not
    // silently interpreted as permission. Device identity is caller-selected.
    if(manifest.value("run")!=run||manifest.value("uuid").toString().compare(device_,Qt::CaseInsensitive)!=0||packet.value("state")!="OFF"||
       !packet.value("owned").isBool()||packet.value("owned").toBool()||
       !packet.value("operator_stop").isBool()||packet.value("operator_stop").toBool()||
       lastExit.value("run")!=run||!zero(packet.value("pending_bytes"))||!completeSourceMatches(packet,run))return {};
    const auto observed=utcOrderKey(packet.value("observed_utc"));if(!observed)return {};
    if(run_!=run){reset();run_=run;}
    if(inFlight_||held_||attempts_>=maximum_||now<retryAt_||(!observed_.isEmpty()&&*observed<=observed_))return {};
    inFlight_=true;++attempts_;observed_=*observed;
    return AdvanceTicket{run_,attempts_,generation_};
}
AdvanceSettlement CompletionAdvanceGate::settle(const AdvanceTicket& ticket,AdvanceOutcome outcome,qint64 now){
    if(ticket.run!=run_||ticket.generation!=generation_||ticket.attempt!=attempts_||!inFlight_)return {true,false,held_,attempts_};
    inFlight_=false;
    const bool retry=outcome==AdvanceOutcome::UncertainTransport&&attempts_<maximum_&&now>=0&&now<=std::numeric_limits<qint64>::max()-delay_;
    held_=!retry;retryAt_=retry?now+delay_:std::numeric_limits<qint64>::max();
    return {false,retry,held_,attempts_};
}
}

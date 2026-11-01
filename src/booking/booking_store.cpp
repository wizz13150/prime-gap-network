#include "mining/source_checkpoint_recovery.h"
#include "booking/booking_store.h"
#include "booking/decimal_range.h"
#include "booking/queue_zone.h"
#include "mining/native_scientific_context.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QUuid>
#include <QRegularExpression>
#include <QSet>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <limits>
#include <sqlite3.h>
#include <stdexcept>

namespace pgn::booking {
namespace {
QString sessionDevice(const QString& device){
    const auto normalized=device.toLower();
    static const QRegularExpression gpu("^gpu-[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$");
    if(normalized.size()!=40||!gpu.match(normalized).hasMatch())throw std::runtime_error("Mining session requires an exact GPU UUID");
    return normalized;
}
QString sessionModeKey(const QString& device){return "mining-session-mode/v1/"+device;}
QString sessionPreferenceKey(const QString& device){return "mining-auto-after-manual/v1/"+device;}
QString modeText(pgn::mining::MiningSessionMode mode){
    using pgn::mining::MiningSessionMode;
    switch(mode){case MiningSessionMode::Unselected:return "unselected";case MiningSessionMode::Automatic:return "automatic";case MiningSessionMode::ManualReservations:return "manual-reservations";}
    throw std::runtime_error("Unknown mining session mode; metadata unchanged");
}
QString key(const QString& decimal){const auto value=canonicalUnsigned(decimal);return QString::number(value.size()).rightJustified(3,'0')+value;}
QString utc(){return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);}
class Statement {
public:
    sqlite3_stmt* value{};sqlite3* db{};
    Statement(sqlite3* connection,const char* sql):db(connection){
        if(sqlite3_prepare_v2(db,sql,-1,&value,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement(){if(value)sqlite3_finalize(value);}
    void text(int index,const QString& input){const auto bytes=input.toUtf8();if(sqlite3_bind_text(value,index,bytes.constData(),bytes.size(),SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    int step(){const int status=sqlite3_step(value);if(status!=SQLITE_ROW&&status!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(db));return status;}
    QString text(int index)const{const auto* value=sqlite3_column_text(this->value,index);return value?QString::fromUtf8(reinterpret_cast<const char*>(value)):QString{};}
};
QString exactMetadataText(const Statement& statement,int column){
    if(sqlite3_column_type(statement.value,column)!=SQLITE_TEXT)throw std::runtime_error("Mining metadata is not text");
    const auto* bytes=sqlite3_column_text(statement.value,column);
    const QByteArray raw(reinterpret_cast<const char*>(bytes),sqlite3_column_bytes(statement.value,column));
    const auto decoded=QString::fromUtf8(raw);
    if(decoded.toUtf8()!=raw)throw std::runtime_error("Mining metadata UTF8 representation changed");
    return decoded;
}
using Resume=pgn::mining::SourceCheckpointResume;
constexpr int ResumeRecordLimit=16384;
struct ResumeField {const char* name;QString Resume::*member;};
constexpr ResumeField ResumeFields[]={
    {"run",&Resume::run},{"start",&Resume::start},{"end",&Resume::end},{"length",&Resume::length},
    {"positions",&Resume::positions},{"nextStart",&Resume::nextStart},{"remaining",&Resume::remaining},
    {"tests",&Resume::tests},{"gaps",&Resume::gaps},{"batches",&Resume::batches},
    {"counterTimeNs",&Resume::counterTimeNs},{"checkpointLabel",&Resume::checkpointLabel},
    {"checkpointSha256",&Resume::checkpointSha256}
};
QString resumeKey(const QString& device,const QString& run){
    static const QRegularExpression pattern("^[0-9a-f]{32}$");
    if(run.size()!=32||!pattern.match(run).hasMatch())throw std::runtime_error("Resume requires an exact lowercase run ID");
    return "mining-resume/v1/"+device+"/"+run;
}
quint64 resumeWord(const QByteArray& bytes,int offset){
    quint64 result=0;for(unsigned i=0;i<8;++i)result|=quint64(uchar(bytes[offset+int(i)]))<<(8*i);return result;
}
void restoreResumeDialect(Resume& resume,const QByteArray& bytes){
    if(bytes.size()==64&&bytes.left(4)=="PGNS"){
        resume.dialect=pgn::mining::SourceCheckpointDialect::ScoutGapBound;
        resume.gapMinimum=quint32(resumeWord(bytes,0)>>32);
    }
}
QString resumeCounterBasis(const QByteArray& bytes){
    return bytes.left(4)=="PGNS"?QStringLiteral("PGNS_GAP32_64B_CUMULATIVE_BASELINE"):QStringLiteral("PGNCG001_CUMULATIVE_BASELINE");
}
void validateResume(const Resume& r,const QByteArray& bytes){
    // Storage consistency only. Neither these bytes nor their SHA carry a run
    // ID/device/ownership certificate. The caller pays the actual proof gate.
    for(const auto& number:QStringList{r.start,r.end,r.length,r.positions,r.nextStart,r.remaining,r.tests,r.gaps,r.batches,r.counterTimeNs})
        if(canonicalUnsigned(number)!=number)throw std::runtime_error("Resume decimal is not canonical");
    bool timeOk=false;r.counterTimeNs.toULongLong(&timeOk);
    if(!timeOk)throw std::runtime_error("Resume counter clock exceeds UInt64");
    const bool adaptive=r.dialect==pgn::mining::SourceCheckpointDialect::ScoutGapBound;
    if(bytes.size()!=64||(adaptive?bytes.left(4)!="PGNS":bytes.left(8)!="PGNCG001"))throw std::runtime_error("Resume requires its explicit64-byte engine checkpoint codec");
    if(adaptive&&(!r.gapMinimum||*r.gapMinimum<954||*r.gapMinimum>2147483647U||*r.gapMinimum!=quint32(resumeWord(bytes,0)>>32)))
        throw std::runtime_error("Resume threshold differs from exact gap-bound checkpoint bytes");
    const auto low=resumeWord(bytes,8),high=resumeWord(bytes,16),length=resumeWord(bytes,24),positions=resumeWord(bytes,32);
    if((adaptive?(high<4||high>2147483647ULL):high!=10)||!length||length>std::numeric_limits<quint64>::max()-low||positions>=length)
        throw std::runtime_error("Resume checkpoint range is outside current producer contract");
    const auto next=low+positions,remainder=(next%30+(high%30)*16)%30;
    if(next<remainder)throw std::runtime_error("Resume checkpoint floor crosses low64 window");
    QString start=QString::number(high);for(int i=0;i<64;++i)start=addUnsigned(start,start);start=addUnsigned(start,QString::number(low));
    if(r.start!=start||r.length!=QString::number(length)||r.positions!=QString::number(positions)||
       r.end!=addUnsigned(start,r.length)||r.nextStart!=addUnsigned(start,r.positions)||
       r.remaining!=subtractUnsigned(r.length,r.positions)||r.tests!=QString::number(resumeWord(bytes,40))||
       r.gaps!=QString::number(resumeWord(bytes,48))||r.batches!=QString::number(resumeWord(bytes,56)))
        throw std::runtime_error("Resume tuple and checkpoint bytes/arithmetic differ");
    if(r.checkpointLabel.isEmpty()||r.checkpointLabel.size()>ResumeRecordLimit||r.checkpointLabel.contains(QChar(0))||r.checkpointLabel.contains('\n')||r.checkpointLabel.contains('\r')||
       QString::fromUtf8(r.checkpointLabel.toUtf8())!=r.checkpointLabel)
        throw std::runtime_error("Resume checkpoint label is absent or not exact UTF8");
    if(r.checkpointSha256!=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()))
        throw std::runtime_error("Resume checkpoint SHA256 differs");
}
QByteArray encodeResume(const QString& device,const Resume& r,const QByteArray& bytes){
    validateResume(r,bytes);QJsonObject tuple;
    for(const auto& field:ResumeFields)tuple.insert(field.name,r.*field.member);
    const auto raw=QJsonDocument(QJsonObject{{"schema","pgn-source-resume-1"},{"device",device},
        {"resume",tuple},{"checkpoint_base64",QString::fromLatin1(bytes.toBase64())}}).toJson(QJsonDocument::Compact);
    if(raw.size()>ResumeRecordLimit)throw std::runtime_error("Resume record exceeds16KiB UTF8 budget");return raw;
}
StoredSourceResume decodeResume(const Statement& query,const QString& device,const QString& run){
    if(sqlite3_column_type(query.value,0)!=SQLITE_TEXT||sqlite3_column_bytes(query.value,0)>ResumeRecordLimit)
        throw std::runtime_error("Resume metadata type/size invalid");
    const auto raw=exactMetadataText(query,0).toUtf8();QJsonParseError error;const auto document=QJsonDocument::fromJson(raw,&error);
    if(error.error!=QJsonParseError::NoError||!document.isObject()||document.toJson(QJsonDocument::Compact)!=raw)
        throw std::runtime_error("Resume JSON must be canonical, no duplicates/trailing bytes");
    const auto object=document.object();
    if(object.size()!=4||object.value("schema")!="pgn-source-resume-1"||object.value("device")!=device||
       !object.value("resume").isObject()||!object.value("checkpoint_base64").isString())
        throw std::runtime_error("Resume metadata envelope identity invalid");
    const auto tuple=object.value("resume").toObject();StoredSourceResume result;
    if(tuple.size()!=13)throw std::runtime_error("Resume tuple requires all13 fields");
    for(const auto& field:ResumeFields){const auto value=tuple.value(field.name);if(!value.isString())throw std::runtime_error("Resume tuple must contain exact strings");result.resume.*field.member=value.toString();}
    if(result.resume.run!=run)throw std::runtime_error("Resume run differs from metadata key");
    const auto encoded=object.value("checkpoint_base64").toString().toLatin1();result.checkpointBytes=QByteArray::fromBase64(encoded);
    if(result.checkpointBytes.size()!=64||result.checkpointBytes.toBase64()!=encoded)
        throw std::runtime_error("Resume base64 is not canonical64 bytes");
    restoreResumeDialect(result.resume,result.checkpointBytes);validateResume(result.resume,result.checkpointBytes);return result;
}
void validateResumeAdvance(const Resume& before,const Resume& after){
    if(before.run!=after.run||before.start!=after.start||before.end!=after.end||before.length!=after.length||before.checkpointLabel!=after.checkpointLabel
        ||before.dialect!=after.dialect||before.gapMinimum!=after.gapMinimum)
        throw std::runtime_error("Existing resume run bounds/label cannot be rebound");
    for(const auto pair:{qMakePair(before.positions,after.positions),qMakePair(before.tests,after.tests),
        qMakePair(before.gaps,after.gaps),qMakePair(before.batches,after.batches),qMakePair(before.counterTimeNs,after.counterTimeNs)})
        if(compareUnsigned(pair.first,pair.second)>0)throw std::runtime_error("Resume counters cannot regress");
}
bool localTable(sqlite3* db,const char* name){
    Statement q(db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name=?");q.text(1,QString::fromLatin1(name));return q.step()==SQLITE_ROW;
}
bool retiredResumeRun(sqlite3* db,const QString& run){
    if(!localTable(db,"dispatch_resume_epochs"))return false;
    Statement q(db,"SELECT 1 FROM dispatch_resume_epochs WHERE previous_run=?");q.text(1,run);return q.step()==SQLITE_ROW;
}
void requireResumeRun(const QString& run){
    static const QRegularExpression pattern("^[0-9a-f]{32}$");
    if(run.size()!=32||!pattern.match(run).hasMatch())throw std::runtime_error("Resume epoch requires exact lowercase run32");
}
void requireResumeUuid(const QString& id){
    const QUuid uuid(id);
    if(uuid.isNull()||uuid.toString(QUuid::WithoutBraces)!=id)throw std::runtime_error("Resume epoch requires canonical nonnull UUID");
}
constexpr int ResumeEpochLimit=24576;
QByteArray encodeResumeEpoch(const CheckpointResumeEpoch& e){
    requireResumeUuid(e.requestUUID);requireResumeUuid(e.bookingId);requireResumeUuid(e.observationUUID);
    requireResumeRun(e.previousRun);requireResumeRun(e.run);
    const auto normalized=sessionDevice(e.device);
    if(e.device!="GPU-"+normalized.mid(4)||e.previousRun==e.run||e.checkpoint.resume.run!=e.previousRun||e.rawConservationRequirement!="LINEAGE_RAW_CONSERVATION_REQUIRED")
        throw std::runtime_error("Resume epoch identity contradicts checkpoint");
    if(e.predecessor)requireResumeRun(*e.predecessor);
    const auto checkpoint=QJsonDocument::fromJson(encodeResume(normalized,e.checkpoint.resume,e.checkpoint.checkpointBytes)).object();
    QJsonObject object{{"schema","pgn-checkpoint-resume-epoch-1"},{"origin","RESUME_CHECKPOINT"},
        {"request_id",e.requestUUID},{"device",e.device},{"booking_id",e.bookingId},{"previous_run",e.previousRun},
        {"run",e.run},{"observation_id",e.observationUUID},{"counter_basis",resumeCounterBasis(e.checkpoint.checkpointBytes)},
        {"raw_conservation_requirement",e.rawConservationRequirement},
        {"predecessor",e.predecessor?QJsonValue(*e.predecessor):QJsonValue(QJsonValue::Null)},
        {"checkpoint",checkpoint}};
    if(!e.recoveryEvidence.isEmpty()){
        pgn::mining::checkpoint_recovery::validate(e.recoveryEvidence,e.previousRun,e.run,e.checkpoint.resume.checkpointSha256,e.checkpoint.checkpointBytes);
        object.insert("origin",pgn::mining::checkpoint_recovery::Origin);object.insert("recovery",e.recoveryEvidence);
    }
    const auto raw=QJsonDocument(object).toJson(QJsonDocument::Compact);
    if(raw.size()>ResumeEpochLimit)throw std::runtime_error("Resume epoch exceeds24KiB UTF8 budget");return raw;
}
CheckpointResumeEpoch decodeResumeEpoch(const Statement& q){
    // SELECT run,previous_run,booking_id,request_id,receipt, in this order.
    if(sqlite3_column_type(q.value,4)!=SQLITE_TEXT||sqlite3_column_bytes(q.value,4)>ResumeEpochLimit)
        throw std::runtime_error("Resume epoch receipt type/size invalid");
    const auto raw=exactMetadataText(q,4).toUtf8();QJsonParseError error;const auto doc=QJsonDocument::fromJson(raw,&error);const auto o=doc.object();
    if(error.error!=QJsonParseError::NoError||!doc.isObject()||doc.toJson(QJsonDocument::Compact)!=raw||o.size()!=(o.value("origin")==pgn::mining::checkpoint_recovery::Origin?13:12))
        throw std::runtime_error("Resume epoch JSON corrupt/noncanonical");
    for(const auto& name:QStringList{"schema","origin","request_id","device","booking_id","previous_run","run","observation_id","counter_basis","raw_conservation_requirement"})
        if(!o.value(name).isString())throw std::runtime_error("Resume epoch fields must be exact strings");
    if(o.value("schema")!="pgn-checkpoint-resume-epoch-1"||(o.value("origin")!="RESUME_CHECKPOINT"&&o.value("origin")!=pgn::mining::checkpoint_recovery::Origin)||
       (o.value("counter_basis")!="PGNCG001_CUMULATIVE_BASELINE"&&o.value("counter_basis")!="PGNS_GAP32_64B_CUMULATIVE_BASELINE")||!o.value("checkpoint").isObject())
        throw std::runtime_error("Resume epoch schema/origin/basis invalid");
    CheckpointResumeEpoch e;e.requestUUID=o.value("request_id").toString();e.device=o.value("device").toString();
    e.bookingId=o.value("booking_id").toString();e.previousRun=o.value("previous_run").toString();e.run=o.value("run").toString();e.observationUUID=o.value("observation_id").toString();
    e.rawConservationRequirement=o.value("raw_conservation_requirement").toString();
    if(o.value("origin")==pgn::mining::checkpoint_recovery::Origin){if(!o.value("recovery").isObject())throw std::runtime_error("Recovery epoch evidence absent");e.recoveryEvidence=o.value("recovery").toObject();}
    if(!o.value("predecessor").isNull()){if(!o.value("predecessor").isString())throw std::runtime_error("Resume epoch predecessor invalid");e.predecessor=o.value("predecessor").toString();}
    if(exactMetadataText(q,0)!=e.run||exactMetadataText(q,1)!=e.previousRun||exactMetadataText(q,2)!=e.bookingId||exactMetadataText(q,3)!=e.requestUUID)
        throw std::runtime_error("Resume epoch row/receipt identity mismatch");
    const auto checkpoint=o.value("checkpoint").toObject(),tuple=checkpoint.value("resume").toObject();
    if(checkpoint.size()!=4||checkpoint.value("schema")!="pgn-source-resume-1"||checkpoint.value("device")!=sessionDevice(e.device)||
       !checkpoint.value("resume").isObject()||!checkpoint.value("checkpoint_base64").isString()||tuple.size()!=13)
        throw std::runtime_error("Resume epoch checkpoint envelope invalid");
    for(const auto& field:ResumeFields){const auto value=tuple.value(field.name);if(!value.isString())throw std::runtime_error("Resume epoch tuple is not exact strings");e.checkpoint.resume.*field.member=value.toString();}
    const auto base64=checkpoint.value("checkpoint_base64").toString().toLatin1();e.checkpoint.checkpointBytes=QByteArray::fromBase64(base64);restoreResumeDialect(e.checkpoint.resume,e.checkpoint.checkpointBytes);
    if(e.checkpoint.checkpointBytes.size()!=64||e.checkpoint.checkpointBytes.toBase64()!=base64||encodeResumeEpoch(e)!=raw)
        throw std::runtime_error("Resume epoch checkpoint bytes/tuple differ");
    return e;
}
InitialStartDispatch initialDispatch(const Statement& q){
    InitialStartDispatch d;d.bookingId=exactMetadataText(q,0);d.start=exactMetadataText(q,1);d.end=exactMetadataText(q,2);
    d.device="GPU-"+sessionDevice(exactMetadataText(q,3)).mid(4);d.state=exactMetadataText(q,4);
    if(d.bookingId.isEmpty()||d.bookingId.size()>128||d.bookingId.contains(QChar(0))||canonicalUnsigned(d.start)!=d.start||canonicalUnsigned(d.end)!=d.end||compareUnsigned(d.start,d.end)>=0)
        throw std::runtime_error("Stored dispatch identity/range invalid");
    for(int i=5;i<=6;++i)if(sqlite3_column_type(q.value,i)!=SQLITE_NULL){
        const auto v=exactMetadataText(q,i);static const QRegularExpression run("^[0-9a-f]{32}$");
        if(v.size()!=32||!run.match(v).hasMatch())throw std::runtime_error("Stored dispatch predecessor/run invalid");
        (i==5?d.predecessor:d.run)=v;
    }
    if(d.state!="PREPARED"&&d.state!="ARMED"&&d.state!="STARTED"&&d.state!="SOURCE_FINISHED"&&d.state!="CANCELLED")throw std::runtime_error("Unknown stored dispatch state");
    return d;
}
void validateDispatchBooking(sqlite3* db,const InitialStartDispatch& d){
    Statement q(db,"SELECT start,end,device,kind FROM bookings WHERE id=?");q.text(1,d.bookingId);
    if(q.step()!=SQLITE_ROW||exactMetadataText(q,0)!=d.start||exactMetadataText(q,1)!=d.end||
       sessionDevice(exactMetadataText(q,2))!=sessionDevice(d.device)||exactMetadataText(q,3)!="LOCAL_INTENT")
        throw std::runtime_error("Stored dispatch booking missing or rebound");
}
constexpr int SourceManifestLimit=16384;
struct SourceManifestRecord {
    QJsonObject manifest;QByteArray raw;QString run,device,bookingId,start,end;
    std::optional<QString> previousRun;QString checkpointSha;
    bool nativeSearchOrigin{};
};
QString sourceManifestKey(const QString& run){requireResumeRun(run);return "mining-source-manifest/v1/"+run;}
void manifestRequire(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
bool sourceManifestUtc(const QJsonValue& v){
    if(!v.isString())return false;
    static const QRegularExpression utcPattern("^(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2})(?:\\.\\d{1,9})?Z$");
    const auto text=v.toString();const auto matched=utcPattern.match(text);
    if(!matched.hasMatch()||matched.capturedLength()!=text.size())return false;
    const auto prefix=matched.captured(1);const auto date=QDateTime::fromString(prefix+'Z',Qt::ISODate);
    return date.isValid()&&date.toUTC().toString("yyyy-MM-dd'T'HH:mm:ss")==prefix;
}
void sourceManifestUtf8(const QJsonObject& object){
    // Iterate, with no history-depth quota or recursive call stack. The JSON
    // record itself is independently bounded before this string inspection.
    QVector<QJsonValue> pending{object};
    const auto exact=[](const QString& text){return QString::fromUtf8(text.toUtf8())==text;};
    while(!pending.isEmpty()){
        const auto value=pending.takeLast();
        if(value.isString())manifestRequire(exact(value.toString()),"Source manifest contains non-UTF8 string representation");
        else if(value.isObject()){const auto nested=value.toObject();for(auto it=nested.begin();it!=nested.end();++it){manifestRequire(exact(it.key()),"Source manifest key encoding is invalid");pending.append(it.value());}}
        else if(value.isArray())for(const auto& child:value.toArray())pending.append(child);
    }
}
SourceManifestRecord sourceManifest(const QString& device,const QJsonObject& object){
    SourceManifestRecord r;r.manifest=object;r.raw=QJsonDocument(object).toJson(QJsonDocument::Compact);
    manifestRequire(!r.raw.isEmpty()&&r.raw.size()<=SourceManifestLimit,"Source manifest exceeds16KiB canonical UTF8 bound");sourceManifestUtf8(object);
    const auto canonicalDevice="GPU-"+sessionDevice(device).mid(4);
    manifestRequire(object.value("schema")=="pgn-native-process-1"&&object.value("run").isString()&&object.value("uuid").isString()&&object.value("uuid").toString()==canonicalDevice,"Source manifest schema/run/device identity invalid");
    r.run=object.value("run").toString();requireResumeRun(r.run);r.device=canonicalDevice;
    const auto pid=object.value("pid");const auto integer=pid.toInteger(-1);
    manifestRequire(pid.isDouble()&&integer>0&&integer<=4294967295LL&&pid.toDouble()==double(integer)&&sourceManifestUtc(object.value("birth")),"Source manifest actual PID/birth is unknown or noncanonical");
    for(const auto* field:{"start","end"}){const auto v=object.value(field);manifestRequire(v.isString()&&v.toString().size()<=160&&canonicalUnsigned(v.toString())==v.toString(),"Source manifest requires exact unsigned string bounds");}
    r.start=object.value("start").toString();r.end=object.value("end").toString();manifestRequire(compareUnsigned(r.start,r.end)<0,"Source manifest range is empty");
    if(object.value("queued_intent").isNull()&&object.value("source_kind")=="NATIVE_PGN_SCIENCE")
        r.nativeSearchOrigin=pgn::mining::nativeScientificSearchOrigin(object);
    else {manifestRequire(object.value("queued_intent").isString(),"Source manifest requires actual queued identity");r.bookingId=object.value("queued_intent").toString();requireResumeUuid(r.bookingId);}
    const bool previous=object.contains("resume_previous_run"),hash=object.contains("checkpoint_input_sha256");
    manifestRequire(previous==hash,"Source resume process markers must be both absent or both exact");
    if(previous){
        manifestRequire(object.value("resume_previous_run").isString()&&object.value("checkpoint_input_sha256").isString(),"Source resume markers must be exact strings");
        r.previousRun=object.value("resume_previous_run").toString();requireResumeRun(*r.previousRun);
        r.checkpointSha=object.value("checkpoint_input_sha256").toString();static const QRegularExpression hashPattern("^[0-9a-f]{64}$");
        manifestRequire(*r.previousRun!=r.run&&r.checkpointSha.size()==64&&hashPattern.match(r.checkpointSha).hasMatch(),"Source resume previous run/checkpoint SHA invalid");
    }return r;
}
std::optional<SourceManifestRecord> storedSourceManifest(sqlite3* db,const QString& device,const QString& run){
    const auto name=sourceManifestKey(run);Statement q(db,"SELECT key,value FROM booking_metadata WHERE key=?");q.text(1,name);
    if(q.step()!=SQLITE_ROW)return {};
    manifestRequire(exactMetadataText(q,0)==name&&sqlite3_column_type(q.value,1)==SQLITE_TEXT&&sqlite3_column_bytes(q.value,1)<=SourceManifestLimit,"Source manifest metadata key/type/size invalid");
    const auto raw=exactMetadataText(q,1).toUtf8();QJsonParseError error;const auto document=QJsonDocument::fromJson(raw,&error);
    manifestRequire(error.error==QJsonParseError::NoError&&document.isObject()&&document.toJson(QJsonDocument::Compact)==raw,"Source manifest JSON is corrupt/noncanonical");
    auto result=sourceManifest(device,document.object());manifestRequire(result.run==run&&result.raw==raw,"Source manifest metadata run/bytes differ");
    manifestRequire(q.step()==SQLITE_DONE,"Source manifest metadata key is ambiguous");return result;
}
std::optional<CheckpointResumeEpoch> sourceManifestEpoch(sqlite3* db,const QString& run){
    Statement type(db,"SELECT type FROM sqlite_master WHERE name='dispatch_resume_epochs'");if(type.step()!=SQLITE_ROW)return {};
    manifestRequire(exactMetadataText(type,0)=="table","Source manifest epoch index is not an actual table");
    Statement q(db,"SELECT run,previous_run,booking_id,request_id,receipt FROM dispatch_resume_epochs WHERE run=?");q.text(1,run);
    if(q.step()!=SQLITE_ROW)return {};auto epoch=decodeResumeEpoch(q);manifestRequire(q.step()==SQLITE_DONE,"Source manifest epoch index is ambiguous");
    Statement unique(db,"SELECT run FROM dispatch_resume_epochs WHERE previous_run=? OR request_id=? LIMIT 2");unique.text(1,epoch.previousRun);unique.text(2,epoch.requestUUID);
    manifestRequire(unique.step()==SQLITE_ROW&&exactMetadataText(unique,0)==epoch.run&&unique.step()==SQLITE_DONE,"Source manifest epoch predecessor/request index conflicts");return epoch;
}
void sourceManifestStorageTypes(sqlite3* db){
    // Detect non-TEXT shadows once, not by a CAST/full-table scan at every
    // ancestry hop. Normal lineage lookups retain the native PK/unique seeks.
    Statement metadata(db,"SELECT 1 FROM booking_metadata WHERE typeof(key)<>'text' AND CAST(key AS TEXT) LIKE 'mining-source-manifest/v1/%' LIMIT 1");
    manifestRequire(metadata.step()==SQLITE_DONE,"Source manifest index contains a non-TEXT shadow key");
    Statement type(db,"SELECT type FROM sqlite_master WHERE name='dispatch_resume_epochs'");if(type.step()!=SQLITE_ROW)return;
    manifestRequire(exactMetadataText(type,0)=="table","Source manifest epoch index is not an actual table");
    Statement epochs(db,"SELECT 1 FROM dispatch_resume_epochs WHERE typeof(run)<>'text' OR typeof(previous_run)<>'text' OR typeof(booking_id)<>'text' OR typeof(request_id)<>'text' OR typeof(receipt)<>'text' LIMIT 1");
    manifestRequire(epochs.step()==SQLITE_DONE,"Source manifest epoch index contains non-TEXT identity/receipt");
}
void sourceManifestEpochMatches(const SourceManifestRecord& r,const CheckpointResumeEpoch& e){
    manifestRequire(r.previousRun&&e.run==r.run&&e.previousRun==*r.previousRun&&e.device==r.device&&e.bookingId==r.bookingId&&
        e.checkpoint.resume.run==*r.previousRun&&e.checkpoint.resume.start==r.start&&e.checkpoint.resume.end==r.end&&e.checkpoint.resume.checkpointSha256==r.checkpointSha,
        "Actual source resume manifest contradicts its immutable epoch/checkpoint receipt");
}
void sourceManifestPointer(sqlite3* db,const SourceManifestRecord& r,const InitialStartDispatch& d){
    manifestRequire(d.run.has_value(),"Actual source manifest requires an exact dispatch run, never NULL/PREPARED inference");
    QString cursor=*d.run;QSet<QString> visited;
    while(cursor!=r.run){
        manifestRequire(!visited.contains(cursor),"Source manifest active epoch pointer contains a cycle");visited.insert(cursor);
        const auto e=sourceManifestEpoch(db,cursor);
        manifestRequire(e&&e->device==r.device&&e->bookingId==r.bookingId&&e->checkpoint.resume.start==r.start&&e->checkpoint.resume.end==r.end&&e->predecessor==d.predecessor,
            "Source manifest no longer belongs to the indexed active dispatch lineage");cursor=e->previousRun;
    }
}
InitialStartDispatch sourceManifestRelations(sqlite3* db,const SourceManifestRecord& r,bool verifyPointer){
    Statement physical(db,"SELECT start,end,device,kind,state,start_key,end_key FROM bookings WHERE id=?");physical.text(1,r.run);
    manifestRequire(physical.step()==SQLITE_ROW&&exactMetadataText(physical,0)==r.start&&exactMetadataText(physical,1)==r.end&&exactMetadataText(physical,2)==r.device&&
        exactMetadataText(physical,3)=="RUNTIME"&&exactMetadataText(physical,5)==key(r.start)&&exactMetadataText(physical,6)==key(r.end),"Source manifest requires its exact existing physical RUNTIME identity");
    const auto physicalState=exactMetadataText(physical,4);manifestRequire(physicalState=="ACTIVE"||physicalState=="PAUSED"||physicalState=="SOURCE_FINISHED","Source manifest physical run is ABORTED/unknown");
    Statement q(db,"SELECT id,start,end,device,state,predecessor,run FROM dispatch_requests WHERE id=?");q.text(1,r.bookingId);
    manifestRequire(q.step()==SQLITE_ROW,"Source manifest has no existing queued dispatch");const auto d=initialDispatch(q);
    manifestRequire(exactMetadataText(q,3)==r.device&&d.device==r.device&&d.start==r.start&&d.end==r.end,"Source manifest queued dispatch range/device differs");
    Statement local(db,"SELECT start,end,device,kind,state,start_key,end_key FROM bookings WHERE id=?");local.text(1,r.bookingId);
    manifestRequire(local.step()==SQLITE_ROW&&exactMetadataText(local,0)==r.start&&exactMetadataText(local,1)==r.end&&exactMetadataText(local,2)==r.device&&
        exactMetadataText(local,3)=="LOCAL_INTENT"&&exactMetadataText(local,5)==key(r.start)&&exactMetadataText(local,6)==key(r.end),"Source manifest requires exact existing LOCAL_INTENT identity");
    const auto localState=exactMetadataText(local,4);
    manifestRequire((d.state=="PREPARED"&&localState=="DISPATCHING")||(d.state=="STARTED"&&localState=="DISPATCHED")||
        (d.state=="SOURCE_FINISHED"&&localState=="SOURCE_FINISHED"),"Source manifest local/dispatch physical-state projection is inconsistent");
    manifestRequire(d.run.has_value(),"Source manifest dispatch run is unknown");if(verifyPointer)sourceManifestPointer(db,r,d);
    if(*d.run==r.run)manifestRequire((d.state=="SOURCE_FINISHED"&&physicalState=="SOURCE_FINISHED")||
        ((d.state=="PREPARED"||d.state=="STARTED")&&(physicalState=="ACTIVE"||physicalState=="PAUSED")),"Source manifest dispatch state is incompatible with its physical run");return d;
}
QString sourceManifestChain(sqlite3* db,SourceManifestRecord current){
    if(current.nativeSearchOrigin){
        // An observed chain-search execution is not a community booking. Keep
        // the same immutable manifest/physical-row checks without inventing a
        // LOCAL_INTENT or dispatch. This admits source history, never a block.
        Statement physical(db,"SELECT start,end,device,kind,state,start_key,end_key FROM bookings WHERE id=?");physical.text(1,current.run);
        manifestRequire(physical.step()==SQLITE_ROW&&exactMetadataText(physical,0)==current.start&&exactMetadataText(physical,1)==current.end&&exactMetadataText(physical,2)==current.device&&exactMetadataText(physical,3)=="RUNTIME"&&
            (exactMetadataText(physical,4)=="ACTIVE"||exactMetadataText(physical,4)=="PAUSED"||exactMetadataText(physical,4)=="SOURCE_FINISHED")&&exactMetadataText(physical,5)==key(current.start)&&exactMetadataText(physical,6)==key(current.end),"Native search origin lacks its exact observed physical run");
        manifestRequire(!sourceManifestEpoch(db,current.run),"Native search origin cannot borrow a community resume epoch");
        return current.manifest.value("birth").toString();
    }
    QSet<QString> visited;bool first=true;
    for(;;){
        manifestRequire(!visited.contains(current.run),"Source manifest ancestry contains a cycle");visited.insert(current.run);const auto d=sourceManifestRelations(db,current,first);first=false;
        const auto epoch=sourceManifestEpoch(db,current.run);
        if(!current.previousRun){manifestRequire(!epoch,"Source manifest missing actual resume markers for an indexed epoch");return current.manifest.value("birth").toString();}
        manifestRequire(epoch.has_value()&&epoch->predecessor==d.predecessor,"Source resume manifest has no exact immutable native epoch/predecessor receipt");sourceManifestEpochMatches(current,*epoch);
        const auto previous=storedSourceManifest(db,current.device,*current.previousRun);
        manifestRequire(previous&&previous->bookingId==current.bookingId&&previous->start==current.start&&previous->end==current.end,
            "Source resume ancestry lacks its actual previous-run manifest; legacy origin remains unknown");current=*previous;
    }
}
// Transaction-local classification only. SOURCE here is the already admitted
// local state, not reconstructed raw conservation/authentication. Retain every
// PAUSED row/checkpoint/date. Only its exactly covered historical role changes
// INITIAL_START blocking/capacity accounting. No depth quota and no new ledger.
class CoveredPausedAncestors {
    sqlite3* db;bool checkedTypes{};QSet<QString> covered;
public:
    explicit CoveredPausedAncestors(sqlite3* connection):db(connection){}
    bool contains(const QString& id,const QString& device){
        if(covered.contains(id))return true;
        if(!retiredResumeRun(db,id))return false;
        if(!checkedTypes){sourceManifestStorageTypes(db);checkedTypes=true;}
        const auto ancestor=storedSourceManifest(db,device,id);
        manifestRequire(ancestor.has_value(),"Retired PAUSED ancestor has no immutable actual manifest");
        const auto d=sourceManifestRelations(db,*ancestor,true);
        if(d.state!="SOURCE_FINISHED")return false; // current partial/live work still blocks
        manifestRequire(d.run&&*d.run!=id,"PAUSED row is not a strict retired ancestor");
        const auto final=storedSourceManifest(db,ancestor->device,*d.run);
        manifestRequire(final&&final->bookingId==ancestor->bookingId&&final->start==ancestor->start&&final->end==ancestor->end,
            "Retired ancestor lacks exact current SOURCE descendant manifest");
        sourceManifestChain(db,*final); // physical + LOCAL_INTENT + dispatch + all epoch/marker tuples
        QSet<QString> candidates,visited;auto cursor=*final;
        while(cursor.previousRun){
            manifestRequire(!visited.contains(cursor.run),"Covered PAUSED ancestry contains a cycle");visited.insert(cursor.run);
            const auto previous=storedSourceManifest(db,cursor.device,*cursor.previousRun);
            manifestRequire(previous.has_value(),"Covered PAUSED ancestry lost an actual manifest");
            Statement state(db,"SELECT state FROM bookings WHERE id=? AND kind='RUNTIME'");state.text(1,previous->run);
            manifestRequire(state.step()==SQLITE_ROW&&exactMetadataText(state,0)=="PAUSED"&&state.step()==SQLITE_DONE,
                "Retired partial ancestor is not exactly PAUSED; no historical normalization");
            candidates.insert(previous->run);cursor=*previous;
        }
        manifestRequire(candidates.contains(id),"PAUSED row does not belong to current SOURCE ancestry");
        // A lineage is validated once per transaction, not K times for K
        // ancestors. Keep only run IDs; no full manifests or raw data cache.
        covered.unite(candidates);return true;
    }
};
quint64 operationalActiveCount(sqlite3* db){
    CoveredPausedAncestors coverage(db);quint64 count=0;
    Statement q(db,"SELECT id,state,kind,device FROM bookings WHERE state IN('QUEUED','ARMED','DISPATCHING','DISPATCHED','ACTIVE','PAUSED')");
    while(q.step()==SQLITE_ROW){
        if(exactMetadataText(q,1)=="PAUSED"&&exactMetadataText(q,2)=="RUNTIME"&&coverage.contains(exactMetadataText(q,0),exactMetadataText(q,3)))continue;
        ++count;
    }return count;
}
void initialReadRequire(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
void initialReadKeys(const QJsonObject& o,QStringList expected){auto actual=o.keys();actual.sort();expected.sort();initialReadRequire(actual==expected,"Initial receipt exact keys differ");}
QString initialReadString(const QJsonObject& o,const char* key,int limit=4096){const auto value=o.value(key);initialReadRequire(value.isString(),"Initial receipt field is not an exact string");const auto s=value.toString();initialReadRequire(s.size()<=limit&&!s.contains(QChar(0))&&QString::fromUtf8(s.toUtf8())==s,"Initial receipt text encoding/bound invalid");return s;}
QString initialReadDecimal(const QJsonObject& o,const char* key){const auto s=initialReadString(o,key,160);initialReadRequire(canonicalUnsigned(s)==s,"Initial receipt noncanonical decimal");return s;}
void initialReadHash(const QString& value){static const QRegularExpression hash("^[0-9a-f]{64}$");initialReadRequire(value.size()==64&&hash.match(value).hasMatch(),"Initial receipt SHA/fingerprint invalid");}
bool initialReadMultiple(const QString& value,const QString& divisor){QString remainder="0";for(const auto digit:value){remainder=canonicalUnsigned(remainder+digit);while(compareUnsigned(remainder,divisor)>=0)remainder=subtractUnsigned(remainder,divisor);}return remainder=="0";}
std::optional<QString> initialReadMetadata(sqlite3* db,const QString& name,int limit=8192){
    Statement q(db,"SELECT key,value FROM booking_metadata WHERE key=? OR key=CAST(? AS BLOB) LIMIT 2");q.text(1,name);q.text(2,name);if(q.step()!=SQLITE_ROW)return {};
    initialReadRequire(exactMetadataText(q,0)==name&&sqlite3_column_type(q.value,1)==SQLITE_TEXT&&sqlite3_column_bytes(q.value,1)>0&&sqlite3_column_bytes(q.value,1)<=limit,"Initial metadata key/type/size invalid");
    const auto text=exactMetadataText(q,1);initialReadRequire(!text.contains(QChar(0))&&q.step()==SQLITE_DONE,"Initial metadata ambiguous/NUL identity");return text;
}
struct InitialReadReceipt {QJsonObject object;QString bookingId,start,end;bool automatic{};};
InitialReadReceipt initialReadReceipt(const QByteArray& raw,const QString& request,const QString& device,const QString& observation){
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(raw,&error);
    initialReadRequire(!raw.isEmpty()&&raw.size()<=8192&&error.error==QJsonParseError::NoError&&doc.isObject()&&doc.toJson(QJsonDocument::Compact)==raw,"Initial receipt JSON corrupt/noncanonical/duplicate keys");
    InitialReadReceipt r;r.object=doc.object();const auto& o=r.object;
    initialReadKeys(o,{"schema","origin","request_id","device","input_fingerprint","observation_id","mode","booking_id","start","end","scope","registry_provenance"});
    initialReadRequire(initialReadString(o,"schema")=="pgn-initial-start-receipt-1"&&initialReadString(o,"origin")=="INITIAL_START"&&initialReadString(o,"request_id")==request&&initialReadString(o,"device")==device&&initialReadString(o,"observation_id")==observation,"Initial receipt command/device/observation differs");
    r.bookingId=initialReadString(o,"booking_id");requireResumeUuid(r.bookingId);r.start=initialReadDecimal(o,"start");r.end=initialReadDecimal(o,"end");initialReadRequire(compareUnsigned(r.start,r.end)<0,"Initial receipt selected range empty");
    const auto mode=initialReadString(o,"mode");initialReadRequire(mode=="automatic"||mode=="manual-reservations","Initial receipt mode unknown");r.automatic=mode=="automatic";
    initialReadRequire(o.value("scope").isObject()&&o.value("registry_provenance").isObject(),"Initial receipt scope/provenance absent");const auto s=o.value("scope").toObject(),p=o.value("registry_provenance").toObject();
    const QStringList fields{"execution_start","execution_end","auto_start","auto_end","block_anchor","block_width"};initialReadKeys(s,fields);for(const auto& field:fields)initialReadDecimal(s,field.toLatin1().constData());
    const auto es=s.value("execution_start").toString(),ee=s.value("execution_end").toString(),a=s.value("auto_start").toString(),b=s.value("auto_end").toString(),anchor=s.value("block_anchor").toString(),width=s.value("block_width").toString();
    initialReadRequire(compareUnsigned(es,ee)<0&&compareUnsigned(a,b)<0&&compareUnsigned(a,es)>=0&&compareUnsigned(b,ee)<=0&&width!="0"&&compareUnsigned(anchor,es)<=0&&compareUnsigned(r.start,es)>=0&&compareUnsigned(r.end,ee)<=0,"Initial receipt execution/automatic scope/grid invalid");
    if(r.automatic)initialReadRequire(compareUnsigned(r.start,a)>=0&&compareUnsigned(r.end,b)<=0&&subtractUnsigned(r.end,r.start)==width&&initialReadMultiple(subtractUnsigned(r.start,anchor),width),"Initial Auto receipt not one exact grid block");
    initialReadKeys(p,{"url","sha256","observed_utc","semantic_digest"});const auto url=initialReadString(p,"url"),observed=initialReadString(p,"observed_utc"),digest=initialReadString(p,"semantic_digest");
    initialReadRequire(!url.isEmpty()&&url.toUtf8().size()<=4096&&!url.contains('\r')&&!url.contains('\n'),"Initial registry URL invalid");initialReadHash(initialReadString(p,"sha256"));initialReadHash(digest);
    const auto date=QDateTime::fromString(observed,Qt::ISODateWithMs);initialReadRequire(date.isValid()&&sourceManifestUtc(observed)&&date.toUTC().toString(Qt::ISODateWithMs)==observed,"Initial registry timestamp is not exact canonical milliseconds");
    QJsonArray input{request,device.toLower()};for(const auto& field:fields)input.append(s.value(field));input.append(observation);input.append(digest);
    const auto fingerprint=initialReadString(o,"input_fingerprint");initialReadHash(fingerprint);initialReadRequire(QCryptographicHash::hash(QJsonDocument(input).toJson(QJsonDocument::Compact)+'\n',QCryptographicHash::Sha256).toHex()==fingerprint.toLatin1(),"Initial receipt reconstructed fingerprint differs");return r;
}
Booking row(const Statement& q){return {q.text(0),q.text(1),q.text(2),q.text(3),q.text(4),q.text(5),q.text(6)};}
QVector<QPair<QString,QString>> selectedBlocks(const QString& a,const QString& b,const QString& za,const QString& zb){
    const auto first=canonicalUnsigned(a),last=canonicalUnsigned(b),zoneFirst=canonicalUnsigned(za),zoneLast=canonicalUnsigned(zb);
    if(compareUnsigned(zoneFirst,first)>0||compareUnsigned(first,last)>=0||compareUnsigned(last,zoneLast)>0)
        throw std::runtime_error("Selection must be inside one exact zone");
    const auto step=divideUnsignedSmall(subtractUnsigned(zoneLast,zoneFirst),100);
    if(step=="0")throw std::runtime_error("Zone is too small for 100 blocks");
    QVector<QPair<QString,QString>> result;auto cursor=zoneFirst;bool startAligned=false,endAligned=false;
    for(int i=0;i<100;++i){const auto next=i==99?zoneLast:addUnsigned(cursor,step);
        if(cursor==first)startAligned=true;if(next==last)endAligned=true;
        if(compareUnsigned(cursor,first)>=0&&compareUnsigned(next,last)<=0)result.append({cursor,next});cursor=next;
    }
    if(!startAligned||!endAligned||result.isEmpty())throw std::runtime_error("Select exact complete blocks, not part of a block");
    return result;
}
bool occupied(sqlite3* db,const QString& first,const QString& last){
    Statement q(db,"SELECT 1 FROM bookings WHERE state IN('QUEUED','ARMED','DISPATCHING','DISPATCHED','ACTIVE','PAUSED','SOURCE_FINISHED') AND start_key<? AND end_key>? LIMIT 1");
    q.text(1,key(last));q.text(2,key(first));return q.step()==SQLITE_ROW;
}
QVector<QPair<QString,QString>> exactSelection(const QVector<ExactBlockSelection>& selection){
    if(selection.isEmpty()||selection.size()>4096)throw std::runtime_error("Select between1 and4096 exact intervals");
    QVector<QPair<QString,QString>> blocks;QSet<QString> seen;
    for(const auto& span:selection)for(const auto& block:selectedBlocks(span.start,span.end,span.zoneStart,span.zoneEnd)){
        const auto identity=block.first+"/"+block.second;if(seen.contains(identity))continue;seen.insert(identity);blocks.append(block);
        if(blocks.size()>4096)throw std::runtime_error("4096 selected blocks supported; entire selection unchanged");
    }
    std::sort(blocks.begin(),blocks.end(),[](const auto& a,const auto& b){return compareUnsigned(a.first,b.first)<0;});
    for(int i=1;i<blocks.size();++i)if(compareUnsigned(blocks[i-1].second,blocks[i].first)>0)throw std::runtime_error("Conflicting block grids; entire selection unchanged");return blocks;
}
bool nextTable(sqlite3* db,const char* name){
    Statement q(db,"SELECT type FROM sqlite_master WHERE name=?");q.text(1,QString::fromLatin1(name));if(q.step()!=SQLITE_ROW)return false;
    initialReadRequire(exactMetadataText(q,0)=="table"&&q.step()==SQLITE_DONE,"Continuation storage object is not an exact table");return true;
}
QString nextWindow(const QString& n){
    const QString divisor="18446744073709551616";QString remainder="0",quotient;
    for(const auto digit:n){remainder=canonicalUnsigned(remainder+digit);int q=0;while(compareUnsigned(remainder,divisor)>=0){remainder=subtractUnsigned(remainder,divisor);++q;}quotient+=QChar('0'+q);}return canonicalUnsigned(quotient);
}
void nextFinite(const QString& a,const QString& b,const QString& capacity){
    initialReadRequire(canonicalUnsigned(a)==a&&canonicalUnsigned(b)==b&&compareUnsigned(a,b)<0&&
        compareUnsigned(subtractUnsigned(b,a),capacity)<=0&&nextWindow(a)==nextWindow(subtractUnsigned(b,"1")),
        "Continuation leaves finite capacity/64-bit window; no block skipped or padded");
}
InitialStartDispatch nextDispatch(sqlite3* db,const QString& id){
    Statement q(db,"SELECT id,start,end,device,state,predecessor,run FROM dispatch_requests WHERE id=? OR id=CAST(? AS BLOB) LIMIT 2");q.text(1,id);q.text(2,id);
    initialReadRequire(q.step()==SQLITE_ROW,"Continuation dispatch missing");auto d=initialDispatch(q);
    initialReadRequire(d.bookingId==id&&exactMetadataText(q,3)==d.device&&q.step()==SQLITE_DONE,"Continuation dispatch ambiguous/noncanonical device");return d;
}
QString nextLocal(sqlite3* db,const InitialStartDispatch& d){
    Statement q(db,"SELECT start,end,start_key,end_key,device,kind,state FROM bookings WHERE id=? OR id=CAST(? AS BLOB) LIMIT 2");q.text(1,d.bookingId);q.text(2,d.bookingId);
    initialReadRequire(q.step()==SQLITE_ROW&&exactMetadataText(q,0)==d.start&&exactMetadataText(q,1)==d.end&&exactMetadataText(q,2)==key(d.start)&&exactMetadataText(q,3)==key(d.end)&&exactMetadataText(q,4)==d.device&&exactMetadataText(q,5)=="LOCAL_INTENT","Continuation LOCAL identity/range/keys differ");
    const auto state=exactMetadataText(q,6);initialReadRequire(q.step()==SQLITE_DONE,"Continuation LOCAL identity ambiguous");return state;
}
SourceManifestRecord nextSource(sqlite3* db,const QString& device,const NextBoundarySource& base){
    requireResumeRun(base.run);initialReadRequire(sourceManifestUtc(base.birth)&&base.start.size()<=160&&base.end.size()<=160&&canonicalUnsigned(base.start)==base.start&&canonicalUnsigned(base.end)==base.end&&compareUnsigned(base.start,base.end)<0,"Continuation predecessor base invalid");
    initialReadRequire(base.queuedIntent.has_value(),"Legacy SOURCE predecessor has no enrolled queued origin; held, not adopted");requireResumeUuid(*base.queuedIntent);
    sourceManifestStorageTypes(db);const auto actual=storedSourceManifest(db,device,base.run);
    initialReadRequire(actual&&actual->bookingId==*base.queuedIntent&&actual->start==base.start&&actual->end==base.end&&actual->manifest.value("birth")==base.birth,"Continuation predecessor differs from immutable actual manifest");sourceManifestChain(db,*actual);
    const auto d=nextDispatch(db,*base.queuedIntent);initialReadRequire(d.state=="SOURCE_FINISHED"&&d.run==base.run&&nextLocal(db,d)=="SOURCE_FINISHED","Continuation predecessor SOURCE/LOCAL/dispatch not settled");
    Statement q(db,"SELECT state FROM bookings WHERE id=? AND kind='RUNTIME'");q.text(1,base.run);initialReadRequire(q.step()==SQLITE_ROW&&exactMetadataText(q,0)=="SOURCE_FINISHED"&&q.step()==SQLITE_DONE,"Continuation predecessor physical SOURCE not committed");return *actual;
}
QString nextClaimId(const QString& predecessor,const QString& a,const QString& b,const QString& device){
    auto bytes=QCryptographicHash::hash(("pgn-local-dispatch-claim-v1/"+predecessor+"/"+a+"/"+b+"/"+device).toUtf8(),QCryptographicHash::Sha256).left(16);
    bytes[6]=char((uchar(bytes[6])&15)|0x80);bytes[8]=char((uchar(bytes[8])&63)|0x80);return QUuid::fromRfc4122(bytes).toString(QUuid::WithoutBraces);
}
bool nextAutomatic(sqlite3* db,const InitialStartDispatch& d){
    if(!nextTable(db,"automatic_claims"))return false;
    Statement q(db,"SELECT booking_id,predecessor,created_utc FROM automatic_claims WHERE booking_id=? OR booking_id=CAST(? AS BLOB) LIMIT 2");q.text(1,d.bookingId);q.text(2,d.bookingId);if(q.step()!=SQLITE_ROW)return false;
    initialReadRequire(d.predecessor&&exactMetadataText(q,0)==d.bookingId&&exactMetadataText(q,1)==*d.predecessor&&!exactMetadataText(q,2).isEmpty()&&q.step()==SQLITE_DONE,"Automatic continuation origin contradicts immutable predecessor");
    initialReadRequire(d.bookingId==nextClaimId(*d.predecessor,d.start,d.end,d.device),"Automatic continuation UUID no longer binds exact predecessor/range/device");return true;
}
void nextLegacy(sqlite3* db,const QString& device,bool adoptions){
    // Validate BEFORE cleanup. Releasing a corrupt/foreign row must not hide
    // the first inconsistency from the subsequent occupied-range scan.
    Statement q(db,"SELECT current_run,booking_id,created_utc FROM automatic_next");
    while(q.step()==SQLITE_ROW){const auto run=exactMetadataText(q,0),id=exactMetadataText(q,1),created=exactMetadataText(q,2);requireResumeRun(run);requireResumeUuid(id);initialReadRequire(!created.isEmpty()&&created.size()<=4096&&!created.contains(QChar(0)),"Legacy automatic date/type unknown");
        Statement b(db,"SELECT start,end,start_key,end_key,kind,device,state FROM bookings WHERE id=? OR id=CAST(? AS BLOB) LIMIT 2");b.text(1,id);b.text(2,id);initialReadRequire(b.step()==SQLITE_ROW,"Legacy automatic booking absent");const auto a=exactMetadataText(b,0),end=exactMetadataText(b,1),state=exactMetadataText(b,6);
        initialReadRequire(a.size()<=160&&end.size()<=160&&canonicalUnsigned(a)==a&&canonicalUnsigned(end)==end&&compareUnsigned(a,end)<0&&exactMetadataText(b,2)==key(a)&&exactMetadataText(b,3)==key(end)&&exactMetadataText(b,4)=="LOCAL_INTENT"&&exactMetadataText(b,5)==device&&(state=="QUEUED"||state=="ARMED"||state=="DISPATCHING"||state=="DISPATCHED"||state=="SOURCE_FINISHED"||state=="RELEASED")&&b.step()==SQLITE_DONE,"Legacy automatic identity/device/kind/keys/state corrupt; unchanged");
    }
    if(adoptions){Statement q(db,"SELECT booking_id,created_utc FROM automatic_adoptions");while(q.step()==SQLITE_ROW){const auto id=exactMetadataText(q,0),created=exactMetadataText(q,1);requireResumeUuid(id);initialReadRequire(!created.isEmpty()&&created.size()<=4096&&!created.contains(QChar(0)),"Legacy automatic adoption date/type unknown");Statement origin(db,"SELECT booking_id FROM automatic_next WHERE booking_id=?");origin.text(1,id);initialReadRequire(origin.step()==SQLITE_ROW&&exactMetadataText(origin,0)==id&&origin.step()==SQLITE_DONE,"Legacy adoption lost its exact automatic origin");}}
}
void nextOrder(sqlite3* db){
    const auto mode=initialReadMetadata(db,"queue-order-mode",16);initialReadRequire(!mode||*mode=="numeric"||*mode=="manual","Continuation queue order mode unknown");
    Statement q(db,"SELECT zone_start,zone_end,start_key,end_key,priority FROM queue_zone_order");QVector<QPair<QString,QString>> zones;
    while(q.step()==SQLITE_ROW){const auto a=exactMetadataText(q,0),b=exactMetadataText(q,1);initialReadRequire(a.size()<=160&&b.size()<=160&&canonicalUnsigned(a)==a&&canonicalUnsigned(b)==b&&compareUnsigned(a,b)<0&&exactMetadataText(q,2)==key(a)&&exactMetadataText(q,3)==key(b)&&sqlite3_column_type(q.value,4)==SQLITE_INTEGER&&sqlite3_column_int64(q.value,4)>=0,"Continuation zone order corrupt");zones.append({a,b});}
    std::sort(zones.begin(),zones.end(),[](const auto& a,const auto& b){return compareUnsigned(a.first,b.first)<0;});for(int i=1;i<zones.size();++i)initialReadRequire(compareUnsigned(zones[i-1].second,zones[i].first)<=0,"Continuation zone order overlaps");
}
bool retiredAbortedRuntime(const QString& id,const QString& state,const QString& kind,const QString& device){
    if(state!="ABORTED")return false;
    if(kind!="RUNTIME")throw std::runtime_error("ABORTED is only a retired physical runtime, never a local intent");
    requireResumeRun(id);sessionDevice(device);
    // Existing ingestion persists this terminal state. It grants no coverage,
    // completion, resume or dispatch capability and must not block ALL work.
    return true;
}
struct NextScan {QVector<Booking> rows;quint64 count{};};
NextScan nextScan(sqlite3* db,const QString& device,const QString& allowed={}){
    // Full local integrity/occupation accounting is paid once on a new claim
    // or submission readback, NOT on a UI poll/pending reconciliation. Raw
    // source bytes are never read here. Historical ranges remain occupied.
    Statement dispatch(db,"SELECT id,start,end,device,state,predecessor,run FROM dispatch_requests");while(dispatch.step()==SQLITE_ROW){const auto d=initialDispatch(dispatch);sessionDevice(exactMetadataText(dispatch,3));if(d.device==device&&d.state=="STARTED"&&d.bookingId!=allowed)throw std::runtime_error("Unreconciled STARTED dispatch holds continuation");}
    NextScan result;CoveredPausedAncestors coverage(db);Statement q(db,"SELECT id,start,end,state,kind,device,created_utc,start_key,end_key FROM bookings ORDER BY start_key,end_key");
    while(q.step()==SQLITE_ROW){Booking b{exactMetadataText(q,0),exactMetadataText(q,1),exactMetadataText(q,2),exactMetadataText(q,3),exactMetadataText(q,4),exactMetadataText(q,5),exactMetadataText(q,6)};
        initialReadRequire(!b.id.isEmpty()&&!b.id.contains(QChar(0))&&b.start.size()<=160&&b.end.size()<=160&&canonicalUnsigned(b.start)==b.start&&canonicalUnsigned(b.end)==b.end&&compareUnsigned(b.start,b.end)<0&&exactMetadataText(q,7)==key(b.start)&&exactMetadataText(q,8)==key(b.end),"Continuation stored range/identity/keys corrupt");
        if(retiredAbortedRuntime(b.id,b.state,b.kind,b.device))continue;
        initialReadRequire((b.kind=="LOCAL_INTENT"||b.kind=="RUNTIME")&&(b.state=="QUEUED"||b.state=="ARMED"||b.state=="DISPATCHING"||b.state=="DISPATCHED"||b.state=="ACTIVE"||b.state=="PAUSED"||b.state=="SOURCE_FINISHED"||b.state=="RELEASED"),"Continuation stored kind/state unknown");const auto normalized=sessionDevice(b.device);
        if(b.state=="RELEASED")continue;const bool covered=b.state=="PAUSED"&&b.kind=="RUNTIME"&&coverage.contains(b.id,b.device);if(b.state!="SOURCE_FINISHED"&&!covered)++result.count;
        if(normalized==sessionDevice(device)&&b.id!=allowed&&(b.state=="ACTIVE"||b.state=="PAUSED"||b.state=="DISPATCHING"||b.state=="DISPATCHED"))initialReadRequire(covered,"Existing physical/interrupted work holds continuation");result.rows.append(b);
    }return result;
}
void nextPrepared(sqlite3* db,InitialStartDispatch& d,const QString& device,const NextBoundarySource& base){
    requireResumeUuid(d.bookingId);initialReadRequire(d.device==device&&d.state=="PREPARED"&&!d.run&&d.predecessor==base.run&&nextLocal(db,d)=="DISPATCHING","Continuation requires exact PREPARED/NULL run/SOURCE predecessor/LOCAL tuple");
    Statement pending(db,"SELECT id FROM dispatch_requests WHERE lower(device)=? AND state='PREPARED' LIMIT 2");pending.text(1,sessionDevice(device));initialReadRequire(pending.step()==SQLITE_ROW&&exactMetadataText(pending,0)==d.bookingId&&pending.step()==SQLITE_DONE,"Continuation is not sole pending dispatch");d.origin="QUEUED_ADVANCE";d.automatic=nextAutomatic(db,d);
    initialReadRequire(*d.automatic||d.bookingId!=nextClaimId(*d.predecessor,d.start,d.end,d.device),"Deterministic automatic continuation lost its immutable origin");
}
}
class BookingStore::Impl {
public:
    sqlite3* db{};QString error;
    bool dispatchAvailable{};
    QByteArray visibleJobFilter(){
        if(!dispatchAvailable){Statement table(db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='dispatch_requests'");dispatchAvailable=table.step()==SQLITE_ROW;}
        if(!dispatchAvailable)return {};
        // The planned intent and physical run retain distinct immutable audit
        // identities, but their exact bound/device link is ONE visible job.
        // Do not hide a claim until the actual RUNTIME record exists.
        return " AND NOT (b.kind='LOCAL_INTENT' AND b.state IN('DISPATCHED','SOURCE_FINISHED') AND EXISTS(SELECT 1 FROM dispatch_requests d JOIN bookings r ON r.id=d.run AND r.kind='RUNTIME' WHERE d.id=b.id AND r.start=b.start AND r.end=b.end AND r.device=b.device)) ";
    }
    ~Impl(){if(db)sqlite3_close(db);}
    void execute(const char* sql){if(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    template<class Action>QString transaction(Action action){
        if(!db)return error;
        try{execute("BEGIN IMMEDIATE");action();execute("COMMIT");return {};}
        catch(const std::exception& e){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);return QString::fromUtf8(e.what());}
    }
    void insert(const Booking& b){
        Statement q(db,"INSERT INTO bookings(id,start,end,start_key,end_key,state,kind,device,created_utc) VALUES(?,?,?,?,?,?,?,?,?)");
        const QStringList values{b.id,b.start,b.end,key(b.start),key(b.end),b.state,b.kind,b.device,b.createdUtc};
        for(int i=0;i<values.size();++i)q.text(i+1,values[i]);q.step();
    }
    void runtime(const QString& run,const QString& a,const QString& b,const QString& device,const QString& state){
        // A delayed observation/completion of a retired partial run must not
        // reactivate it, pause its successor, or turn it into full coverage.
        if(retiredResumeRun(db,run))throw std::runtime_error("Retired partial run requires its current resume epoch; observation held");
        const auto first=canonicalUnsigned(a),last=canonicalUnsigned(b);
        static const QRegularExpression identity("^[0-9a-f]{32}$");
        if(compareUnsigned(first,last)>=0||!identity.match(run).hasMatch()||device.isEmpty())throw std::runtime_error("Physical job identity/range invalid");
        Statement existing(db,"SELECT start,end,device,kind,state FROM bookings WHERE id=?");existing.text(1,run);
        if(existing.step()==SQLITE_ROW){
            if(existing.text(0)!=first||existing.text(1)!=last||existing.text(2)!=device||existing.text(3)!="RUNTIME")throw std::runtime_error("Saved physical job identity mismatch");
            const auto previous=existing.text(4);
            if(previous=="SOURCE_FINISHED"||previous=="ABORTED")return;
            if(state=="ACTIVE"){
                // An admitted recovery is still the one physical owner of
                // this device. Preserve interrupted jobs as PAUSED, never
                // leave two ACTIVE rows or discard their event history.
                Statement pause(db,"UPDATE bookings SET state='PAUSED' WHERE kind='RUNTIME' AND device=? AND state='ACTIVE' AND id<>?");
                pause.text(1,device);pause.text(2,run);pause.step();
            }
            if(previous==state)return;
            Statement update(db,"UPDATE bookings SET state=? WHERE id=?");update.text(1,state);update.text(2,run);update.step();
        }else{
            // Missing completion cannot promote an interrupted run to coverage.
            if(state!="SOURCE_FINISHED"){
                Statement pause(db,"UPDATE bookings SET state='PAUSED' WHERE kind='RUNTIME' AND device=? AND state='ACTIVE'");pause.text(1,device);pause.step();
            }
            insert({run,first,last,state,"RUNTIME",device,utc()});
        }
    }
};
BookingStore::BookingStore(const QString& file):impl_(std::make_unique<Impl>()){
    try{
        if(!QDir().mkpath(QFileInfo(file).absolutePath()))throw std::runtime_error("Booking directory unavailable");
        const auto bytes=file.toUtf8();
        if(sqlite3_open_v2(bytes.constData(),&impl_->db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)
            throw std::runtime_error("Local booking database unavailable");
        sqlite3_busy_timeout(impl_->db,200);
        impl_->execute("PRAGMA journal_mode=WAL;PRAGMA synchronous=FULL;"
            "CREATE TABLE IF NOT EXISTS bookings(id TEXT PRIMARY KEY,start TEXT NOT NULL,end TEXT NOT NULL,start_key TEXT NOT NULL,end_key TEXT NOT NULL,state TEXT NOT NULL,kind TEXT NOT NULL,device TEXT NOT NULL,created_utc TEXT NOT NULL);"
            "CREATE INDEX IF NOT EXISTS active_ranges ON bookings(state,start_key,end_key);"
            "CREATE INDEX IF NOT EXISTS lifecycle_history ON bookings(created_utc,id);");
        impl_->execute("CREATE TABLE IF NOT EXISTS queue_zone_order(zone_start TEXT PRIMARY KEY,zone_end TEXT NOT NULL,start_key TEXT NOT NULL,end_key TEXT NOT NULL,priority INTEGER NOT NULL);");
        impl_->execute("CREATE TABLE IF NOT EXISTS booking_derivations(child_id TEXT PRIMARY KEY,parent_id TEXT NOT NULL,reason TEXT NOT NULL,created_utc TEXT NOT NULL);");
        // One transaction installs the audit boundary and snapshots pre-existing
        // latest states. Their lost earlier transitions are never fabricated.
        impl_->execute("BEGIN IMMEDIATE;"
            "CREATE TABLE IF NOT EXISTS booking_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS booking_events(sequence INTEGER PRIMARY KEY AUTOINCREMENT,booking_id TEXT NOT NULL,start TEXT NOT NULL,end TEXT NOT NULL,device TEXT NOT NULL,kind TEXT NOT NULL,from_state TEXT NOT NULL,to_state TEXT NOT NULL,event_kind TEXT NOT NULL,observed_utc TEXT NOT NULL);"
            "CREATE INDEX IF NOT EXISTS booking_event_history ON booking_events(booking_id,sequence);"
            "INSERT INTO booking_events(booking_id,start,end,device,kind,from_state,to_state,event_kind,observed_utc) SELECT id,start,end,device,kind,'',state,'BASELINE_CURRENT_STATE',strftime('%Y-%m-%dT%H:%M:%fZ','now') FROM bookings WHERE NOT EXISTS(SELECT 1 FROM booking_metadata WHERE key='lifecycle-audit-v1') ORDER BY created_utc,id;"
            "INSERT OR IGNORE INTO booking_metadata VALUES('lifecycle-audit-v1','installed');"
            "CREATE TRIGGER IF NOT EXISTS booking_created AFTER INSERT ON bookings BEGIN INSERT INTO booking_events(booking_id,start,end,device,kind,from_state,to_state,event_kind,observed_utc) VALUES(NEW.id,NEW.start,NEW.end,NEW.device,NEW.kind,'',NEW.state,'CREATED',strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;"
            "CREATE TRIGGER IF NOT EXISTS booking_state_changed AFTER UPDATE OF state ON bookings WHEN OLD.state<>NEW.state BEGIN INSERT INTO booking_events(booking_id,start,end,device,kind,from_state,to_state,event_kind,observed_utc) VALUES(NEW.id,NEW.start,NEW.end,NEW.device,NEW.kind,OLD.state,NEW.state,'STATE_CHANGED',strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;"
            "CREATE TRIGGER IF NOT EXISTS booking_event_no_update BEFORE UPDATE ON booking_events BEGIN SELECT RAISE(ABORT,'Booking event history is append-only'); END;"
            "CREATE TRIGGER IF NOT EXISTS booking_event_no_delete BEFORE DELETE ON booking_events BEGIN SELECT RAISE(ABORT,'Booking event history is append-only'); END;"
            "COMMIT;");
    }catch(const std::exception& e){impl_->error=QString::fromUtf8(e.what());if(impl_->db){sqlite3_close(impl_->db);impl_->db=nullptr;}}
}
BookingStore::~BookingStore()=default;
bool BookingStore::valid()const{return impl_->db;}
QString BookingStore::error()const{return impl_->error;}
std::optional<bool> BookingStore::hasPreparedDispatches()const{
    if(!impl_->db)return std::nullopt;
    try{
        Statement row(impl_->db,
            "SELECT EXISTS(SELECT 1 FROM sqlite_master WHERE type='table' AND name='dispatch_requests'),"
            "EXISTS(SELECT 1 FROM dispatch_requests WHERE typeof(state)<>'text' OR typeof(device)<>'text' "
            "OR state NOT IN('ARMED','PREPARED','STARTED','SOURCE_FINISHED','CANCELLED')),"
            "EXISTS(SELECT 1 FROM dispatch_requests WHERE state='PREPARED')");
        if(row.step()!=SQLITE_ROW||sqlite3_column_type(row.value,0)!=SQLITE_INTEGER||sqlite3_column_type(row.value,1)!=SQLITE_INTEGER||sqlite3_column_type(row.value,2)!=SQLITE_INTEGER||
           sqlite3_column_int(row.value,0)!=1||sqlite3_column_int(row.value,1)!=0)return std::nullopt;
        return sqlite3_column_int(row.value,2)!=0;
    }catch(const std::exception&){return std::nullopt;}
}
std::optional<MiningSessionMetadata> BookingStore::miningSessionMetadata(const QString& device) const {
    if(!impl_->db)return {};
    try{
        const auto normalized=sessionDevice(device),modeKey=sessionModeKey(normalized),preferenceKey=sessionPreferenceKey(normalized);
        // A single SQLite statement holds one snapshot across both keys. No
        // pair assembled from independently refreshed/defaulted observations.
        Statement query(impl_->db,"SELECT key,value FROM booking_metadata WHERE key IN(?,?)");query.text(1,modeKey);query.text(2,preferenceKey);
        MiningSessionMetadata result;
        while(query.step()==SQLITE_ROW){
            const auto key=exactMetadataText(query,0),value=exactMetadataText(query,1);
            if(key==preferenceKey){if(value=="0")result.automaticAfterManual=false;else if(value=="1")result.automaticAfterManual=true;else return {};}
            else if(key==modeKey){
                using pgn::mining::MiningSessionMode;
                if(value=="unselected")result.mode=MiningSessionMode::Unselected;
                else if(value=="automatic")result.mode=MiningSessionMode::Automatic;
                else if(value=="manual-reservations")result.mode=MiningSessionMode::ManualReservations;
                else return {};
            }else return {};
        }
        return result;
    }catch(const std::exception&){return {};}
}
std::optional<bool> BookingStore::automaticAfterManual(const QString& device) const {
    const auto metadata=miningSessionMetadata(device);if(!metadata)return {};return metadata->automaticAfterManual;
}
QString BookingStore::setAutomaticAfterManual(const QString& device,bool enabled){
    try{
        const auto key=sessionPreferenceKey(sessionDevice(device));
        return impl_->transaction([&]{Statement write(impl_->db,"INSERT INTO booking_metadata(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");write.text(1,key);write.text(2,enabled?"1":"0");write.step();});
    }catch(const std::exception& e){return QString::fromUtf8(e.what());}
}
QString BookingStore::setMiningSessionMode(const QString& device,pgn::mining::MiningSessionMode mode){
    try{
        const auto key=sessionModeKey(sessionDevice(device)),value=modeText(mode);
        return impl_->transaction([&]{Statement write(impl_->db,"INSERT INTO booking_metadata(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");write.text(1,key);write.text(2,value);write.step();});
    }catch(const std::exception& e){return QString::fromUtf8(e.what());}
}
QString BookingStore::saveResumeCheckpoint(const QString& device,const Resume& resume,const QByteArray& checkpointBytes){
    try{
        if(!impl_->db)throw std::runtime_error("Booking database unavailable; resume not saved");
        const auto normalized=sessionDevice(device),key=resumeKey(normalized,resume.run);
        const auto raw=encodeResume(normalized,resume,checkpointBytes);
        return impl_->transaction([&]{
            Statement previous(impl_->db,"SELECT value FROM booking_metadata WHERE key=?");previous.text(1,key);
            if(previous.step()==SQLITE_ROW){
                const auto stored=decodeResume(previous,normalized,resume.run);validateResumeAdvance(stored.resume,resume);
                if(encodeResume(normalized,stored.resume,stored.checkpointBytes)==raw)return; // no UPDATE for exact replay
            }
            if(retiredResumeRun(impl_->db,resume.run))throw std::runtime_error("Retired resume checkpoint is immutable");
            Statement write(impl_->db,"INSERT INTO booking_metadata(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
            write.text(1,key);write.text(2,QString::fromUtf8(raw));write.step();
        });
    }catch(const std::exception& e){return QString::fromUtf8(e.what());}
}
std::optional<StoredSourceResume> BookingStore::savedResumeCheckpoint(const QString& device,const QString& run) const {
    if(!impl_->db)return {};
    try{
        const auto normalized=sessionDevice(device),key=resumeKey(normalized,run);
        Statement query(impl_->db,"SELECT value FROM booking_metadata WHERE key=?");query.text(1,key);
        if(query.step()!=SQLITE_ROW)return {};return decodeResume(query,normalized,run);
    }catch(const std::exception&){return {};}
}
CheckpointResumeEpochRead BookingStore::savedCheckpointResumeEpoch(const QString& device,const QString& run) const {
    CheckpointResumeEpochRead result;
    try{
        if(!impl_->db)throw std::runtime_error("Resume epoch storage unavailable");
        const auto canonicalDevice="GPU-"+sessionDevice(device).mid(4);requireResumeRun(run);
        if(!localTable(impl_->db,"dispatch_resume_epochs")){result.status=CheckpointResumeReadStatus::Absent;return result;}
        Statement q(impl_->db,"SELECT run,previous_run,booking_id,request_id,receipt FROM dispatch_resume_epochs WHERE run=?");q.text(1,run);
        if(q.step()!=SQLITE_ROW){result.status=CheckpointResumeReadStatus::Absent;return result;}
        auto epoch=decodeResumeEpoch(q);
        if(epoch.device!=canonicalDevice)throw std::runtime_error("Resume epoch belongs to another device");
        result.receiptUtf8=exactMetadataText(q,4).toUtf8();result.epoch=std::move(epoch);result.status=CheckpointResumeReadStatus::Present;
    }catch(const std::exception& e){result.error=QString::fromUtf8(e.what()).left(512);}
    return result;
}
QString BookingStore::saveSourceRunManifest(const QString& device,const QJsonObject& actualManifest){
    try{
        if(!impl_->db)throw std::runtime_error("Source manifest storage unavailable");const auto observed=sourceManifest(device,actualManifest);
        return impl_->transaction([&]{
            sourceManifestStorageTypes(impl_->db);
            const auto existing=storedSourceManifest(impl_->db,device,observed.run);
            if(existing){
                manifestRequire(existing->raw==observed.raw,"Immutable actual source manifest bytes/keys/birth/markers changed");
                sourceManifestChain(impl_->db,*existing);return; // No UPDATE, even after retirement.
            }
            manifestRequire(!retiredResumeRun(impl_->db,observed.run),"Retired source epoch has no saved actual manifest; legacy origin cannot be adopted");
            if(observed.nativeSearchOrigin){
                Statement present(impl_->db,"SELECT 1 FROM bookings WHERE id=?");present.text(1,observed.run);
                if(present.step()==SQLITE_DONE)impl_->insert({observed.run,observed.start,observed.end,"PAUSED","RUNTIME",observed.device,observed.manifest.value("birth").toString()});
            }
            sourceManifestChain(impl_->db,observed);
            Statement save(impl_->db,"INSERT INTO booking_metadata(key,value) VALUES(?,?)");save.text(1,sourceManifestKey(observed.run));save.text(2,QString::fromUtf8(observed.raw));save.step();
            sourceManifestStorageTypes(impl_->db);
            const auto read=storedSourceManifest(impl_->db,device,observed.run);
            manifestRequire(read&&read->raw==observed.raw,"Actual source manifest post-insert readback differs");sourceManifestChain(impl_->db,*read);
        }).left(512);
    }catch(const std::exception& e){return QString::fromUtf8(e.what()).left(512);}
}
SourceRunManifestRead BookingStore::savedSourceRunManifest(const QString& device,const QString& run) const {
    SourceRunManifestRead result;bool begun=false;
    try{
        if(!impl_->db)throw std::runtime_error("Source manifest storage unavailable");sessionDevice(device);requireResumeRun(run);
        impl_->execute("BEGIN");begun=true;sourceManifestStorageTypes(impl_->db);const auto read=storedSourceManifest(impl_->db,device,run);
        if(!read){impl_->execute("COMMIT");begun=false;result.status=SourceRunManifestReadStatus::Absent;return result;}
        const auto blockStarted=sourceManifestChain(impl_->db,*read);impl_->execute("COMMIT");begun=false;
        result.blockStartedUtc=blockStarted;result.manifest=read->manifest;result.receiptUtf8=read->raw;result.status=SourceRunManifestReadStatus::Present;
    }catch(const std::exception& e){if(begun)sqlite3_exec(impl_->db,"ROLLBACK",nullptr,nullptr,nullptr);result.error=QString::fromUtf8(e.what()).left(512);}
    return result;
}
CheckpointResumeAdmission BookingStore::prepareCheckpointResume(const QString& request,const QString& device,
        const QString& bookingId,const QString& previousRun,const QString& run,
        const QString& expectedSha,const QString& observation,const QJsonObject& recoveryEvidence){
    CheckpointResumeAdmission result;
    try{
        if(!impl_->db)throw std::runtime_error("Resume admission storage unavailable");
        requireResumeUuid(request);requireResumeUuid(bookingId);requireResumeUuid(observation);requireResumeRun(previousRun);requireResumeRun(run);
        static const QRegularExpression sha("^[0-9a-f]{64}$");
        if(previousRun==run||expectedSha.size()!=64||!sha.match(expectedSha).hasMatch())throw std::runtime_error("Resume requires a NEW run and exact checkpoint SHA256");
        const auto normalized=sessionDevice(device),canonicalDevice="GPU-"+normalized.mid(4);
        const auto error=impl_->transaction([&]{
            if(!miningSessionMetadata(device))throw std::runtime_error("Resume session metadata unavailable");
            Statement initialCommand(impl_->db,"SELECT 1 FROM booking_metadata WHERE key=?");initialCommand.text(1,"mining-initial-start/v1/"+request);
            if(initialCommand.step()==SQLITE_ROW)throw std::runtime_error("Resume request UUID already identifies INITIAL_START");
            if(!localTable(impl_->db,"dispatch_requests"))throw std::runtime_error("Resume requires existing dispatch, not INITIAL_START");
            Statement checkpoint(impl_->db,"SELECT value FROM booking_metadata WHERE key=?");checkpoint.text(1,resumeKey(normalized,previousRun));
            if(checkpoint.step()!=SQLITE_ROW)throw std::runtime_error("Resume checkpoint missing");
            const auto stored=decodeResume(checkpoint,normalized,previousRun);
            if(stored.resume.checkpointSha256!=expectedSha)throw std::runtime_error("Resume checkpoint SHA changed; re-observation required");
            Statement dispatch(impl_->db,"SELECT id,start,end,device,state,predecessor,run FROM dispatch_requests WHERE id=?");dispatch.text(1,bookingId);
            if(dispatch.step()!=SQLITE_ROW)throw std::runtime_error("Resume dispatch missing");const auto d=initialDispatch(dispatch);validateDispatchBooking(impl_->db,d);
            if(d.device!=canonicalDevice||d.start!=stored.resume.start||d.end!=stored.resume.end)
                throw std::runtime_error("Resume checkpoint/booking/device bounds contradict");
            Statement job(impl_->db,"SELECT state,start_key,end_key FROM bookings WHERE id=?");job.text(1,bookingId);
            if(job.step()!=SQLITE_ROW||exactMetadataText(job,1)!=key(d.start)||exactMetadataText(job,2)!=key(d.end))throw std::runtime_error("Resume local intent keys invalid");
            const auto jobState=exactMetadataText(job,0);
            Statement physical(impl_->db,"SELECT start,end,device,kind,state,start_key,end_key FROM bookings WHERE id=?");physical.text(1,previousRun);
            if(physical.step()!=SQLITE_ROW||exactMetadataText(physical,0)!=d.start||exactMetadataText(physical,1)!=d.end||
               exactMetadataText(physical,2)!=canonicalDevice||exactMetadataText(physical,3)!="RUNTIME"||exactMetadataText(physical,4)!="PAUSED"||
               exactMetadataText(physical,5)!=key(d.start)||exactMetadataText(physical,6)!=key(d.end))
                throw std::runtime_error("Resume requires exact old RUNTIME PAUSED, never finished/inferred");
            CheckpointResumeEpoch candidate{request,canonicalDevice,bookingId,previousRun,run,observation,d.predecessor,stored};
            candidate.recoveryEvidence=recoveryEvidence;
            const auto raw=encodeResumeEpoch(candidate);
            const bool hasEpochs=localTable(impl_->db,"dispatch_resume_epochs");
            if(hasEpochs){
                Statement replay(impl_->db,"SELECT run,previous_run,booking_id,request_id,receipt FROM dispatch_resume_epochs WHERE request_id=?");replay.text(1,request);
                if(replay.step()==SQLITE_ROW){
                    const auto epoch=decodeResumeEpoch(replay);
                    if(encodeResumeEpoch(epoch)!=raw||!d.run||*d.run!=run||
                       (d.state!="PREPARED"&&d.state!="STARTED"&&d.state!="SOURCE_FINISHED")||
                       (jobState!="DISPATCHING"&&jobState!="DISPATCHED"&&jobState!="SOURCE_FINISHED"))
                        throw std::runtime_error("Resume request replay conflicts or epoch is no longer current");
                    result.status=CheckpointResumeAdmissionStatus::ReconcilePrepared;result.epoch=epoch;result.receiptUtf8=raw;result.replayed=true;return;
                }
                Statement conflict(impl_->db,"SELECT 1 FROM dispatch_resume_epochs WHERE run=? OR previous_run=? LIMIT 1");conflict.text(1,run);conflict.text(2,previousRun);
                if(conflict.step()==SQLITE_ROW)throw std::runtime_error("Resume run already reserved or old epoch already has a successor");
                // Only an immediate local baseline comparison, not proof that
                // the producer loaded a checkpoint or conserved every GAP row.
                Statement prior(impl_->db,"SELECT run,previous_run,booking_id,request_id,receipt FROM dispatch_resume_epochs WHERE run=?");prior.text(1,previousRun);
                if(prior.step()==SQLITE_ROW){const auto old=decodeResumeEpoch(prior);const auto& baseline=old.checkpoint.resume;
                    if(old.device!=canonicalDevice||old.bookingId!=bookingId||baseline.start!=stored.resume.start||baseline.end!=stored.resume.end)
                        throw std::runtime_error("Resume lineage booking/range mismatch");
                    for(const auto pair:{qMakePair(baseline.positions,stored.resume.positions),qMakePair(baseline.tests,stored.resume.tests),qMakePair(baseline.gaps,stored.resume.gaps),qMakePair(baseline.batches,stored.resume.batches)})
                        if(compareUnsigned(pair.first,pair.second)>0)throw std::runtime_error("Resume cumulative checkpoint counters regress");
                }
            }
            if(d.state!="STARTED"||!d.run||*d.run!=previousRun||jobState!="DISPATCHED")
                throw std::runtime_error("Resume requires exact current STARTED/old run; pending epochs require reconciliation");
            Statement pending(impl_->db,"SELECT 1 FROM dispatch_requests WHERE lower(device)=? AND state='PREPARED' LIMIT 1");pending.text(1,normalized);
            if(pending.step()==SQLITE_ROW)throw std::runtime_error("Existing PREPARED epoch must reconcile before resume");
            Statement active(impl_->db,"SELECT 1 FROM bookings WHERE lower(device)=? AND kind='RUNTIME' AND state='ACTIVE' LIMIT 1");active.text(1,normalized);
            if(active.step()==SQLITE_ROW)throw std::runtime_error("Active runtime contradicts stopped resume admission");
            Statement reused(impl_->db,"SELECT 1 FROM bookings WHERE id=? UNION ALL SELECT 1 FROM dispatch_requests WHERE run=? LIMIT 1");reused.text(1,run);reused.text(2,run);
            if(reused.step()==SQLITE_ROW)throw std::runtime_error("Resume NEW run was already used");
            // DDL is inside the successful transaction: any failure removes it
            // too on a legacy DB, rather than partially enrolling that DB.
            impl_->execute("CREATE TABLE IF NOT EXISTS dispatch_resume_epochs(run TEXT PRIMARY KEY,previous_run TEXT NOT NULL UNIQUE,booking_id TEXT NOT NULL,request_id TEXT NOT NULL UNIQUE,receipt TEXT NOT NULL);");
            Statement append(impl_->db,"INSERT INTO dispatch_resume_epochs(run,previous_run,booking_id,request_id,receipt) VALUES(?,?,?,?,?)");
            append.text(1,run);append.text(2,previousRun);append.text(3,bookingId);append.text(4,request);append.text(5,QString::fromUtf8(raw));append.step();
            Statement move(impl_->db,"UPDATE dispatch_requests SET state='PREPARED',run=? WHERE id=? AND state='STARTED' AND run=?");move.text(1,run);move.text(2,bookingId);move.text(3,previousRun);move.step();
            if(sqlite3_changes(impl_->db)!=1)throw std::runtime_error("Resume dispatch compare-and-swap failed");
            Statement stage(impl_->db,"UPDATE bookings SET state='DISPATCHING' WHERE id=? AND state='DISPATCHED'");stage.text(1,bookingId);stage.step();
            if(sqlite3_changes(impl_->db)!=1)throw std::runtime_error("Resume local intent compare-and-swap failed");
            result.status=CheckpointResumeAdmissionStatus::Prepared;result.epoch=std::move(candidate);result.receiptUtf8=raw;
        });
        if(!error.isEmpty()){result={};result.error=error.left(512);}
    }catch(const std::exception& e){result={};result.error=QString::fromUtf8(e.what()).left(512);}
    return result;
}
InitialStartResult BookingStore::prepareInitialStart(const QString& request,const QString& device,const InitialStartScope& scope,const SourceRegistry& source,const InitialBoundaryEvidence& proof){
    InitialStartResult result;
    if(proof.boundary==pgn::mining::WorkBoundary::Interrupted){result.status=InitialStartStatus::ResumeRequired;result.error="Interrupted work requires explicit resume/reconciliation; no initial allocation";return result;}
    if(proof.boundary!=pgn::mining::WorkBoundary::Ready){result.error="Initial start requires caller-declared Ready, not Running/Unknown/Completed";return result;}
    try{
        if(!impl_->db)throw std::runtime_error("Booking database unavailable");
        const auto normalized=sessionDevice(device),canonicalDevice="GPU-"+normalized.mid(4);
        QString registryDigest;const auto fingerprint=initialStartFingerprint(request,normalized,scope,source,proof,&registryDigest),receiptKey="mining-initial-start/v1/"+request;
        const QJsonObject scopeJson{{"execution_start",scope.executionStart},{"execution_end",scope.executionEnd},{"auto_start",scope.autoStart},{"auto_end",scope.autoEnd},{"block_anchor",scope.blockAnchor},{"block_width",scope.blockWidth}};
        const QJsonObject provenance{{"url",source.url},{"sha256",source.sha256},{"observed_utc",source.observedUtc.toUTC().toString(Qt::ISODateWithMs)},{"semantic_digest",registryDigest}};
        const auto error=impl_->transaction([&]{
            const auto metadata=miningSessionMetadata(normalized);if(!metadata)throw std::runtime_error("Session metadata unknown/corrupt");
            if(localTable(impl_->db,"dispatch_resume_epochs")){Statement reused(impl_->db,"SELECT 1 FROM dispatch_resume_epochs WHERE request_id=?");reused.text(1,request);if(reused.step()==SQLITE_ROW)throw std::runtime_error("INITIAL_START request UUID already identifies checkpoint resume");}
            Statement order(impl_->db,"SELECT value FROM booking_metadata WHERE key='queue-order-mode'");
            if(order.step()==SQLITE_ROW){const auto v=exactMetadataText(order,0);if(v!="manual"&&v!="numeric")throw std::runtime_error("Queue order metadata unknown");}
            Statement exists(impl_->db,"SELECT type FROM sqlite_master WHERE name='dispatch_requests'");const bool hasDispatch=exists.step()==SQLITE_ROW;
            if(hasDispatch&&exists.text(0)!="table")throw std::runtime_error("Dispatch storage is not a table");
            Statement replay(impl_->db,"SELECT value FROM booking_metadata WHERE key=?");replay.text(1,receiptKey);
            if(replay.step()==SQLITE_ROW){
                if(!hasDispatch||sqlite3_column_type(replay.value,0)!=SQLITE_TEXT||sqlite3_column_bytes(replay.value,0)>8192)throw std::runtime_error("Initial receipt storage unavailable/type/size invalid");
                const auto raw=exactMetadataText(replay,0).toUtf8();QJsonParseError parse;const auto doc=QJsonDocument::fromJson(raw,&parse);const auto o=doc.object();
                if(parse.error!=QJsonParseError::NoError||!doc.isObject()||doc.toJson(QJsonDocument::Compact)!=raw||o.size()!=12)throw std::runtime_error("Initial receipt JSON corrupt/noncanonical");
                for(const auto& name:QStringList{"schema","request_id","device","input_fingerprint","observation_id","mode","booking_id","start","end","origin"})if(!o.value(name).isString())throw std::runtime_error("Initial receipt fields must be exact strings");
                if(o.value("schema")!="pgn-initial-start-receipt-1"||o.value("origin")!="INITIAL_START"||o.value("request_id")!=request||o.value("device")!=canonicalDevice||o.value("input_fingerprint")!=fingerprint||o.value("observation_id")!=proof.observationUUID||o.value("scope")!=scopeJson||o.value("registry_provenance")!=provenance)
                    throw std::runtime_error("Initial request UUID cannot be rebound to different inputs/provenance");
                Statement q(impl_->db,"SELECT id,start,end,device,state,predecessor,run FROM dispatch_requests WHERE id=?");q.text(1,o.value("booking_id").toString());
                if(q.step()!=SQLITE_ROW)throw std::runtime_error("Initial receipt dispatch missing");auto d=initialDispatch(q);validateDispatchBooking(impl_->db,d);
                if(d.predecessor||d.start!=o.value("start").toString()||d.end!=o.value("end").toString()||d.device!=canonicalDevice)throw std::runtime_error("Initial receipt dispatch rebound");
                const auto mode=o.value("mode").toString();if(mode!="automatic"&&mode!="manual-reservations")throw std::runtime_error("Initial receipt mode unknown");
                d.origin="INITIAL_START";d.automatic=mode=="automatic";result.dispatch=d;result.mode=*d.automatic?pgn::mining::MiningSessionMode::Automatic:pgn::mining::MiningSessionMode::ManualReservations;
                result.status=InitialStartStatus::ReconcilePrepared;result.replayed=true;return; // NO UPDATE, no new launch token.
            }
            if(hasDispatch){
                Statement invalid(impl_->db,"SELECT 1 FROM dispatch_requests WHERE typeof(state)<>'text' OR typeof(device)<>'text' OR state NOT IN('ARMED','PREPARED','STARTED','SOURCE_FINISHED','CANCELLED') LIMIT 1");if(invalid.step()==SQLITE_ROW)throw std::runtime_error("Dispatch metadata types/state unknown");
                Statement pending(impl_->db,"SELECT id,start,end,device,state,predecessor,run FROM dispatch_requests WHERE lower(device)=? AND state='PREPARED' ORDER BY created_utc,id LIMIT 2");pending.text(1,normalized);
                if(pending.step()==SQLITE_ROW){auto d=initialDispatch(pending);validateDispatchBooking(impl_->db,d);if(pending.step()==SQLITE_ROW)throw std::runtime_error("Multiple pending dispatches require reconciliation");
                    d.origin="EXISTING_PENDING_RECONCILIATION";result.dispatch=d;result.mode=metadata->mode;result.status=InitialStartStatus::ReconcilePrepared;return;}
            }
            CoveredPausedAncestors coverage(impl_->db);
            Statement active(impl_->db,"SELECT id,state,kind,device FROM bookings WHERE lower(device)=? AND state IN('ACTIVE','PAUSED','DISPATCHING','DISPATCHED')");active.text(1,normalized);
            while(active.step()==SQLITE_ROW){const auto state=exactMetadataText(active,1);
                if(state=="PAUSED"&&exactMetadataText(active,2)=="RUNTIME"&&coverage.contains(exactMetadataText(active,0),exactMetadataText(active,3)))continue;
                result.status=state=="PAUSED"?InitialStartStatus::ResumeRequired:InitialStartStatus::Held;result.error="Existing physical/interrupted booking requires reconciliation, not INITIAL_START";return;
            }
            Statement zones(impl_->db,"SELECT zone_start,zone_end,start_key,end_key,priority FROM queue_zone_order ORDER BY priority,zone_start");
            while(zones.step()==SQLITE_ROW){const auto a=exactMetadataText(zones,0),b=exactMetadataText(zones,1);if(canonicalUnsigned(a)!=a||canonicalUnsigned(b)!=b||compareUnsigned(a,b)>=0||exactMetadataText(zones,2)!=key(a)||exactMetadataText(zones,3)!=key(b)||sqlite3_column_type(zones.value,4)!=SQLITE_INTEGER||sqlite3_column_int64(zones.value,4)<0)throw std::runtime_error("Queue zone order corrupt");}
            const auto manualSql=QByteArray("SELECT b.id,b.start,b.end,b.state,b.kind,b.device,b.created_utc FROM bookings b LEFT JOIN queue_zone_order z ON b.start_key>=z.start_key AND b.end_key<=z.end_key WHERE b.kind='LOCAL_INTENT' AND lower(b.device)=? AND b.state IN('QUEUED','ARMED') ORDER BY (z.priority IS NULL),z.priority,b.start_key,b.end_key,b.id LIMIT 1");
            Statement head(impl_->db,manualSql.constData());head.text(1,normalized);std::optional<Booking> manual;if(head.step()==SQLITE_ROW)manual=row(head);
            QVector<QPair<QString,QString>> occupiedRanges;quint64 activeCount=0;
            Statement all(impl_->db,"SELECT id,start,end,state,start_key,end_key,kind,device FROM bookings ORDER BY start_key,end_key");
            while(all.step()==SQLITE_ROW){const auto id=exactMetadataText(all,0),a=exactMetadataText(all,1),b=exactMetadataText(all,2),state=exactMetadataText(all,3);
                if(canonicalUnsigned(a)!=a||canonicalUnsigned(b)!=b||compareUnsigned(a,b)>=0||exactMetadataText(all,4)!=key(a)||exactMetadataText(all,5)!=key(b))throw std::runtime_error("Stored booking range/key invalid");
                if(retiredAbortedRuntime(id,state,exactMetadataText(all,6),exactMetadataText(all,7)))continue;
                if(state!="QUEUED"&&state!="ARMED"&&state!="DISPATCHING"&&state!="DISPATCHED"&&state!="ACTIVE"&&state!="PAUSED"&&state!="SOURCE_FINISHED"&&state!="RELEASED")throw std::runtime_error("Stored booking state unknown");
                if(state=="RELEASED")continue;
                if(state!="SOURCE_FINISHED"&&!(state=="PAUSED"&&exactMetadataText(all,6)=="RUNTIME"&&coverage.contains(id,exactMetadataText(all,7))))++activeCount;
                if(manual&&id==manual->id)continue;occupiedRanges.append({a,b});
            }
            InitialStartDispatch d;d.device=canonicalDevice;d.state="PREPARED";d.origin="INITIAL_START";d.automatic=!manual;
            if(manual){
                d.bookingId=manual->id;d.start=manual->start;d.end=manual->end;
                if(hasDispatch){Statement existing(impl_->db,"SELECT id,start,end,device,state,predecessor,run FROM dispatch_requests WHERE id=?");existing.text(1,d.bookingId);
                    if(existing.step()==SQLITE_ROW){const auto armed=initialDispatch(existing);if(armed.state!="ARMED"||armed.predecessor||armed.run||armed.start!=d.start||armed.end!=d.end||armed.device!=canonicalDevice)throw std::runtime_error("Manual head dispatch contradicts INITIAL_START; no skip/rebind");}}
                if(compareUnsigned(d.start,scope.executionStart)<0||compareUnsigned(d.end,scope.executionEnd)>0)throw std::runtime_error("First manual reservation is outside executionScope; not skipped or rebound");
                if(!source.declaredOpenContaining(d.start,d.end))throw std::runtime_error("First manual reservation no longer declared Open");
                for(const auto& span:occupiedRanges)if(overlaps(d.start,d.end,span.first,span.second))throw std::runtime_error("Manual head overlaps other stored/completed work");
            }else{
                if(activeCount>=4097)throw std::runtime_error("New automatic active entry exceeds the existing4097 operational bound");
                const auto candidate=firstInitialFreeBlock(scope,source,occupiedRanges);if(!candidate)throw std::runtime_error("No entire declared-Open free block in authorized scope");
                d.bookingId=QUuid::createUuid().toString(QUuid::WithoutBraces);d.start=candidate->first;d.end=candidate->second;
            }
            if(!hasDispatch)impl_->execute("CREATE TABLE dispatch_requests(id TEXT PRIMARY KEY,start TEXT NOT NULL,end TEXT NOT NULL,device TEXT NOT NULL,state TEXT NOT NULL,predecessor TEXT,run TEXT,created_utc TEXT NOT NULL);CREATE UNIQUE INDEX dispatch_run ON dispatch_requests(run) WHERE run IS NOT NULL;");
            const auto created=utc();if(!manual)impl_->insert({d.bookingId,d.start,d.end,"QUEUED","LOCAL_INTENT",canonicalDevice,created});
            Statement dispatch(impl_->db,"INSERT INTO dispatch_requests(id,start,end,device,state,predecessor,run,created_utc) VALUES(?,?,?,?,'PREPARED',NULL,NULL,?) ON CONFLICT(id) DO UPDATE SET state='PREPARED',predecessor=NULL WHERE dispatch_requests.state='ARMED' AND dispatch_requests.start=excluded.start AND dispatch_requests.end=excluded.end AND lower(dispatch_requests.device)=lower(excluded.device) AND dispatch_requests.run IS NULL AND dispatch_requests.predecessor IS NULL");
            dispatch.text(1,d.bookingId);dispatch.text(2,d.start);dispatch.text(3,d.end);dispatch.text(4,canonicalDevice);dispatch.text(5,created);dispatch.step();if(sqlite3_changes(impl_->db)!=1)throw std::runtime_error("Existing dispatch identity cannot be rebound");
            Statement claim(impl_->db,"UPDATE bookings SET state='DISPATCHING' WHERE id=? AND state IN('QUEUED','ARMED')");claim.text(1,d.bookingId);claim.step();if(sqlite3_changes(impl_->db)!=1)throw std::runtime_error("Manual/automatic booking changed while preparing");
            const auto mode=*d.automatic?QString("automatic"):QString("manual-reservations");Statement setMode(impl_->db,"INSERT INTO booking_metadata(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");setMode.text(1,sessionModeKey(normalized));setMode.text(2,mode);setMode.step();
            const QJsonObject receipt{{"schema","pgn-initial-start-receipt-1"},{"request_id",request},{"device",canonicalDevice},{"input_fingerprint",fingerprint},{"observation_id",proof.observationUUID},{"mode",mode},{"booking_id",d.bookingId},{"start",d.start},{"end",d.end},{"origin","INITIAL_START"},{"scope",scopeJson},{"registry_provenance",provenance}};
            if(QJsonDocument(receipt).toJson(QJsonDocument::Compact).size()>8192)throw std::runtime_error("Initial command receipt exceeds8KiB budget");
            Statement save(impl_->db,"INSERT INTO booking_metadata(key,value) VALUES(?,?)");save.text(1,receiptKey);save.text(2,QString::fromUtf8(QJsonDocument(receipt).toJson(QJsonDocument::Compact)));save.step();
            result.dispatch=d;result.mode=*d.automatic?pgn::mining::MiningSessionMode::Automatic:pgn::mining::MiningSessionMode::ManualReservations;result.status=InitialStartStatus::Prepared;
        });
        if(!error.isEmpty()){result={};result.error=error;}
    }catch(const std::exception& e){result={};result.error=QString::fromUtf8(e.what());}return result;
}
NextBoundaryAdmission BookingStore::prepareNextAtBoundary(const QString& device,const NextBoundarySource& base,
        const InitialStartScope& scope,const SourceRegistry& source,const NextBoundaryEvidence& proof){
    NextBoundaryAdmission result;
    try{
        initialReadRequire(impl_->db&&proof.boundary==pgn::mining::WorkBoundary::Ready,"Continuation requires caller-paid Ready SOURCE boundary, not Unknown/Running/interrupted");requireResumeUuid(proof.observationUUID);
        const auto normalized=sessionDevice(device),target="GPU-"+normalized.mid(4);
        const auto error=impl_->transaction([&]{
            initialReadMetadata(impl_->db,sessionModeKey(normalized),64);initialReadMetadata(impl_->db,sessionPreferenceKey(normalized),16);
            const auto metadata=miningSessionMetadata(target);initialReadRequire(metadata.has_value(),"Continuation session metadata unknown");result.mode=metadata->mode;nextSource(impl_->db,target,base);
            Statement pending(impl_->db,"SELECT id FROM dispatch_requests WHERE lower(device)=? AND state='PREPARED' ORDER BY created_utc,id LIMIT 2");pending.text(1,normalized);
            if(pending.step()==SQLITE_ROW){auto d=nextDispatch(impl_->db,exactMetadataText(pending,0));initialReadRequire(pending.step()==SQLITE_DONE,"Multiple pending dispatches require reconciliation");nextPrepared(impl_->db,d,target,base);result.dispatch=d;result.status=NextBoundaryStatus::ReconcilePrepared;return;}
            initialReadRequire(metadata->mode!=pgn::mining::MiningSessionMode::Unselected,"Explicit mining start required; empty queue never selects Auto");
            // Reuse exact scope/registry validation, not INITIAL_START mode or
            // receipt semantics. Pending reconciliation above does no search.
            firstInitialFreeBlock(scope,source,{});nextOrder(impl_->db);
            if(nextTable(impl_->db,"automatic_next")){
                const bool adoptions=nextTable(impl_->db,"automatic_adoptions");
                nextLegacy(impl_->db,target,adoptions);
                const auto sql=QByteArray("SELECT b.id,b.start,b.end FROM bookings b JOIN automatic_next a ON a.booking_id=b.id ")+(adoptions?"LEFT JOIN automatic_adoptions h ON h.booking_id=b.id ":"")+"WHERE b.state IN('QUEUED','ARMED') "+(adoptions?"AND h.booking_id IS NULL ":"");
                Statement old(impl_->db,sql.constData());QVector<QString> retired;
                while(old.step()==SQLITE_ROW){const auto id=exactMetadataText(old,0),a=exactMetadataText(old,1),b=exactMetadataText(old,2);initialReadRequire(canonicalUnsigned(a)==a&&canonicalUnsigned(b)==b&&compareUnsigned(a,b)<0,"Legacy automatic range corrupt");if(compareUnsigned(b,base.start)<=0||compareUnsigned(a,base.end)>=0){Statement d(impl_->db,"SELECT id,start,end,device,state,predecessor,run FROM dispatch_requests WHERE id=?");d.text(1,id);if(d.step()==SQLITE_ROW){const auto tuple=initialDispatch(d);initialReadRequire(tuple.state=="ARMED"&&!tuple.run&&!tuple.predecessor,"Claimed legacy automatic work cannot be retired");}retired.append(id);}}
                for(const auto& id:retired){Statement d(impl_->db,"UPDATE dispatch_requests SET state='CANCELLED' WHERE id=? AND state='ARMED'");d.text(1,id);d.step();Statement b(impl_->db,"UPDATE bookings SET state='RELEASED' WHERE id=? AND state IN('QUEUED','ARMED')");b.text(1,id);b.step();}
            }
            const auto scan=nextScan(impl_->db,target);
            Statement head(impl_->db,"SELECT b.id,b.start,b.end,b.state,b.kind,b.device,b.created_utc FROM bookings b LEFT JOIN dispatch_requests d ON d.id=b.id LEFT JOIN queue_zone_order z ON b.start_key>=z.start_key AND b.end_key<=z.end_key WHERE b.kind='LOCAL_INTENT' AND lower(b.device)=? AND b.state IN('QUEUED','ARMED') AND (d.id IS NULL OR d.state='ARMED') AND b.start_key>=? AND b.end_key<=? ORDER BY (z.priority IS NULL),z.priority,b.start_key,b.end_key,b.id LIMIT 1");head.text(1,normalized);head.text(2,key(scope.executionStart));head.text(3,key(scope.executionEnd));std::optional<Booking> manual;if(head.step()==SQLITE_ROW)manual=row(head);
            InitialStartDispatch d;d.device=target;d.state="PREPARED";d.origin="QUEUED_ADVANCE";d.predecessor=base.run;d.automatic=!manual;
            if(manual){
                requireResumeUuid(manual->id);initialReadRequire(source.declaredOpenContaining(manual->start,manual->end),"First manual continuation no longer declared Open; not skipped");
                for(const auto& b:scan.rows)if(b.id!=manual->id)initialReadRequire(!overlaps(manual->start,manual->end,b.start,b.end),"Manual continuation overlaps stored/historical work");
                Statement existing(impl_->db,"SELECT id,start,end,device,state,predecessor,run FROM dispatch_requests WHERE id=?");existing.text(1,manual->id);if(existing.step()==SQLITE_ROW){const auto armed=initialDispatch(existing);initialReadRequire(armed.state=="ARMED"&&!armed.run&&!armed.predecessor&&armed.start==manual->start&&armed.end==manual->end&&armed.device==target&&!nextAutomatic(impl_->db,armed),"Manual continuation identity cannot be rebound");}
                if(compareUnsigned(subtractUnsigned(manual->end,manual->start),scope.blockWidth)>0){
                    initialReadRequire(scan.count<4097,"Finite split exceeds existing4097 operational bound; reservation unchanged");const auto boundary=addUnsigned(manual->start,scope.blockWidth);nextFinite(manual->start,boundary,scope.blockWidth);
                    Statement release(impl_->db,"UPDATE bookings SET state='RELEASED' WHERE id=? AND state IN('QUEUED','ARMED')");release.text(1,manual->id);release.step();initialReadRequire(sqlite3_changes(impl_->db)==1,"Legacy manual split head changed");Statement cancel(impl_->db,"UPDATE dispatch_requests SET state='CANCELLED' WHERE id=? AND state='ARMED'");cancel.text(1,manual->id);cancel.step();const auto parent=manual->id,created=utc();
                    QVector<Booking> children;for(const auto& span:{qMakePair(manual->start,boundary),qMakePair(boundary,manual->end)}){Booking child{QUuid::createUuid().toString(QUuid::WithoutBraces),span.first,span.second,"QUEUED","LOCAL_INTENT",target,created};impl_->insert(child);Statement derivation(impl_->db,"INSERT INTO booking_derivations VALUES(?,?,'FINITE_WORK_SPLIT',?)");derivation.text(1,child.id);derivation.text(2,parent);derivation.text(3,created);derivation.step();children.append(child);}manual=children.first();
                }
                d.bookingId=manual->id;d.start=manual->start;d.end=manual->end;nextFinite(d.start,d.end,scope.blockWidth);
            }else{
                if(metadata->mode==pgn::mining::MiningSessionMode::ManualReservations&&!metadata->automaticAfterManual){result.status=NextBoundaryStatus::EndSession;return;}
                QVector<QPair<QString,QString>> occupied;for(const auto& b:scan.rows)occupied.append({b.start,b.end});const auto candidate=firstInitialFreeBlock(scope,source,occupied);
                if(!candidate){result.status=NextBoundaryStatus::Exhausted;result.error="No entire declared-Open free block in authorized scope; reservations preserved";return;}
                initialReadRequire(scan.count<4097,"Automatic continuation exceeds existing4097 operational bound; reservation unchanged");d.start=candidate->first;d.end=candidate->second;nextFinite(d.start,d.end,scope.blockWidth);
                d.bookingId=nextClaimId(base.run,d.start,d.end,target);impl_->insert({d.bookingId,d.start,d.end,"DISPATCHING","LOCAL_INTENT",target,utc()});
            }
            Statement dispatch(impl_->db,"INSERT INTO dispatch_requests(id,start,end,device,state,predecessor,run,created_utc) VALUES(?,?,?,?,'PREPARED',?,NULL,?) ON CONFLICT(id) DO UPDATE SET state='PREPARED',predecessor=excluded.predecessor WHERE dispatch_requests.state='ARMED' AND dispatch_requests.run IS NULL AND dispatch_requests.predecessor IS NULL AND dispatch_requests.start=excluded.start AND dispatch_requests.end=excluded.end AND dispatch_requests.device=excluded.device");dispatch.text(1,d.bookingId);dispatch.text(2,d.start);dispatch.text(3,d.end);dispatch.text(4,target);dispatch.text(5,base.run);dispatch.text(6,utc());dispatch.step();initialReadRequire(sqlite3_changes(impl_->db)==1,"Continuation dispatch changed; no rebind");
            if(manual){Statement claim(impl_->db,"UPDATE bookings SET state='DISPATCHING' WHERE id=? AND state IN('QUEUED','ARMED')");claim.text(1,d.bookingId);claim.step();initialReadRequire(sqlite3_changes(impl_->db)==1,"Manual continuation changed before claim");if(metadata->mode!=pgn::mining::MiningSessionMode::ManualReservations){Statement mode(impl_->db,"INSERT INTO booking_metadata VALUES(?,'manual-reservations') ON CONFLICT(key) DO UPDATE SET value=excluded.value");mode.text(1,sessionModeKey(normalized));mode.step();}result.mode=pgn::mining::MiningSessionMode::ManualReservations;}
            else {nextTable(impl_->db,"automatic_claims");impl_->execute("CREATE TABLE IF NOT EXISTS automatic_claims(booking_id TEXT PRIMARY KEY,predecessor TEXT NOT NULL,created_utc TEXT NOT NULL)");Statement claim(impl_->db,"INSERT INTO automatic_claims VALUES(?,?,?)");claim.text(1,d.bookingId);claim.text(2,base.run);claim.text(3,utc());claim.step();}
            result.dispatch=d;result.status=NextBoundaryStatus::Prepared;
        });if(!error.isEmpty()){result={};result.error=error.left(512);}
    }catch(const std::exception& e){result={};result.error=QString::fromUtf8(e.what()).left(512);}return result;
}
PreparedNextAdmissionRead BookingStore::savedPreparedNextAdmission(const QString& device,const QString& id,const NextBoundarySource& base) const {
    PreparedNextAdmissionRead result;bool begun=false;
    try{
        initialReadRequire(impl_->db,"Continuation readback unavailable");requireResumeUuid(id);const auto normalized=sessionDevice(device),target="GPU-"+normalized.mid(4);impl_->execute("BEGIN");begun=true;
        initialReadMetadata(impl_->db,sessionModeKey(normalized),64);initialReadMetadata(impl_->db,sessionPreferenceKey(normalized),16);const auto metadata=miningSessionMetadata(target);initialReadRequire(metadata&&metadata->mode!=pgn::mining::MiningSessionMode::Unselected,"Continuation session metadata unknown/unselected");nextSource(impl_->db,target,base);nextOrder(impl_->db);
        Statement exists(impl_->db,"SELECT 1 FROM dispatch_requests WHERE id=? OR id=CAST(? AS BLOB) LIMIT 1");exists.text(1,id);exists.text(2,id);if(exists.step()!=SQLITE_ROW){impl_->execute("COMMIT");begun=false;result.status=PreparedNextReadStatus::Absent;return result;}
        auto d=nextDispatch(impl_->db,id);nextPrepared(impl_->db,d,target,base);const auto scan=nextScan(impl_->db,target,id);for(const auto& b:scan.rows)if(b.id!=id)initialReadRequire(!overlaps(d.start,d.end,b.start,b.end),"Prepared continuation now overlaps known work");if(*d.automatic)initialReadRequire(scan.count<=4097,"Prepared automatic continuation exceeds4097 capacity");
        impl_->execute("COMMIT");begun=false;result.status=PreparedNextReadStatus::Present;result.predecessor=base;result.admission.status=NextBoundaryStatus::Prepared;result.admission.dispatch=d;result.admission.mode=metadata->mode;
    }catch(const std::exception& e){if(begun)sqlite3_exec(impl_->db,"ROLLBACK",nullptr,nullptr,nullptr);result={};result.error=QString::fromUtf8(e.what()).left(512);}return result;
}
ArmQueuedResult BookingStore::armQueuedIntent(const QString& id,const QString& device,
        const InitialStartScope& scope,const SourceRegistry& source){
    ArmQueuedResult result;
    try{
        requireResumeUuid(id);const auto normalized=sessionDevice(device),target="GPU-"+normalized.mid(4);
        const auto error=impl_->transaction([&]{
            initialReadMetadata(impl_->db,sessionModeKey(normalized),64);initialReadMetadata(impl_->db,sessionPreferenceKey(normalized),16);
            const auto metadata=miningSessionMetadata(target);initialReadRequire(metadata.has_value(),"Arm session metadata unknown");
            for(const auto& value:{scope.executionStart,scope.executionEnd,scope.autoStart,scope.autoEnd,scope.blockAnchor,scope.blockWidth})initialReadRequire(value.size()<=160&&canonicalUnsigned(value)==value,"Arm scope must contain exact bounded decimals");
            // Pure shared scope/provenance validation only. No free block is
            // persisted or returned by this explicit existing-intent action.
            firstInitialFreeBlock(scope,source,{});
            const auto checkedBooking=[&](const Statement& q){
                Booking b{exactMetadataText(q,0),exactMetadataText(q,1),exactMetadataText(q,2),exactMetadataText(q,3),exactMetadataText(q,4),exactMetadataText(q,5),exactMetadataText(q,6)};
                initialReadRequire(!b.id.isEmpty()&&b.id.size()<=128&&!b.id.contains(QChar(0))&&b.start.size()<=160&&b.end.size()<=160&&canonicalUnsigned(b.start)==b.start&&canonicalUnsigned(b.end)==b.end&&compareUnsigned(b.start,b.end)<0&&exactMetadataText(q,7)==key(b.start)&&exactMetadataText(q,8)==key(b.end),"Arm stored booking identity/range/keys corrupt");
                initialReadRequire(!b.createdUtc.isEmpty()&&b.createdUtc.size()<=4096&&!b.createdUtc.contains(QChar(0))&&(b.kind=="LOCAL_INTENT"||b.kind=="RUNTIME")&&(b.state=="QUEUED"||b.state=="ARMED"||b.state=="DISPATCHING"||b.state=="DISPATCHED"||b.state=="ACTIVE"||b.state=="PAUSED"||b.state=="SOURCE_FINISHED"||b.state=="RELEASED"),"Arm stored booking kind/state/date unknown");
                sessionDevice(b.device);return b;
            };
            Statement selected(impl_->db,"SELECT id,start,end,state,kind,device,created_utc,start_key,end_key FROM bookings WHERE id=? OR id=CAST(? AS BLOB) LIMIT 2");selected.text(1,id);selected.text(2,id);
            initialReadRequire(selected.step()==SQLITE_ROW,"Select an exact existing queued local intent first");const auto job=checkedBooking(selected);
            initialReadRequire(selected.step()==SQLITE_DONE&&job.id==id&&job.kind=="LOCAL_INTENT"&&job.device==target&&(job.state=="QUEUED"||job.state=="ARMED"),"Only existing queued/armed local work for this exact device can be armed");
            nextFinite(job.start,job.end,scope.blockWidth);
            initialReadRequire(compareUnsigned(job.start,scope.executionStart)>=0&&compareUnsigned(job.end,scope.executionEnd)<=0&&source.declaredOpenContaining(job.start,job.end),"Arm leaves authorized scope or current declared Open source");
            const bool hasDispatch=nextTable(impl_->db,"dispatch_requests"),hasAutomatic=nextTable(impl_->db,"automatic_next"),hasAdoptions=nextTable(impl_->db,"automatic_adoptions");
            bool existing=false;
            if(hasDispatch){
                Statement q(impl_->db,"SELECT id,start,end,device,state,predecessor,run,created_utc FROM dispatch_requests WHERE id=? OR id=CAST(? AS BLOB) LIMIT 2");q.text(1,id);q.text(2,id);
                if(q.step()==SQLITE_ROW){const auto d=initialDispatch(q);const auto rawDevice=exactMetadataText(q,3),created=exactMetadataText(q,7);initialReadRequire(q.step()==SQLITE_DONE&&d.bookingId==id&&d.start==job.start&&d.end==job.end&&d.device==target&&rawDevice==target&&d.state=="ARMED"&&!d.run&&!d.predecessor&&job.state=="ARMED"&&!created.isEmpty()&&created.size()<=4096&&!created.contains(QChar(0)),"Existing arm dispatch identity/state cannot be rebound");existing=true;}
            }
            initialReadRequire(existing||job.state=="QUEUED","ARMED local intent lost its exact dispatch tuple");
            InitialStartDispatch selectedArm;selectedArm.bookingId=id;selectedArm.start=job.start;selectedArm.end=job.end;selectedArm.device=target;selectedArm.state="ARMED";
            // Modern automatic_claims describe already claimed continuations,
            // not adoptable ARMED look-ahead. A dangling/contradictory origin
            // must not be silently relabelled as a new manual arm.
            initialReadRequire(!nextAutomatic(impl_->db,selectedArm),"Claimed automatic continuation cannot be manually rearmed");
            bool automatic=false,adopted=false;QSet<QString> retire;
            if(hasAutomatic){
                nextLegacy(impl_->db,target,hasAdoptions); // validate BEFORE any adoption/retirement
                QSet<QString> origins,epochs;Statement allOrigins(impl_->db,"SELECT current_run,booking_id FROM automatic_next");
                while(allOrigins.step()==SQLITE_ROW){
                    const auto epoch=exactMetadataText(allOrigins,0),booking=exactMetadataText(allOrigins,1);initialReadRequire(!origins.contains(booking)&&!epochs.contains(epoch),"Automatic origin identities are ambiguous");origins.insert(booking);epochs.insert(epoch);
                    Statement local(impl_->db,"SELECT state FROM bookings WHERE id=?");local.text(1,booking);initialReadRequire(local.step()==SQLITE_ROW,"Automatic origin lost local intent");
                    if(exactMetadataText(local,0)=="ARMED"){
                        initialReadRequire(hasDispatch,"Automatic ARMED origin lost dispatch table");const auto d=nextDispatch(impl_->db,booking);initialReadRequire(d.device==target&&d.state=="ARMED"&&!d.run&&!d.predecessor&&nextLocal(impl_->db,d)=="ARMED"&&!nextAutomatic(impl_->db,d),"Automatic ARMED origin is inconsistent or already claimed");
                    }
                }
                if(hasAdoptions){QSet<QString> seen;Statement q(impl_->db,"SELECT booking_id FROM automatic_adoptions");while(q.step()==SQLITE_ROW){const auto booking=exactMetadataText(q,0);initialReadRequire(!seen.contains(booking),"Automatic adoption identity ambiguous");seen.insert(booking);}}
                Statement origin(impl_->db,"SELECT booking_id FROM automatic_next WHERE booking_id=?");origin.text(1,id);automatic=origin.step()==SQLITE_ROW;
                if(hasAdoptions){Statement q(impl_->db,"SELECT booking_id FROM automatic_adoptions WHERE booking_id=?");q.text(1,id);adopted=q.step()==SQLITE_ROW;}
                if(hasDispatch){
                    const auto sql=QByteArray("SELECT d.id FROM dispatch_requests d JOIN automatic_next a ON a.booking_id=d.id ")+(hasAdoptions?"LEFT JOIN automatic_adoptions h ON h.booking_id=d.id ":"")+"WHERE d.state='ARMED' AND d.id<>? "+(hasAdoptions?"AND h.booking_id IS NULL ":"");
                    Statement q(impl_->db,sql.constData());q.text(1,id);
                    while(q.step()==SQLITE_ROW){const auto candidate=exactMetadataText(q,0);requireResumeUuid(candidate);const auto d=nextDispatch(impl_->db,candidate);initialReadRequire(d.device==target&&d.state=="ARMED"&&!d.run&&!d.predecessor&&nextLocal(impl_->db,d)=="ARMED","Claimed/corrupt automatic arm cannot be retired");retire.insert(candidate);}
                }
            }else if(hasAdoptions){Statement q(impl_->db,"SELECT booking_id FROM automatic_adoptions LIMIT 1");initialReadRequire(q.step()==SQLITE_DONE,"Automatic adoption lost its origin table");}
            // Audit every occupied row, including all retirement candidates,
            // before the first write. Do not hide corrupt history by cleanup.
            Statement rows(impl_->db,"SELECT id,start,end,state,kind,device,created_utc,start_key,end_key FROM bookings");
            while(rows.step()==SQLITE_ROW){const auto b=checkedBooking(rows);if(b.id==id||b.state=="RELEASED"||retire.contains(b.id))continue;initialReadRequire(!overlaps(job.start,job.end,b.start,b.end),"Arm overlaps stored reservation, physical or completed work");}
            bool changed=!existing||job.state!="ARMED"||!retire.isEmpty()||(automatic&&!adopted)||metadata->mode!=pgn::mining::MiningSessionMode::ManualReservations;
            if(automatic&&!adopted){
                if(!hasAdoptions)impl_->execute("CREATE TABLE automatic_adoptions(booking_id TEXT PRIMARY KEY,created_utc TEXT NOT NULL)");
                Statement q(impl_->db,"INSERT INTO automatic_adoptions VALUES(?,?)");q.text(1,id);q.text(2,utc());q.step();
            }
            for(const auto& candidate:retire){
                Statement d(impl_->db,"UPDATE dispatch_requests SET state='CANCELLED' WHERE id=? AND state='ARMED' AND predecessor IS NULL AND run IS NULL");d.text(1,candidate);d.step();initialReadRequire(sqlite3_changes(impl_->db)==1,"Automatic arm changed before retirement");
                Statement b(impl_->db,"UPDATE bookings SET state='RELEASED' WHERE id=? AND kind='LOCAL_INTENT' AND state='ARMED'");b.text(1,candidate);b.step();initialReadRequire(sqlite3_changes(impl_->db)==1,"Automatic local intent changed before retirement");
            }
            if(!hasDispatch)impl_->execute("CREATE TABLE dispatch_requests(id TEXT PRIMARY KEY,start TEXT NOT NULL,end TEXT NOT NULL,device TEXT NOT NULL,state TEXT NOT NULL,predecessor TEXT,run TEXT,created_utc TEXT NOT NULL);CREATE UNIQUE INDEX dispatch_run ON dispatch_requests(run) WHERE run IS NOT NULL;");
            if(!existing){Statement q(impl_->db,"INSERT INTO dispatch_requests VALUES(?,?,?,?,'ARMED',NULL,NULL,?)");q.text(1,id);q.text(2,job.start);q.text(3,job.end);q.text(4,target);q.text(5,utc());q.step();}
            if(job.state!="ARMED"){Statement q(impl_->db,"UPDATE bookings SET state='ARMED' WHERE id=? AND kind='LOCAL_INTENT' AND state='QUEUED'");q.text(1,id);q.step();initialReadRequire(sqlite3_changes(impl_->db)==1,"Existing local intent changed before arm");}
            if(metadata->mode!=pgn::mining::MiningSessionMode::ManualReservations){Statement q(impl_->db,"INSERT INTO booking_metadata VALUES(?,'manual-reservations') ON CONFLICT(key) DO UPDATE SET value=excluded.value");q.text(1,sessionModeKey(normalized));q.step();}
            auto d=nextDispatch(impl_->db,id);initialReadRequire(d.state=="ARMED"&&!d.run&&!d.predecessor&&nextLocal(impl_->db,d)=="ARMED","Arm readback changed during commit");d.origin="MANUAL_ARM";d.automatic=false;
            result.dispatch=d;result.status=ArmQueuedStatus::Armed;result.replayed=!changed;
        });
        if(!error.isEmpty()){result={};result.error=error.left(512);}
    }catch(const std::exception& e){result={};result.error=QString::fromUtf8(e.what()).left(512);}return result;
}
ObservedDispatchReconcileResult BookingStore::reconcileObservedDispatch(const QJsonObject& packet){
    return reconcileObservedDispatchInternal(packet,nullptr,{});
}
ObservedDispatchReconcileResult BookingStore::reconcileRecoveredDispatch(const QJsonObject& packet,
    const pgn::mining::SourceCheckpointResume& stopped,const QByteArray& bytes){
    return reconcileObservedDispatchInternal(packet,&stopped,bytes);
}
ObservedDispatchReconcileResult BookingStore::reconcileObservedDispatchInternal(const QJsonObject& packet,
    const pgn::mining::SourceCheckpointResume* stopped,const QByteArray& sealBytes){
    ObservedDispatchReconcileResult result;
    try{
        const bool recovered=stopped!=nullptr;
        if(recovered){
            initialReadRequire(impl_->db&&packet.value("schema")=="pgn-runtime-snapshot-1"&&packet.value("state")=="OFF"&&packet.value("owned").isBool()&&!packet.value("owned").toBool()&&packet.value("operator_stop")==true&&sourceManifestUtc(packet.value("observed_utc")),"Recovery requires actual stopped OFF observation");
            const auto last=packet.value("last_exit").toObject();initialReadRequire(last.value("run")==stopped->run&&pgn::mining::checkpoint_recovery::integer(last,"exit_code",0)&&sourceManifestUtc(last.value("utc")),"Recovery requires actual successful seal exit, not a forged parent stop");
        }else{
        initialReadRequire(impl_->db&&packet.value("schema")=="pgn-runtime-snapshot-1"&&packet.value("owned").isBool()&&packet.value("owned").toBool()&&packet.value("operator_stop").isBool()&&packet.value("driver_valid")==true&&(packet.value("state")=="RUNNING"||packet.value("state")=="STOPPING")&&sourceManifestUtc(packet.value("observed_utc")),"Current typed owned RUNNING/STOPPING observation required; first OFF/checkpoint remains Held");
        }
        initialReadRequire(packet.value("manifest").isObject()&&packet.value("binding").isObject(),"Actual manifest/binding unavailable");const auto manifest=packet.value("manifest").toObject(),binding=packet.value("binding").toObject();initialReadRequire(manifest.value("uuid").isString(),"Owned device identity missing");const auto observed=sourceManifest(manifest.value("uuid").toString(),manifest);
        initialReadRequire(binding.value("uuid")==observed.device&&binding.value("start")==observed.start&&binding.value("end")==observed.end&&binding.value("queued_intent")==observed.bookingId&&binding.value("length")==subtractUnsigned(observed.end,observed.start),"Owned binding differs from actual manifest tuple");
        if(recovered)initialReadRequire(stopped->run==observed.run&&stopped->start==observed.start&&stopped->end==observed.end&&!encodeResume(sessionDevice(observed.device),*stopped,sealBytes).isEmpty(),"Recovery stopped checkpoint tuple/bytes differ");
        const auto error=impl_->transaction([&]{
            sourceManifestStorageTypes(impl_->db);initialReadRequire(!retiredResumeRun(impl_->db,observed.run),"Retired epoch cannot become current owned dispatch");auto d=nextDispatch(impl_->db,observed.bookingId);initialReadRequire(d.device==observed.device&&d.start==observed.start&&d.end==observed.end&&(d.state=="PREPARED"||d.state=="STARTED")&&(!d.run||*d.run==observed.run),"Owned process contradicts current dispatch");
            const auto local=nextLocal(impl_->db,d);initialReadRequire((d.state=="PREPARED"&&local=="DISPATCHING")||(d.state=="STARTED"&&d.run&&local=="DISPATCHED"),"Owned local/dispatch projection inconsistent");
            Statement pending(impl_->db,"SELECT id FROM dispatch_requests WHERE lower(device)=? AND state='PREPARED'");pending.text(1,sessionDevice(observed.device));while(pending.step()==SQLITE_ROW)initialReadRequire(exactMetadataText(pending,0)==d.bookingId,"Another pending epoch holds physical reconciliation");
            QSet<QString> ancestors;const auto epoch=sourceManifestEpoch(impl_->db,observed.run);
            if(epoch){
                initialReadRequire(d.run==observed.run&&epoch->bookingId==d.bookingId&&epoch->device==observed.device&&epoch->predecessor==d.predecessor,"Owned resumed epoch pointer differs");sourceManifestEpochMatches(observed,*epoch);
                if(recovered){
                    const auto proof=pgn::mining::checkpoint_recovery::validate(epoch->recoveryEvidence,epoch->previousRun,epoch->run,epoch->checkpoint.resume.checkpointSha256,epoch->checkpoint.checkpointBytes);
                    const auto profile=manifest.value("source_profile").toObject();
                    const auto seal=pgn::mining::checkpoint_recovery::decoded(epoch->recoveryEvidence,"seal_base64",8192);
                    initialReadRequire(manifest.value("pid").toInteger(-1)==qint64(proof.childPid)&&manifest.value("birth")==proof.childBirth&&profile.value("image_sha256")==proof.imageSha&&seal.value("checkpoint_sha256").toString().toLower()==stopped->checkpointSha256,"Recovery actual PID/birth/image/output checkpoint differs");
                }else{
                const auto marker=binding.value("resume_start").toObject();initialReadRequire(marker.value("schema")=="pgn-checkpoint-resume-marker-1"&&marker.value("request_id")==epoch->requestUUID&&marker.value("observation_id")==epoch->observationUUID&&marker.value("run")==epoch->run&&marker.value("previous_run")==epoch->previousRun&&marker.value("checkpoint_sha256")==epoch->checkpoint.resume.checkpointSha256,"Owned resume marker differs from immutable epoch");
                }
                Statement checkpoint(impl_->db,"SELECT value FROM booking_metadata WHERE key=?");checkpoint.text(1,resumeKey(sessionDevice(observed.device),epoch->previousRun));initialReadRequire(checkpoint.step()==SQLITE_ROW,"Owned resume lost original checkpoint");const auto saved=decodeResume(checkpoint,sessionDevice(observed.device),epoch->previousRun);initialReadRequire(encodeResume(sessionDevice(observed.device),saved.resume,saved.checkpointBytes)==encodeResume(sessionDevice(observed.device),epoch->checkpoint.resume,epoch->checkpoint.checkpointBytes),"Owned resume checkpoint changed after preparation");
                auto previous=storedSourceManifest(impl_->db,observed.device,epoch->previousRun);initialReadRequire(previous.has_value(),"Owned resume lacks actual original manifest");sourceManifestChain(impl_->db,*previous);
                for(;;){initialReadRequire(!ancestors.contains(previous->run),"Owned resume ancestry cycle");ancestors.insert(previous->run);Statement state(impl_->db,"SELECT state FROM bookings WHERE id=? AND kind='RUNTIME'");state.text(1,previous->run);initialReadRequire(state.step()==SQLITE_ROW&&exactMetadataText(state,0)=="PAUSED","Owned resume original ancestry is not PAUSED");if(!previous->previousRun)break;previous=storedSourceManifest(impl_->db,observed.device,*previous->previousRun);initialReadRequire(previous.has_value(),"Owned resume ancestry manifest absent");}
            }else if(d.predecessor){
                initialReadRequire(!recovered,"Recovery requires explicit immutable recovered epoch");
                initialReadRequire(!observed.previousRun&&!binding.contains("resume_start"),"Continuation cannot invent a checkpoint epoch");const auto before=storedSourceManifest(impl_->db,observed.device,*d.predecessor);initialReadRequire(before.has_value(),"Owned continuation predecessor manifest absent");nextSource(impl_->db,observed.device,{before->run,before->manifest.value("birth").toString(),before->start,before->end,before->bookingId});const auto marker=binding.value("predecessor").toObject();initialReadRequire(marker.value("run")==before->run&&marker.value("start")==before->start&&marker.value("end")==before->end,"Owned continuation predecessor marker differs");
                // An inherited initial_start marker is historical, not this
                // dispatch's request identity. PS advance has no new one.
            }else{
                initialReadRequire(!observed.previousRun&&!binding.contains("resume_start")&&!binding.contains("predecessor"),"Initial owner has foreign predecessor/resume markers");const auto marker=binding.value("initial_start").toObject();initialReadRequire(marker.value("origin")=="INITIAL_START"&&marker.value("request_id").isString(),"Owned initial marker missing");const auto request=marker.value("request_id").toString();requireResumeUuid(request);const auto raw=initialReadMetadata(impl_->db,"mining-initial-start/v1/"+request);initialReadRequire(raw.has_value(),"Owned initial receipt absent");const auto object=QJsonDocument::fromJson(raw->toUtf8()).object();const auto receipt=initialReadReceipt(raw->toUtf8(),request,observed.device,initialReadString(object,"observation_id"));initialReadRequire(receipt.bookingId==d.bookingId&&receipt.start==d.start&&receipt.end==d.end,"Owned initial receipt selected another tuple");
            }
            CoveredPausedAncestors coverage(impl_->db);Statement others(impl_->db,"SELECT id,state,kind,device FROM bookings WHERE lower(device)=? AND state IN('ACTIVE','PAUSED','DISPATCHING','DISPATCHED')");others.text(1,sessionDevice(observed.device));while(others.step()==SQLITE_ROW){const auto id=exactMetadataText(others,0),state=exactMetadataText(others,1),kind=exactMetadataText(others,2);if(id==observed.run||id==d.bookingId)continue;initialReadRequire(state=="PAUSED"&&kind=="RUNTIME"&&(ancestors.contains(id)||coverage.contains(id,exactMetadataText(others,3))),"Unrelated live/interrupted work holds owned reconciliation");}
            Statement runtime(impl_->db,"SELECT start,end,device,kind,state,start_key,end_key FROM bookings WHERE id=? OR id=CAST(? AS BLOB) LIMIT 2");runtime.text(1,observed.run);runtime.text(2,observed.run);bool active=false;if(runtime.step()==SQLITE_ROW){initialReadRequire(exactMetadataText(runtime,0)==observed.start&&exactMetadataText(runtime,1)==observed.end&&exactMetadataText(runtime,2)==observed.device&&exactMetadataText(runtime,3)=="RUNTIME"&&exactMetadataText(runtime,5)==key(observed.start)&&exactMetadataText(runtime,6)==key(observed.end),"Owned runtime tuple differs");const auto state=exactMetadataText(runtime,4);initialReadRequire(state=="ACTIVE"||state=="PAUSED","Finished/aborted runtime cannot reactivate");active=state=="ACTIVE";initialReadRequire(runtime.step()==SQLITE_DONE,"Owned runtime identity ambiguous");}
            const auto old=storedSourceManifest(impl_->db,observed.device,observed.run);if(old)initialReadRequire(old->raw==observed.raw,"Immutable owned manifest bytes/keys/birth changed");const bool changed=(active==recovered)||d.state!="STARTED"||!old;
            impl_->runtime(observed.run,observed.start,observed.end,observed.device,recovered?"PAUSED":"ACTIVE");
            if(d.state=="PREPARED"){Statement dispatch(impl_->db,"UPDATE dispatch_requests SET state='STARTED',run=? WHERE id=? AND state='PREPARED'");dispatch.text(1,observed.run);dispatch.text(2,d.bookingId);dispatch.step();initialReadRequire(sqlite3_changes(impl_->db)==1,"Owned dispatch changed during reconciliation");Statement claim(impl_->db,"UPDATE bookings SET state='DISPATCHED' WHERE id=? AND state='DISPATCHING'");claim.text(1,d.bookingId);claim.step();initialReadRequire(sqlite3_changes(impl_->db)==1,"Owned LOCAL changed during reconciliation");}
            sourceManifestChain(impl_->db,observed);if(!old){Statement save(impl_->db,"INSERT INTO booking_metadata VALUES(?,?)");save.text(1,sourceManifestKey(observed.run));save.text(2,QString::fromUtf8(observed.raw));save.step();}const auto read=storedSourceManifest(impl_->db,observed.device,observed.run);initialReadRequire(read&&read->raw==observed.raw,"Owned manifest commit readback differs");sourceManifestChain(impl_->db,*read);
            result.status=changed?ObservedDispatchReconcileStatus::Reconciled:ObservedDispatchReconcileStatus::Unchanged;result.bookingId=d.bookingId;result.run=observed.run;
        });if(!error.isEmpty()){result={};result.error=error.left(512);}
    }catch(const std::exception& e){result={};result.error=QString::fromUtf8(e.what()).left(512);}return result;
}
InitialStartAdmissionRead BookingStore::savedInitialStartAdmission(const QString& request,const QString& device,
    const QString& observation,const std::optional<InitialStartObservedBase>& base) const {
    InitialStartAdmissionRead result;bool begun=false;
    try{
        if(!impl_->db)throw std::runtime_error("Initial admission storage unavailable");requireResumeUuid(request);requireResumeUuid(observation);
        const auto normalized=sessionDevice(device),target="GPU-"+normalized.mid(4);
        if(base){requireResumeRun(base->run);initialReadRequire(sourceManifestUtc(base->birth)&&base->start.size()<=160&&base->end.size()<=160&&canonicalUnsigned(base->start)==base->start&&canonicalUnsigned(base->end)==base->end&&compareUnsigned(base->start,base->end)<0,"Initial observed base identity/range invalid");}
        impl_->execute("BEGIN");begun=true;
        initialReadRequire(localTable(impl_->db,"booking_metadata")&&localTable(impl_->db,"bookings"),"Initial admission tables unavailable");
        const auto raw=initialReadMetadata(impl_->db,"mining-initial-start/v1/"+request);
        if(!raw){impl_->execute("COMMIT");begun=false;result.status=InitialStartAdmissionReadStatus::Absent;return result;}
        initialReadRequire(localTable(impl_->db,"dispatch_requests"),"Initial dispatch is not an existing table");
        const auto receipt=initialReadReceipt(raw->toUtf8(),request,target,observation);
        // Probe optional metadata through exact TEXT+BLOB-key lookups too;
        // defaults remain read-only and cannot hide a poisoned shadow key.
        initialReadMetadata(impl_->db,sessionModeKey(normalized),64);initialReadMetadata(impl_->db,sessionPreferenceKey(normalized),16);
        const auto metadata=miningSessionMetadata(target);initialReadRequire(metadata&&metadata->mode==(receipt.automatic?pgn::mining::MiningSessionMode::Automatic:pgn::mining::MiningSessionMode::ManualReservations),"Initial committed session mode is unknown/rebound");
        const auto order=initialReadMetadata(impl_->db,"queue-order-mode",16);initialReadRequire(!order||*order=="manual"||*order=="numeric","Initial queue order mode unknown");
        {Statement table(impl_->db,"SELECT type FROM sqlite_master WHERE name='dispatch_resume_epochs'");if(table.step()==SQLITE_ROW){initialReadRequire(exactMetadataText(table,0)=="table"&&table.step()==SQLITE_DONE,"Resume index is not an actual unique table");Statement reused(impl_->db,"SELECT 1 FROM dispatch_resume_epochs WHERE request_id=? OR request_id=CAST(? AS BLOB) LIMIT 1");reused.text(1,request);reused.text(2,request);initialReadRequire(reused.step()==SQLITE_DONE,"Initial request also identifies a resume epoch");}}
        Statement dispatch(impl_->db,"SELECT id,start,end,device,state,predecessor,run FROM dispatch_requests WHERE id=? OR id=CAST(? AS BLOB) LIMIT 2");dispatch.text(1,receipt.bookingId);dispatch.text(2,receipt.bookingId);
        initialReadRequire(dispatch.step()==SQLITE_ROW,"Initial committed dispatch absent");auto d=initialDispatch(dispatch);
        initialReadRequire(exactMetadataText(dispatch,3)==target&&d.bookingId==receipt.bookingId&&d.start==receipt.start&&d.end==receipt.end&&d.device==target&&d.state=="PREPARED"&&!d.predecessor&&!d.run&&dispatch.step()==SQLITE_DONE,"Initial dispatch requires exact PREPARED/NULL epoch; reconcile changed state");
        Statement local(impl_->db,"SELECT id,start,end,start_key,end_key,device,state,kind FROM bookings WHERE id=? OR id=CAST(? AS BLOB) LIMIT 2");local.text(1,receipt.bookingId);local.text(2,receipt.bookingId);
        initialReadRequire(local.step()==SQLITE_ROW,"Initial existing local booking absent");const QStringList fields{receipt.bookingId,receipt.start,receipt.end,key(receipt.start),key(receipt.end),target,"DISPATCHING","LOCAL_INTENT"};
        for(int i=0;i<fields.size();++i)initialReadRequire(exactMetadataText(local,i)==fields[i],"Initial LOCAL_INTENT identity/range/state differs");initialReadRequire(local.step()==SQLITE_DONE,"Initial LOCAL_INTENT identity ambiguous");
        {Statement all(impl_->db,"SELECT id,state,device FROM dispatch_requests");while(all.step()==SQLITE_ROW){const auto state=exactMetadataText(all,1);initialReadRequire(!exactMetadataText(all,0).contains(QChar(0))&&(state=="ARMED"||state=="PREPARED"||state=="STARTED"||state=="SOURCE_FINISHED"||state=="CANCELLED"),"Initial dispatch index contains unknown identity/state");sessionDevice(exactMetadataText(all,2));}}
        Statement pending(impl_->db,"SELECT id FROM dispatch_requests WHERE lower(device)=? AND state='PREPARED' LIMIT 2");pending.text(1,normalized);initialReadRequire(pending.step()==SQLITE_ROW&&exactMetadataText(pending,0)==receipt.bookingId&&pending.step()==SQLITE_DONE,"Initial prepared identity is not the sole pending dispatch");
        CoveredPausedAncestors coverage(impl_->db);quint64 activeCount=0;
        Statement bookings(impl_->db,"SELECT id,start,end,start_key,end_key,state,kind,device FROM bookings");
        while(bookings.step()==SQLITE_ROW){
            const auto id=exactMetadataText(bookings,0),a=exactMetadataText(bookings,1),b=exactMetadataText(bookings,2),state=exactMetadataText(bookings,5),kind=exactMetadataText(bookings,6),rowDevice=exactMetadataText(bookings,7);
            initialReadRequire(!id.isEmpty()&&!id.contains(QChar(0))&&a.size()<=160&&b.size()<=160&&canonicalUnsigned(a)==a&&canonicalUnsigned(b)==b&&compareUnsigned(a,b)<0&&exactMetadataText(bookings,3)==key(a)&&exactMetadataText(bookings,4)==key(b),"Initial stored booking identity/range/key corrupt");
            if(retiredAbortedRuntime(id,state,kind,rowDevice))continue;
            initialReadRequire((kind=="RUNTIME"||kind=="LOCAL_INTENT")&&(state=="QUEUED"||state=="ARMED"||state=="DISPATCHING"||state=="DISPATCHED"||state=="ACTIVE"||state=="PAUSED"||state=="SOURCE_FINISHED"||state=="RELEASED"),"Initial stored booking kind/state unknown");const auto rowNormalized=sessionDevice(rowDevice);
            if(state=="RELEASED")continue;
            const bool covered=state=="PAUSED"&&kind=="RUNTIME"&&coverage.contains(id,rowDevice);
            if(state!="SOURCE_FINISHED"&&!covered){initialReadRequire(activeCount<std::numeric_limits<quint64>::max(),"Initial operational count overflow");++activeCount;}
            if(id==receipt.bookingId)continue;
            initialReadRequire(!overlaps(a,b,receipt.start,receipt.end),"Initial prepared range now overlaps another reserved/historical range");
            if(rowNormalized==normalized&&(state=="ACTIVE"||state=="PAUSED"||state=="DISPATCHING"||state=="DISPATCHED"))initialReadRequire(covered,"Existing physical/interrupted booking requires reconciliation, not initial submission");
        }
        if(receipt.automatic)initialReadRequire(activeCount<=4097,"Initial prepared Auto exceeds existing4097 operational capacity");
        if(base){
            sourceManifestStorageTypes(impl_->db);const auto actual=storedSourceManifest(impl_->db,target,base->run);
            initialReadRequire(actual&&actual->start==base->start&&actual->end==base->end&&actual->manifest.value("birth")==base->birth,"Initial observed base differs from immutable actual source manifest");sourceManifestChain(impl_->db,*actual);
            Statement physical(impl_->db,"SELECT state FROM bookings WHERE id=? AND kind='RUNTIME'");physical.text(1,base->run);initialReadRequire(physical.step()==SQLITE_ROW&&exactMetadataText(physical,0)=="SOURCE_FINISHED"&&physical.step()==SQLITE_DONE,"Initial observed base is not an exact settled SOURCE runtime");
        }
        d.origin="INITIAL_START";d.automatic=receipt.automatic;
        InitialStartResult admission;admission.status=InitialStartStatus::Prepared;admission.dispatch=d;admission.mode=receipt.automatic?pgn::mining::MiningSessionMode::Automatic:pgn::mining::MiningSessionMode::ManualReservations;
        impl_->execute("COMMIT");begun=false;result.status=InitialStartAdmissionReadStatus::Present;result.admission=std::move(admission);result.receiptUtf8=raw->toUtf8();result.requestUUID=request;result.observationUUID=observation;result.base=base;
    }catch(const std::exception& e){if(begun)sqlite3_exec(impl_->db,"ROLLBACK",nullptr,nullptr,nullptr);result={};result.error=QString::fromUtf8(e.what()).left(512);}return result;
}
CheckpointResumeAdmission BookingStore::savedPreparedCheckpointResumeAdmission(const QString& request,const QString& device,
    const QString& newRun,const QString& observation,const InitialStartObservedBase& base) const {
    CheckpointResumeAdmission result;bool begun=false;
    try{
        if(!impl_->db)throw std::runtime_error("Prepared resume storage unavailable");requireResumeUuid(request);requireResumeUuid(observation);requireResumeRun(newRun);requireResumeRun(base.run);
        const auto normalized=sessionDevice(device),target="GPU-"+normalized.mid(4);
        initialReadRequire(newRun!=base.run&&sourceManifestUtc(base.birth)&&base.start.size()<=160&&base.end.size()<=160&&canonicalUnsigned(base.start)==base.start&&canonicalUnsigned(base.end)==base.end&&compareUnsigned(base.start,base.end)<0,"Prepared resume observed base/new run invalid");
        impl_->execute("BEGIN");begun=true;
        initialReadRequire(localTable(impl_->db,"booking_metadata")&&localTable(impl_->db,"bookings")&&localTable(impl_->db,"dispatch_requests")&&localTable(impl_->db,"dispatch_resume_epochs"),"Prepared resume native tables absent");sourceManifestStorageTypes(impl_->db);
        const auto saved=sourceManifestEpoch(impl_->db,newRun);initialReadRequire(saved.has_value(),"Prepared resume immutable epoch absent");const auto& e=*saved;const auto& r=e.checkpoint.resume;
        initialReadRequire(e.requestUUID==request&&e.observationUUID==observation&&e.device==target&&e.run==newRun&&e.previousRun==base.run&&r.run==base.run&&r.start==base.start&&r.end==base.end,"Prepared resume request/observation/device/base/epoch differs");
        initialReadRequire(!initialReadMetadata(impl_->db,"mining-initial-start/v1/"+request),"Prepared resume request also identifies INITIAL_START");
        const auto checkpoint=initialReadMetadata(impl_->db,resumeKey(normalized,base.run),ResumeRecordLimit);initialReadRequire(checkpoint&&checkpoint->toUtf8()==encodeResume(normalized,r,e.checkpoint.checkpointBytes),"Prepared resume saved old checkpoint differs from immutable epoch");
        initialReadMetadata(impl_->db,sessionModeKey(normalized),64);initialReadMetadata(impl_->db,sessionPreferenceKey(normalized),16);initialReadRequire(miningSessionMetadata(target).has_value(),"Prepared resume session metadata unknown/corrupt");
        Statement q(impl_->db,"SELECT id,start,end,device,state,predecessor,run FROM dispatch_requests WHERE id=? OR id=CAST(? AS BLOB) LIMIT 2");q.text(1,e.bookingId);q.text(2,e.bookingId);initialReadRequire(q.step()==SQLITE_ROW,"Prepared resume dispatch absent");const auto d=initialDispatch(q);
        initialReadRequire(exactMetadataText(q,3)==target&&d.bookingId==e.bookingId&&d.start==r.start&&d.end==r.end&&d.device==target&&d.state=="PREPARED"&&d.run&&*d.run==newRun&&d.predecessor==e.predecessor&&q.step()==SQLITE_DONE,"Resume dispatch is not exact PREPARED/new run/unchanged predecessor; reconcile");
        const auto exactRow=[&](const QString& id,const QString& kind,const QString& state){Statement row(impl_->db,"SELECT id,start,end,start_key,end_key,device,state,kind FROM bookings WHERE id=? OR id=CAST(? AS BLOB) LIMIT 2");row.text(1,id);row.text(2,id);initialReadRequire(row.step()==SQLITE_ROW,"Prepared resume physical/local row absent");const QStringList fields{id,r.start,r.end,key(r.start),key(r.end),target,state,kind};for(int i=0;i<fields.size();++i)initialReadRequire(exactMetadataText(row,i)==fields[i],"Prepared resume physical/local tuple/state differs");initialReadRequire(row.step()==SQLITE_DONE,"Prepared resume physical/local identity ambiguous");};
        exactRow(e.bookingId,"LOCAL_INTENT","DISPATCHING");exactRow(base.run,"RUNTIME","PAUSED");
        {Statement used(impl_->db,"SELECT 1 FROM bookings WHERE id=? OR id=CAST(? AS BLOB) LIMIT 1");used.text(1,newRun);used.text(2,newRun);initialReadRequire(used.step()==SQLITE_DONE,"Prepared resume NEW run already physically observed; reconcile, never retry");}
        {Statement pending(impl_->db,"SELECT id FROM dispatch_requests WHERE lower(device)=? AND state='PREPARED' LIMIT 2");pending.text(1,normalized);initialReadRequire(pending.step()==SQLITE_ROW&&exactMetadataText(pending,0)==e.bookingId&&pending.step()==SQLITE_DONE,"Prepared resume requires one exact pending dispatch");}
        const auto actual=storedSourceManifest(impl_->db,target,base.run);initialReadRequire(actual&&actual->bookingId==e.bookingId&&actual->start==r.start&&actual->end==r.end&&actual->manifest.value("birth")==base.birth,"Prepared resume base differs from immutable actual old manifest");
        QSet<QString> ancestry;auto current=*actual;auto incoming=e.checkpoint;bool first=true;
        for(;;){
            initialReadRequire(!ancestry.contains(current.run),"Prepared resume actual ancestry cycle");ancestry.insert(current.run);
            initialReadRequire(current.device==target&&current.bookingId==e.bookingId&&current.start==r.start&&current.end==r.end,"Prepared resume actual ancestry range/device/intent differs");
            const auto linked=sourceManifestRelations(impl_->db,current,first);first=false;initialReadRequire(linked.run&&*linked.run==newRun&&linked.state=="PREPARED"&&linked.predecessor==e.predecessor,"Prepared resume ancestry dispatch pointer/state differs");exactRow(current.run,"RUNTIME","PAUSED");
            const auto prior=sourceManifestEpoch(impl_->db,current.run);if(!current.previousRun){initialReadRequire(!prior,"Prepared resume origin lacks actual markers for indexed epoch");break;}
            initialReadRequire(prior.has_value()&&prior->predecessor==e.predecessor,"Prepared resume actual ancestor lacks exact immutable epoch");sourceManifestEpochMatches(current,*prior);
            for(const auto pair:{qMakePair(prior->checkpoint.resume.positions,incoming.resume.positions),qMakePair(prior->checkpoint.resume.tests,incoming.resume.tests),qMakePair(prior->checkpoint.resume.gaps,incoming.resume.gaps),qMakePair(prior->checkpoint.resume.batches,incoming.resume.batches)})initialReadRequire(compareUnsigned(pair.first,pair.second)<=0,"Prepared resume cumulative checkpoint lineage regressed");
            initialReadRequire(*current.previousRun!=newRun&&!ancestry.contains(*current.previousRun),"Prepared resume actual ancestry cycle/back-reference to reserved NEW run");
            const auto previous=storedSourceManifest(impl_->db,target,*current.previousRun);initialReadRequire(previous.has_value(),"Prepared resume legacy actual origin absent; never adopt");incoming=prior->checkpoint;current=*previous;
        }
        // Own partial ancestors are not new work. Unrelated PAUSED work still
        // blocks unless the existing CoveredPausedAncestors proof settles it.
        CoveredPausedAncestors coverage(impl_->db);Statement active(impl_->db,"SELECT id,state,kind,device FROM bookings WHERE lower(device)=?");active.text(1,normalized);
        while(active.step()==SQLITE_ROW){const auto id=exactMetadataText(active,0),state=exactMetadataText(active,1),kind=exactMetadataText(active,2),rowDevice=exactMetadataText(active,3);if(retiredAbortedRuntime(id,state,kind,rowDevice))continue;initialReadRequire(sessionDevice(rowDevice)==normalized&&(kind=="RUNTIME"||kind=="LOCAL_INTENT")&&(state=="QUEUED"||state=="ARMED"||state=="DISPATCHING"||state=="DISPATCHED"||state=="ACTIVE"||state=="PAUSED"||state=="SOURCE_FINISHED"||state=="RELEASED"),"Prepared resume device booking state/type unknown");
            if(id==e.bookingId)continue;if(state=="PAUSED"&&kind=="RUNTIME"&&(ancestry.contains(id)||coverage.contains(id,rowDevice)))continue;
            initialReadRequire(state!="ACTIVE"&&state!="PAUSED"&&state!="DISPATCHING"&&state!="DISPATCHED","Other active/interrupted booking contradicts stopped resume admission");
        }
        const auto raw=encodeResumeEpoch(e);const QJsonObject wire{{"schema","pgn-checkpoint-resume-admission-1"},{"receipt",QJsonDocument::fromJson(raw).object()},{"base",QJsonObject{{"base_run",base.run},{"base_birth",base.birth},{"base_start",base.start},{"base_end",base.end}}}};
        initialReadRequire(QJsonDocument(wire).toJson(QJsonDocument::Compact).size()<=8192,"Prepared resume admission exceeds conservative8KiB wire bound");
        impl_->execute("COMMIT");begun=false;result.status=CheckpointResumeAdmissionStatus::Prepared;result.epoch=e;result.receiptUtf8=raw;
    }catch(const std::exception& e){if(begun)sqlite3_exec(impl_->db,"ROLLBACK",nullptr,nullptr,nullptr);result={};result.error=QString::fromUtf8(e.what()).left(512);}return result;
}
QVector<Booking> BookingStore::active()const{
    QVector<Booking> result;if(!valid())return result;
    const auto sql=QByteArray("SELECT b.id,b.start,b.end,b.state,b.kind,b.device,b.created_utc FROM bookings b LEFT JOIN queue_zone_order z ON b.start_key>=z.start_key AND b.end_key<=z.end_key WHERE b.state IN('QUEUED','ARMED','DISPATCHING','DISPATCHED','ACTIVE','PAUSED')")+impl_->visibleJobFilter()+" ORDER BY (z.priority IS NULL),z.priority,b.start_key,b.id";
    Statement q(impl_->db,sql.constData());
    CoveredPausedAncestors coverage(impl_->db);
    while(result.size()<4097&&q.step()==SQLITE_ROW){
        const auto booking=row(q);
        if(booking.state=="PAUSED"&&booking.kind=="RUNTIME"){
            // A retired partial epoch belongs to history after its current
            // descendant has paid SOURCE completion. Filter BEFORE applying
            // the operational view bound so history cannot crowd out work.
            // Uncertain/corrupt ancestry stays visibly PAUSED, never hidden.
            try{if(coverage.contains(booking.id,booking.device))continue;}catch(const std::exception&){}
        }
        result.append(booking);
    }return result;
}
QVector<Booking> BookingStore::history(int limit)const{
    QVector<Booking> result;if(!valid())return result;
    if(limit<1||limit>256)throw std::invalid_argument("Booking history page bound");
    const auto sql=QByteArray("SELECT b.id,b.start,b.end,b.state,b.kind,b.device,b.created_utc FROM bookings b WHERE 1")+impl_->visibleJobFilter()+" ORDER BY b.created_utc DESC,b.id DESC LIMIT ?";
    Statement q(impl_->db,sql.constData());
    sqlite3_bind_int(q.value,1,limit);while(q.step()==SQLITE_ROW)result.append(row(q));return result;
}
QVector<Booking> BookingStore::completedRanges() const {
    QVector<Booking> result;if(!valid())return result;
    Statement q(impl_->db,"SELECT id,start,end,state,kind,device,created_utc FROM bookings WHERE kind='RUNTIME' AND state='SOURCE_FINISHED' ORDER BY device,start_key,end_key");
    while(q.step()==SQLITE_ROW){auto next=row(q);
        if(!result.isEmpty()&&result.last().device==next.device&&compareUnsigned(next.start,result.last().end)<=0){
            auto& previous=result.last();if(compareUnsigned(next.end,previous.end)>0)previous.end=next.end;
            previous.id="processed/"+previous.device+"/"+previous.start+"/"+previous.end;
        }else result.append(next);
    }
    return result;
}
QString BookingStore::completedPositions() const {
    if(!valid())return {};
    Statement query(impl_->db,"SELECT start,end FROM bookings WHERE kind='RUNTIME' AND state='SOURCE_FINISHED'");QString total="0";
    while(query.step()==SQLITE_ROW){const auto first=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(query.value,0)));
        const auto last=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(query.value,1)));total=addUnsigned(total,subtractUnsigned(last,first));}
    return total;
}
QString BookingStore::queue(const SourceRegistry& source,const QString& a,const QString& b,const QString& device){
    return impl_->transaction([&]{
        const auto first=canonicalUnsigned(a),last=canonicalUnsigned(b);
        if(!source.declaredOpenContaining(first,last))throw std::runtime_error("The exact range is not contained in one declared-open source row");
        if(device.isEmpty()||device.size()>128)throw std::runtime_error("Explicit device identity required");
        if(operationalActiveCount(impl_->db)>=4096)throw std::runtime_error("4096 active blocks supported; release/finish a local intent first");
        Statement overlaps(impl_->db,"SELECT id FROM bookings WHERE state IN('QUEUED','ARMED','DISPATCHING','DISPATCHED','ACTIVE','PAUSED','SOURCE_FINISHED') AND start_key<? AND end_key>? LIMIT 1");
        overlaps.text(1,key(last));overlaps.text(2,key(first));
        if(overlaps.step()==SQLITE_ROW)throw std::runtime_error("Local work overlaps queued/active/paused or source-finished work");
        impl_->insert({QUuid::createUuid().toString(QUuid::WithoutBraces),first,last,"QUEUED","LOCAL_INTENT",device,utc()});
    });
}
int BookingStore::availableBlocks(const SourceRegistry& source,const QString& a,const QString& b,const QString& za,const QString& zb) const {
    if(!valid())return 0;int free=0;
    try{for(const auto& block:selectedBlocks(a,b,za,zb))
        if(source.declaredOpenContaining(block.first,block.second)&&!occupied(impl_->db,block.first,block.second))++free;
    }catch(const std::exception&){return 0;}return free;
}
QString BookingStore::queueAvailableBlocks(const SourceRegistry& source,const QString& a,const QString& b,const QString& za,const QString& zb,const QString& device){
    return queueSelection(source,{{a,b,za,zb}},device);
}
int BookingStore::availableSelection(const SourceRegistry& source,const QVector<ExactBlockSelection>& selection,QString* reason)const{
    if(reason)reason->clear();int free=0;try{for(const auto& block:exactSelection(selection))if(source.declaredOpenContaining(block.first,block.second)&&!occupied(impl_->db,block.first,block.second))++free;}catch(const std::exception& error){if(reason)*reason=QString::fromUtf8(error.what());return 0;}return free;
}
QString BookingStore::queueSelection(const SourceRegistry& source,const QVector<ExactBlockSelection>& selection,const QString& device){
    return impl_->transaction([&]{
        if(device.isEmpty()||device.size()>128)throw std::runtime_error("Observed GPU identity required");
        const auto blocks=exactSelection(selection);QVector<QPair<QString,QString>> free;
        for(const auto& block:blocks)if(source.declaredOpenContaining(block.first,block.second)&&!occupied(impl_->db,block.first,block.second))free.append(block);
        if(free.isEmpty())throw std::runtime_error("No free blocks: reserved, active, processed or not declared open. Nothing changed");
        if(operationalActiveCount(impl_->db)+quint64(free.size())>4096)throw std::runtime_error("4096 active blocks supported; entire selection unchanged");
        for(const auto& block:free)impl_->insert({QUuid::createUuid().toString(QUuid::WithoutBraces),block.first,block.second,"QUEUED","LOCAL_INTENT",device,utc()});
    });
}
QString BookingStore::releaseSelection(const QVector<ExactBlockSelection>& selection){
    return impl_->transaction([&]{
        const auto blocks=exactSelection(selection);QVector<QPair<QString,QString>> spans;
        for(const auto& block:blocks){if(!spans.isEmpty()&&spans.last().second==block.first)spans.last().second=block.second;else spans.append(block);}
        QVector<Booking> touched;QSet<QString> seen;
        for(const auto& requested:spans){Statement query(impl_->db,"SELECT id,start,end,state,kind,device,created_utc FROM bookings WHERE kind='LOCAL_INTENT' AND state IN('QUEUED','ARMED') AND start_key<? AND end_key>? ORDER BY start_key LIMIT 4097");query.text(1,key(requested.second));query.text(2,key(requested.first));
            while(query.step()==SQLITE_ROW){const auto job=row(query);if(seen.contains(job.id))continue;seen.insert(job.id);if(seen.size()>4096)throw std::runtime_error("Queued selection bound exceeded; nothing released");touched.append(job);
            }
        }
        if(touched.isEmpty())throw std::runtime_error("No queued reservation in selection; active work and history unchanged");
        impl_->visibleJobFilter(); // Detect a dispatch table created since construction.
        for(const auto& job:touched){
            QVector<QPair<QString,QString>> remainder;auto cursor=job.start;
            for(const auto& span:spans)if(overlaps(job.start,job.end,span.first,span.second)){
                const auto first=compareUnsigned(span.first,job.start)>0?span.first:job.start,last=compareUnsigned(span.second,job.end)<0?span.second:job.end;
                if(compareUnsigned(cursor,first)<0)remainder.append({cursor,first});if(compareUnsigned(cursor,last)<0)cursor=last;
            }
            if(compareUnsigned(cursor,job.end)<0)remainder.append({cursor,job.end});
            Statement update(impl_->db,"UPDATE bookings SET state='RELEASED' WHERE id=? AND kind='LOCAL_INTENT' AND state IN('QUEUED','ARMED')");update.text(1,job.id);update.step();
            if(impl_->dispatchAvailable){Statement cancel(impl_->db,"UPDATE dispatch_requests SET state='CANCELLED' WHERE id=? AND state='ARMED'");cancel.text(1,job.id);cancel.step();}
            for(const auto& range:remainder){Booking child{QUuid::createUuid().toString(QUuid::WithoutBraces),range.first,range.second,"QUEUED","LOCAL_INTENT",job.device,utc()};impl_->insert(child);Statement provenance(impl_->db,"INSERT INTO booking_derivations VALUES(?,?,'SELECTION_RELEASE_REMAINDER',?)");provenance.text(1,child.id);provenance.text(2,job.id);provenance.text(3,child.createdUtc);provenance.step();}
        }
        if(operationalActiveCount(impl_->db)>4096)throw std::runtime_error("4096 active intents supported; release would exceed capacity. Entire selection unchanged");
    });
}
QString BookingStore::reorderQueuedZones(const QVector<ExactBlockSelection>& zones,const QStringList& expectedQueuedIds){
    return impl_->transaction([&]{
        if(zones.isEmpty()||zones.size()>4096)throw std::runtime_error("Select an existing queued zone order");
        QVector<Booking> queued;QSet<QString> actual,expected;
        for(const auto& id:expectedQueuedIds)expected.insert(id);
        Statement query(impl_->db,"SELECT id,start,end,state,kind,device,created_utc FROM bookings WHERE kind='LOCAL_INTENT' AND state IN('QUEUED','ARMED') ORDER BY start_key,id LIMIT 4097");
        while(query.step()==SQLITE_ROW){auto job=row(query);actual.insert(job.id);queued.append(job);}
        if(actual!=expected||actual.size()!=expectedQueuedIds.size()||actual.size()>4096)throw std::runtime_error("Queue changed while dragging; refresh and try again. Nothing reordered");
        QSet<QString> wanted;for(const auto& job:queued){const auto z=queueZone(job);wanted.insert(z.start+"/"+z.end);}
        QSet<QString> seen;for(const auto& z:zones){canonicalUnsigned(z.start);canonicalUnsigned(z.end);const auto id=z.start+"/"+z.end;if(!wanted.contains(id)||seen.contains(id))throw std::runtime_error("Order must contain each complete queued zone exactly once");seen.insert(id);}
        if(seen!=wanted)throw std::runtime_error("Queued zones missing from order; nothing changed");
        impl_->execute("DELETE FROM queue_zone_order");
        for(int i=0;i<zones.size();++i){const auto& z=zones[i];Statement insert(impl_->db,"INSERT INTO queue_zone_order VALUES(?,?,?,?,?)");insert.text(1,z.start);insert.text(2,z.end);insert.text(3,key(z.start));insert.text(4,key(z.end));sqlite3_bind_int(insert.value,5,i);insert.step();}
        impl_->execute("INSERT INTO booking_metadata(key,value) VALUES('queue-order-mode','manual') ON CONFLICT(key) DO UPDATE SET value='manual'");
    });
}
QString BookingStore::resetQueueOrder(){return impl_->transaction([&]{impl_->execute("DELETE FROM queue_zone_order;INSERT INTO booking_metadata(key,value) VALUES('queue-order-mode','numeric') ON CONFLICT(key) DO UPDATE SET value='numeric'");});}
QString BookingStore::release(const QString& id){
    return impl_->transaction([&]{
        Statement update(impl_->db,"UPDATE bookings SET state='RELEASED' WHERE id=? AND state IN('QUEUED','ARMED') AND kind='LOCAL_INTENT'");
        update.text(1,id);update.step();if(sqlite3_changes(impl_->db)!=1)throw std::runtime_error("Only a queued local intent can be released; mining state was not changed");
    });
}
QString BookingStore::releaseRange(const QString& a,const QString& b){
    return impl_->transaction([&]{
        const auto first=canonicalUnsigned(a),last=canonicalUnsigned(b);
        if(first.isEmpty()||last.isEmpty()||compareUnsigned(first,last)>=0)throw std::runtime_error("Invalid exact release interval");
        Statement crossing(impl_->db,"SELECT id FROM bookings WHERE kind='LOCAL_INTENT' AND state IN('QUEUED','ARMED') AND start_key<? AND end_key>? AND (start_key<? OR end_key>?) LIMIT 1");
        crossing.text(1,key(last));crossing.text(2,key(first));crossing.text(3,key(first));crossing.text(4,key(last));
        if(crossing.step()==SQLITE_ROW)throw std::runtime_error("Select all blocks of the queued reservation before releasing it");
        Statement update(impl_->db,"UPDATE bookings SET state='RELEASED' WHERE kind='LOCAL_INTENT' AND state IN('QUEUED','ARMED') AND start_key>=? AND end_key<=?");
        update.text(1,key(first));update.text(2,key(last));update.step();
        if(sqlite3_changes(impl_->db)==0)throw std::runtime_error("No complete queued reservation to release. Active work and completed history unchanged");
    });
}
QString BookingStore::observeRuntime(const QString& run,const QString& a,const QString& b,const QString& device,bool running){
    return impl_->transaction([&]{impl_->runtime(run,a,b,device,running?"ACTIVE":"PAUSED");});
}
QString BookingStore::recordSourceCompletion(const QString& run,const QString& a,const QString& b,const QString& device){
    // Receipt admission and its audit event commit together, with no artificial
    // intermediate PAUSED state and no partial two-transaction completion.
    return impl_->transaction([&]{impl_->runtime(run,a,b,device,"SOURCE_FINISHED");});
}
QString BookingStore::recordQueuedSourceCompletion(const QString& run,const QString& a,const QString& b,
        const QString& device,const std::optional<QString>& queuedIntent){
    if(!impl_->db)return "Queued SOURCE completion storage unavailable";
    // One writer lock covers ALL preconditions, mutations and audit triggers.
    // Caller evidence is not an authority synthesized by this storage API.
    return impl_->transaction([&]{
        if(retiredResumeRun(impl_->db,run))throw std::runtime_error("Retired partial run cannot be completed as SOURCE");
        static const QRegularExpression runPattern("^[0-9a-f]{32}$");
        const auto canonicalDevice="GPU-"+sessionDevice(device).mid(4);
        if(run.size()!=32||!runPattern.match(run).hasMatch()||canonicalUnsigned(a)!=a||
           canonicalUnsigned(b)!=b||compareUnsigned(a,b)>=0)
            throw std::runtime_error("Queued SOURCE completion identity/range invalid");
        if(queuedIntent&&(queuedIntent->isEmpty()||queuedIntent->size()>128||queuedIntent->contains(QChar(0))||
           QString::fromUtf8(queuedIntent->toUtf8())!=*queuedIntent||*queuedIntent==run))
            throw std::runtime_error("Queued SOURCE intent identity invalid");

        bool physicalFinished=false;
        {
            Statement physical(impl_->db,"SELECT start,end,device,kind,state,start_key,end_key FROM bookings WHERE id=?");physical.text(1,run);
            if(physical.step()==SQLITE_ROW){
                if(exactMetadataText(physical,0)!=a||exactMetadataText(physical,1)!=b||
                   exactMetadataText(physical,2)!=canonicalDevice||exactMetadataText(physical,3)!="RUNTIME"||
                   exactMetadataText(physical,5)!=key(a)||exactMetadataText(physical,6)!=key(b))
                    throw std::runtime_error("Queued SOURCE physical run rebound or malformed");
                const auto state=exactMetadataText(physical,4);
                if(state!="ACTIVE"&&state!="PAUSED"&&state!="SOURCE_FINISHED")
                    throw std::runtime_error("Queued SOURCE physical state unavailable");
                physicalFinished=state=="SOURCE_FINISHED";
            }
        }
        Statement table(impl_->db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='dispatch_requests'");
        const bool hasDispatch=table.step()==SQLITE_ROW;
        if(hasDispatch){
            Statement mapped(impl_->db,"SELECT id FROM dispatch_requests WHERE run=? OR CAST(run AS TEXT)=?");mapped.text(1,run);mapped.text(2,run);
            while(mapped.step()==SQLITE_ROW){
                if(!queuedIntent||exactMetadataText(mapped,0)!=*queuedIntent)
                    throw std::runtime_error("Queued SOURCE mapped run requires its exact explicit intent");
            }
        }
        bool intentFinished=false,dispatchFinished=false;
        if(queuedIntent){
            if(!hasDispatch)throw std::runtime_error("Queued SOURCE dispatch table missing");
            Statement dispatch(impl_->db,"SELECT id,start,end,device,state,predecessor,run FROM dispatch_requests WHERE id=?");dispatch.text(1,*queuedIntent);
            if(dispatch.step()!=SQLITE_ROW)throw std::runtime_error("Queued SOURCE dispatch missing");
            const auto d=initialDispatch(dispatch);
            if(d.start!=a||d.end!=b||d.device!=canonicalDevice||
               (d.state!="PREPARED"&&d.state!="STARTED"&&d.state!="SOURCE_FINISHED")||
               (d.run&&*d.run!=run)||(!d.run&&d.state!="PREPARED"))
                throw std::runtime_error("Queued SOURCE dispatch identity/state contradicts receipt");
            // Never pick an intent by overlap or repair an unknown local row.
            validateDispatchBooking(impl_->db,d);
            Statement intent(impl_->db,"SELECT state,start_key,end_key FROM bookings WHERE id=?");intent.text(1,*queuedIntent);
            if(intent.step()!=SQLITE_ROW||exactMetadataText(intent,1)!=key(a)||exactMetadataText(intent,2)!=key(b))
                throw std::runtime_error("Queued SOURCE local intent keys malformed");
            const auto state=exactMetadataText(intent,0);
            if(state!="DISPATCHING"&&state!="DISPATCHED"&&state!="SOURCE_FINISHED")
                throw std::runtime_error("Queued SOURCE local intent is not dispatched");
            intentFinished=state=="SOURCE_FINISHED";dispatchFinished=d.state=="SOURCE_FINISHED";
        }
        if(physicalFinished&&(!queuedIntent||(intentFinished&&dispatchFinished)))return;
        if(!physicalFinished)impl_->runtime(run,a,b,canonicalDevice,"SOURCE_FINISHED");
        if(queuedIntent){
            if(!intentFinished){Statement q(impl_->db,"UPDATE bookings SET state='SOURCE_FINISHED' WHERE id=?");q.text(1,*queuedIntent);q.step();}
            if(!dispatchFinished){Statement q(impl_->db,"UPDATE dispatch_requests SET state='SOURCE_FINISHED',run=? WHERE id=?");q.text(1,run);q.text(2,*queuedIntent);q.step();}
        }
    }).left(512);
}
}

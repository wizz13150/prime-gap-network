#include "mining/native_control_command.h"
#include "mining/run_source_profile.h"
#include "booking/decimal_range.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QMetaType>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

namespace pgn::mining {
namespace {
using namespace booking;
[[noreturn]] void fail(const char* message){throw NativeControlError(NativeControlErrorCode::InvalidContract,message);}
void require(bool value,const char* message){if(!value)fail(message);}
void text(const QString& value,qsizetype limit=4096){require(value.size()<=limit&&!value.contains(QChar(0))&&QString::fromUtf8(value.toUtf8())==value,"Exact bounded UTF8 text required");}
void uuid(const QString& value){const QUuid id(value);require(!id.isNull()&&id.toString(QUuid::WithoutBraces)==value,"Canonical nonnull UUID required");}
void run(const QString& value){require(value.size()==32&&QRegularExpression("^[0-9a-f]{32}$").match(value).hasMatch(),"Exact lowercase run32 required");}
void hash(const QString& value){require(value.size()==64&&QRegularExpression("^[0-9a-f]{64}$").match(value).hasMatch(),"Exact lowercase SHA256 required");}
void target(const QString& value){require(value=="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac","No native-control v1 ABI for this device");}
void decimal(const QString& value){text(value,160);require(!value.isEmpty()&&canonicalUnsigned(value)==value,"Canonical unsigned decimal string required");}
void span(const QString& first,const QString& last){decimal(first);decimal(last);require(compareUnsigned(first,last)<0,"Nonempty exact range required");}
void utc(const QString& value){
    const auto match=QRegularExpression("^([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2})(?:\\.[0-9]{1,9})?Z$").match(value);
    require(match.hasMatch()&&match.capturedLength()==value.size(),"Exact UTC birth required");
    const auto date=QDateTime::fromString(match.captured(1)+'Z',Qt::ISODate);
    require(date.isValid()&&date.toUTC().toString("yyyy-MM-dd'T'HH:mm:ss")==match.captured(1),"Invalid UTC calendar fields");
}
void keys(const QJsonObject& object,QStringList expected){auto actual=object.keys();actual.sort();expected.sort();require(actual==expected,"JSON contract keys differ");}
QString field(const QJsonObject& object,const char* name,qsizetype limit=4096){const auto value=object.value(QLatin1String(name));require(value.isString(),"Required exact JSON string absent/type invalid");const auto result=value.toString();text(result,limit);return result;}
QJsonObject objectField(const QJsonObject& object,const char* name){require(object.value(QLatin1String(name)).isObject(),"Required JSON object absent/type invalid");return object.value(QLatin1String(name)).toObject();}
QString configuredPath(QString value){
    text(value);require(QRegularExpression("^[A-Za-z]:/").match(value).hasMatch()&&!value.contains('\\')&&QDir::cleanPath(value)==value,"Configured image must be canonical absolute Windows drive path");
    for(const auto c:value)require(c.unicode()>=32&&c.unicode()!=127,"Configured image path has controls");return value;
}
QJsonObject configuredTuple(const NativeConfiguredProfileObservation& p){
    target(p.device);configuredPath(p.executable);hash(p.imageSha256.toLower());hash(p.bindingSha256.toLower());
    require(p.imageSha256==p.imageSha256.toUpper()&&p.bindingSha256==p.bindingSha256.toUpper(),"Configured hashes must retain uppercase wire representation");
    return {{"device",p.device},{"executable",p.executable},{"image_sha256",p.imageSha256},{"binding_sha256",p.bindingSha256}};
}
void configuredBinding(const NativeConfiguredProfileObservation& p){
    require(p.binding.value("uuid")==p.device,"Configured binding device differs");
    auto path=field(p.binding,"executable");path.replace('\\','/');
    require(QDir::cleanPath(path)==p.executable&&p.binding.value("pins").isArray(),"Configured binding image/pins differ");
    int matches{};QSet<QString> paths;
    for(const auto& item:p.binding.value("pins").toArray()){
        require(item.isObject(),"Configured pin must be object");const auto pin=item.toObject();auto pinPath=field(pin,"path");pinPath.replace('\\','/');pinPath=QDir::cleanPath(pinPath);configuredPath(pinPath);
        const auto sha=field(pin,"sha256");hash(sha.toLower());require(!paths.contains(pinPath.toCaseFolded()),"Configured duplicate pin path");paths.insert(pinPath.toCaseFolded());
        if(pinPath.compare(p.executable,Qt::CaseInsensitive)==0){++matches;require(sha.toUpper()==p.imageSha256,"Configured executable pin differs");}
    }
    require(matches==1,"Configured executable pin absent/ambiguous");
}
NativeConfiguredProfileObservation configuredObservation(const QJsonObject& packet){
    keys(packet,{"schema","protocol","status","profile","binding","source_capacity"});
    require(packet.value("schema")=="pgn-configured-profile-observation-1"&&packet.value("protocol")=="pgn-configured-stop-1"&&packet.value("status")=="CONFIGURED_PROFILE_OBSERVED","Unsupported configured profile observation ABI");
    const auto tuple=objectField(packet,"profile");keys(tuple,{"device","executable","image_sha256","binding_sha256"});
    NativeConfiguredProfileObservation p;p.protocol=field(packet,"protocol");p.device=field(tuple,"device");p.executable=field(tuple,"executable");p.imageSha256=field(tuple,"image_sha256");p.bindingSha256=field(tuple,"binding_sha256");
    configuredTuple(p);p.binding=objectField(packet,"binding");configuredBinding(p);
    const auto cap=packet.value("source_capacity");require(cap.isNull()||cap.isObject(),"Configured source capacity must be explicit null or typed object");
    if(cap.isObject()&&cap.toObject().value("schema")=="pgn-prime-gap-source-capacity-1"){
        const auto c=cap.toObject();keys(c,{"schema","policy","sdk","scope_start","scope_end","max_length","guard","source_manifest_sha256","job"});
        const auto sdk=configuredPath(field(c,"sdk"));
        require(c.value("policy")=="ADAPTIVE_PRIME_GAP_67_95"&&c.value("guard")=="PRIME_GAP_PLAN_REQUIRED"
            &&c.value("source_manifest_sha256")==primeGapSourceManifest()&&c.value("scope_start")=="73786976294838206464"
            &&c.value("scope_end")=="39614081257132168796771975168"&&c.value("max_length")=="1000000000000000"
            &&p.binding.value("sdk")==sdk&&p.binding.value("source_boundary_protocol")=="PGN_NATIVE_GAP_RUNTIME_V1","Adaptive source capacity declaration differs");
        const auto engine=objectField(p.binding,"engine");
        keys(engine,{"schema","build_arch","profile_k","minimum_gap","image_sha256","source_manifest_sha256"});
        const auto arch=primeGapReleaseArchitecture(p.imageSha256);
        require(engine.value("schema")=="pgn-prime-gap-engine-1"&&arch&&engine.value("build_arch")==*arch
            &&engine.value("image_sha256")==p.imageSha256&&engine.value("source_manifest_sha256")==primeGapSourceManifest(),"Adaptive binding differs from actual image/source identity");
        const QJsonObject sourceProfile{{"schema","pgn-run-source-profile-2"},{"protocol","PGN_NATIVE_GAP_RUNTIME_V1"},
            {"image_sha256",p.imageSha256.toLower()},{"executable",p.executable},{"source_manifest_sha256",primeGapSourceManifest().toLower()},
            {"checkpoint_schema","PGNS_GAP32_64B"},{"job",objectField(c,"job")}};
        const auto known=inspectRunSourceProfile({{"start",p.binding.value("start")},{"end",p.binding.value("end")},{"source_profile",sourceProfile}});
        require(known.status==RunSourceProfileStatus::KnownAdaptivePrimeGap&&known.engineJob,"Adaptive current job profile is invalid");
        const auto& job=*known.engineJob;
        require(engine.value("profile_k")==job.descriptorK&&field(p.binding,"length")==job.length,"Adaptive binding geometry differs from exact job");
        const auto requested=engine.value("minimum_gap");
        require((requested=="auto"&&job.minimumGap==primeGapMinimumAt(job.start)
            &&job.minimumGap==primeGapMinimumAt(subtractUnsigned(job.end,"1")))
            ||(requested.isString()&&requested.toString()==QString::number(job.minimumGap))
            ||(requested.isDouble()&&requested.toDouble()==double(job.minimumGap)),"Adaptive requested threshold contradicts current job");
        const auto expected=primeGapRuntimePins(sdk,p.executable,p.imageSha256);const auto pins=p.binding.value("pins").toArray();
        require(pins.size()==expected.size(),"Adaptive engine complete runtime/proof closure is absent");
        for(const auto value:pins){const auto pin=value.toObject();require(expected.value(field(pin,"path"))==field(pin,"sha256"),"Adaptive engine runtime/proof pin differs");}
        NativeQualifiedSourceCapacity q;q.policy=field(c,"policy");q.sdk=sdk;q.scopeStart=field(c,"scope_start");q.scopeEnd=field(c,"scope_end");
        q.maximumLength=field(c,"max_length");q.guard=field(c,"guard");q.sourceManifestSha256=field(c,"source_manifest_sha256");q.engineJob=job;q.nativePlanRequired=true;
        p.sourceCapacity=std::move(q);return p;
    }
    if(cap.isObject()){
        const auto c=cap.toObject();keys(c,{"schema","policy","sdk","scope_start","scope_end","max_length","guard","qualification_sha256"});
        NativeQualifiedSourceCapacity q{field(c,"policy"),field(c,"sdk"),field(c,"scope_start"),field(c,"scope_end"),field(c,"max_length"),field(c,"guard"),field(c,"qualification_sha256")};
        // Reuse the one exact image/path/protocol table. This is declaration
        // consistency, NOT a local file rehash or permission to launch. Legacy
        // A73 may lack its historical declaration; v2 must declare itself.
        const auto protocol=p.binding.contains("source_boundary_protocol")?field(p.binding,"source_boundary_protocol"):QString("pgn-source-legacy-a73");
        const auto known=inspectRunSourceProfile(QJsonObject{{"source_profile",QJsonObject{{"schema","pgn-run-source-profile-1"},{"protocol",protocol},{"image_sha256",p.imageSha256.toLower()},{"executable",p.executable}}}});
        require(known.status==RunSourceProfileStatus::KnownLegacyA73||known.status==RunSourceProfileStatus::KnownBoundaryV2||known.status==RunSourceProfileStatus::KnownLazyV1||known.status==RunSourceProfileStatus::KnownLazySharedV1||known.status==RunSourceProfileStatus::KnownLazySharedPeriodicV1||known.status==RunSourceProfileStatus::KnownLazySharedPeriodicBankV1||known.status==RunSourceProfileStatus::KnownRadix70V1||known.status==RunSourceProfileStatus::KnownFloatPrefixV1||known.status==RunSourceProfileStatus::KnownBarrett70V1||known.status==RunSourceProfileStatus::KnownWindow2Recip5V1||known.status==RunSourceProfileStatus::KnownTop2V1||known.status==RunSourceProfileStatus::KnownRollingRootsV1||known.status==RunSourceProfileStatus::KnownRollingRootsLanes2V1||known.status==RunSourceProfileStatus::KnownCarryChainV1||known.status==RunSourceProfileStatus::KnownFusedSquareV1||known.status==RunSourceProfileStatus::KnownFusedHigh47V1||known.status==RunSourceProfileStatus::KnownFusedWindow3RingV1,"Configured source image/path/protocol not qualified");
        const bool v2=known.status==RunSourceProfileStatus::KnownBoundaryV2;
        const bool lazy=known.status==RunSourceProfileStatus::KnownLazyV1;
        const bool shared=known.status==RunSourceProfileStatus::KnownLazySharedV1;
        const bool periodic=known.status==RunSourceProfileStatus::KnownLazySharedPeriodicV1;
        const bool bank=known.status==RunSourceProfileStatus::KnownLazySharedPeriodicBankV1;
        const bool radix70=known.status==RunSourceProfileStatus::KnownRadix70V1;
        const bool floatPrefix=known.status==RunSourceProfileStatus::KnownFloatPrefixV1;
        const bool barrett70=known.status==RunSourceProfileStatus::KnownBarrett70V1;
        const bool window2Recip5=known.status==RunSourceProfileStatus::KnownWindow2Recip5V1;
        const bool top2=known.status==RunSourceProfileStatus::KnownTop2V1;
        const bool rollingRoots=known.status==RunSourceProfileStatus::KnownRollingRootsV1;
        const bool rollingRootsLanes2=known.status==RunSourceProfileStatus::KnownRollingRootsLanes2V1;
        const bool carryChain=known.status==RunSourceProfileStatus::KnownCarryChainV1;
        const bool fusedHigh47=known.status==RunSourceProfileStatus::KnownFusedHigh47V1;
        const bool fusedSquare=known.status==RunSourceProfileStatus::KnownFusedSquareV1;
        const bool fusedWindow3Ring=known.status==RunSourceProfileStatus::KnownFusedWindow3RingV1;
        const bool fusedSeed8R=fusedWindow3Ring&&p.imageSha256=="CA363CB76CBAF9A008402E0D47B80DC6DC846C331EFA1BD42DAF91DE15DFC6B3";
        const bool fusedFrontier16=fusedWindow3Ring&&p.imageSha256=="122ABD3DA35ADFCE429325F3ED0E16CCA8B38B78BC144A11722A9AC342793952";
        const bool fusedEvent210Hybrid=fusedWindow3Ring&&p.imageSha256=="3121A85578AEBC61648311216A468574745447A52FED7088285D2222C029D1E3";
        const bool fusedEvent210Composed=fusedWindow3Ring&&p.imageSha256=="B0439859530229D2C3451690C3A0361A78FE570CDFF14F131EFB1B8A22253C43";
        const bool fusedSmallword7=fusedWindow3Ring&&p.imageSha256=="85412118A5686090FA612C090C6C43DC9222231E4AC08A39BC594F4D4235A945";
        const bool fusedEventPair16=fusedWindow3Ring&&p.imageSha256=="843BCADBAA279A19E5CD0D6C54ABD386DDEF47B10B1F2E41A8DA32C6390A0428";
        const bool fusedPrefix11Final=fusedWindow3Ring&&p.imageSha256=="CF3C215BF30140F89D04324377F64694D106852120634A47AF6B08558A6294ED";
        const bool fusedStatefulU160=fusedWindow3Ring&&p.imageSha256=="CCD3D4FDA96B8C26B74947B6FC8399D5502EFBF55A4AEFCABFC92659C6277673";
        const bool fusedLiveComposition22=fusedWindow3Ring&&p.imageSha256=="962A98E4D6FD97390CE20FF99143C7BC1BEFD7A1093998D8B73F730D28525F67";
        const QString sdk=fusedLiveComposition22?"C:/PGN/native/20261021-live-composition-v22":fusedStatefulU160?"C:/PGN/native/20261020-stateful-u160-live-v1":fusedPrefix11Final?"C:/PGN/native/20261019-prefix11-final-v1":fusedEventPair16?"C:/PGN/native/20261019-event-pair16-v1":fusedSmallword7?"C:/PGN/native/20261019-event210-smallword7-v1":fusedEvent210Composed?"C:/PGN/native/20261019-frontier16-event210-composed-v1":fusedEvent210Hybrid?"C:/PGN/native/20261019-frontier16-event210-hybrid-v1":fusedFrontier16?"C:/PGN/native/20261019-frontier16-retry-v2":fusedSeed8R?"C:/PGN/native/20261018-fused-window3-seed8r-v1":fusedWindow3Ring?"C:/PGN/native/20261018-fused-window3-ring-v1":fusedHigh47?"C:/PGN/native/20261018-fused-high47-v1":fusedSquare?"C:/PGN/native/20261018-fused-square-v1":carryChain?"C:/PGN/native/20261018-carry-chain-v1":rollingRootsLanes2?"C:/PGN/native/20261017-rolling-roots-lanes2-v1":rollingRoots?"C:/PGN/native/20261017-rolling-roots-v1":top2?"C:/PGN/native/20261017-barrett70-top2-v1":window2Recip5?"C:/PGN/native/20261017-window2-recip5-v1":barrett70?"C:/PGN/native/20261017-barrett70-v1":floatPrefix?"C:/PGN/native/20261017-radix70-float-prefix-v1":radix70?"C:/PGN/native/20261017-radix70-v1":bank?"C:/PGN/native/20261017-lazy-shared-periodic-bank-v1":periodic?"C:/PGN/native/20261017-lazy-shared-periodic-v1":shared?"C:/PGN/native/20261017-lazy-shared-v1":lazy?"C:/PGN/native/20261017-lazy-integer-v1":v2?"C:/PGN/native/20261015-source-boundary-v2":"C:/PGN/native/20261012-gpu-owned-source-v1";
        const QString policy=fusedWindow3Ring?"GPU_OWNED_HIGH10_FUSED_WINDOW3_RING_V1":fusedHigh47?"GPU_OWNED_HIGH47_BARRETT_TWO_LANES_FUSED_SQUARE_V1":fusedSquare?"GPU_OWNED_HIGH10_BARRETT70_TOP2_ROLLING_ROOTS_TWO_LANES_FUSED_SQUARE_V1":carryChain?"GPU_OWNED_HIGH10_BARRETT70_TOP2_ROLLING_ROOTS_TWO_LANES_CARRY_V1":rollingRootsLanes2?"GPU_OWNED_HIGH10_BARRETT70_TOP2_ROLLING_ROOTS_TWO_LANES_SHARED_PERIODIC_BANK_V1":rollingRoots?"GPU_OWNED_HIGH10_BARRETT70_TOP2_ROLLING_ROOTS_SHARED_PERIODIC_BANK_V1":top2?"GPU_OWNED_HIGH10_BARRETT70_TOP2_SHARED_PERIODIC_BANK_V1":window2Recip5?"GPU_OWNED_HIGH10_BARRETT70_WINDOW2_RECIP5_SHARED_PERIODIC_BANK_V1":barrett70?"GPU_OWNED_HIGH10_BARRETT70_SHARED_PERIODIC_BANK_V1":floatPrefix?"GPU_OWNED_HIGH10_RADIX70_FLOAT_PREFIX_SHARED_PERIODIC_BANK_V1":radix70?"GPU_OWNED_HIGH10_RADIX70_SHARED_PERIODIC_BANK_V1":bank?"GPU_OWNED_HIGH10_LAZY_INTEGER_SHARED_PERIODIC_BANK_V1":periodic?"GPU_OWNED_HIGH10_LAZY_INTEGER_SHARED_PERIODIC_V1":shared?"GPU_OWNED_HIGH10_LAZY_INTEGER_SHARED_V1":lazy?"GPU_OWNED_HIGH10_LAZY_INTEGER_V1":v2?"GPU_OWNED_HIGH10_SOURCE_BOUNDARY_V2":"GPU_OWNED_HIGH10_SOURCE_V1";
        const QString qualification=fusedLiveComposition22?"0406894ADEF8EF475CF81418964586567465A634CC9C06556700EE3A7F137B6A":fusedStatefulU160?"F71920DE3A8246BBB3D552107BD6D9BBE6F0049E485923098C7A20438C0BF4AB":fusedPrefix11Final?"B9CD21629CFEB42E95C829E862E8B7300CC2CF0460BD3C691B3C4DBB08943F30":fusedEventPair16?"6FF25670C9903DD8F958821CEA5A3960ADAEC3CA259D63ECC1AAB03681A7CB86":fusedSmallword7?"DC503F08ACDC55469F1AF482B1300CB611E177BBA6FD0F5AFD1BBDE6D2E1DDCB":fusedEvent210Composed?"35EE4605BF309A96973E0D1DF875E29382B0533A6AEBFECA357349C49E1E8227":fusedEvent210Hybrid?"6598C50D1AA41ECF97F4006ED5721E767399EB55C2EBB4A6F630FA26E13D5AEF":fusedFrontier16?"9C2C3ED3357A077E8CCD7F60CB400F39DDF4CF25624ED5CCE80D46ABB62B9249":fusedSeed8R?"352F5A43AD79FE24A49F4EB1DECF6D1D7A3A810301D5CD73F48E4CC9691C7548":fusedWindow3Ring?"9B560C15DABBB26BFE22FD4C9D0041AF17B68C273F0211FD45ACEC4C98BF3415":fusedHigh47?"C09786DACA5D6A6200BC71352BFAE319AB7B731AC0D9F79045BB494B25975EE9":fusedSquare?"57503A214735EA72F336EA15EAF02418F3921C7622483B2ACDBCED10CF3D3B18":carryChain?"9DF2C6B6605792A9E15D9D4BA627521178FAD0686655559975768A31B482A9AF":rollingRootsLanes2?"5C18FB2BAFB97E2743E804C2B55B46882FE0FC913983817AAFFEAD039672BDC2":rollingRoots?"419DA06E9D15A55A8452181E402F086BCDAAC5F1463A2F39C73A7A22B2C86E08":top2?"CC3554D5350D8C9551C2A39F66651504E089ABF5AF290DB538C93E3A884801AC":window2Recip5?"DB6F8E08FAA9AB0E7A89ED1305E3B298E854A680851B2418D54175F4EADA0D26":barrett70?"A758E586FE3F31EF40D0F07A80857758D8835B0A1E1E1594E7AE02B2490EC1CF":floatPrefix?"5EC12E85699ACA23A507E20966168F4D611BA27258FD6BF56B517B14651C1C70":radix70?"5C74B73EA8A22A11339595BBA9EB5592253B8ADB64D8FCF142EE66C2E0B94675":bank?"FC752A691686BA693EAFBB1273E0C1EB7B9CC5E47B9217B2E4F69E329101A5A8":periodic?"2FC9F2DB60CF745EB28DA80AA3E96CEAC9397E91A6185F314E1E87D7BD7D84AF":shared?"0A58315DB343DEDBECF732CEF9FD13E35DBF505872899B59D88784FE7502823B":lazy?"37D549CB05AA4E47E423FB554F1DFEEA6FB5B10CAEC3A37D9E78F33843C168FD":v2?"2DB06EAD094579A18588BBEF6429102A5E59052BCF2D09E3A13E0E8E84964C52":"99E5823D17AA6B7D202B205D797C0C1E18C807D5CCC93CBA2262F35FCDF72684";
        require(c.value("schema")=="pgn-qualified-source-capacity-1"&&q.policy==policy&&q.guard=="Assert-PgnQualifiedSourceRange"&&q.sdk==sdk&&p.binding.value("sdk")==sdk&&p.executable==sdk+"/bin/pgn-continuous-gpu.exe"&&
            q.scopeStart==(fusedHigh47?"866996971464348925952":"184467440737095516160")&&q.scopeEnd==(fusedHigh47?"885443715538058477568":"202914184810805067776")&&q.maximumLength=="1000000000000000"&&q.qualificationSha256==qualification,"Unsupported qualified source capacity metadata");
        const auto upgrade=objectField(p.binding,"engine_upgrade");require(upgrade.value("policy")==q.policy&&upgrade.value("qualification_sha256")==q.qualificationSha256,"Capacity contradicts binding profile");
        const auto pins=p.binding.value("pins").toArray();require(pins.size()==2,"Qualified SDK requires both closure pins");
        const QHash<QString,QString> expected{{sdk+"/bin/pgn-continuous-gpu.exe",p.imageSha256},{sdk+"/bin/cudart64_12.dll","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"}};
        for(const auto& value:pins){const auto pin=value.toObject();require(expected.contains(field(pin,"path"))&&expected.value(field(pin,"path"))==field(pin,"sha256"),"Qualified SDK pin declaration differs");}
        p.sourceCapacity=std::move(q);
    }
    return p;
}
QString psQuote(QString value){value.replace("'","''");return "'"+value+"'";}
QJsonObject canonicalObject(const QByteArray& raw,qsizetype limit){
    require(!raw.isEmpty()&&raw.size()<=limit&&QString::fromUtf8(raw).toUtf8()==raw,"Native receipt UTF8/size invalid");
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(raw,&error);
    // Exact re-encoding rejects duplicate/escaped alias keys, noncanonical
    // representations and hidden whitespace. Only native canonical receipts.
    require(error.error==QJsonParseError::NoError&&doc.isObject()&&doc.toJson(QJsonDocument::Compact)==raw,"Native receipt JSON noncanonical/corrupt");return doc.object();
}
QByteArray utf16le(const QString& script){QByteArray bytes;bytes.reserve(script.size()*2);for(const auto c:script){bytes.append(char(c.unicode()&255));bytes.append(char(c.unicode()>>8));}return bytes;}
QJsonObject baseObject(const NativeObservedBase& base){run(base.run);utc(base.birth);span(base.start,base.end);return {{"base_run",base.run},{"base_birth",base.birth},{"base_start",base.start},{"base_end",base.end}};}
bool multiple(QString value,const QString& divisor){QString remainder="0";for(const auto digit:value){remainder=canonicalUnsigned(remainder+digit);while(compareUnsigned(remainder,divisor)>=0)remainder=subtractUnsigned(remainder,divisor);}return remainder=="0";}
quint64 word(const QByteArray& bytes,int index){quint64 result=0;for(int i=0;i<8;++i)result|=quint64(quint8(bytes[8+index*8+i]))<<(8*i);return result;}
void checkpoint(const QJsonObject& cp,const CheckpointResumeEpoch& e){
    keys(cp,{"schema","device","resume","checkpoint_base64"});require(cp.value("schema")=="pgn-source-resume-1"&&cp.value("device")==e.device.toLower(),"Checkpoint envelope identity differs");
    const auto tuple=objectField(cp,"resume");const auto& r=e.checkpoint.resume;
    const QJsonObject expected{{"run",r.run},{"start",r.start},{"end",r.end},{"length",r.length},{"positions",r.positions},
        {"nextStart",r.nextStart},{"remaining",r.remaining},{"tests",r.tests},{"gaps",r.gaps},{"batches",r.batches},
        {"counterTimeNs",r.counterTimeNs},{"checkpointLabel",r.checkpointLabel},{"checkpointSha256",r.checkpointSha256}};
    require(tuple==expected,"Checkpoint13tuple differs from native readback");run(r.run);require(r.run==e.previousRun,"Checkpoint previous run differs");span(r.start,r.end);
    for(const auto* name:{"length","positions","tests","gaps","batches","counterTimeNs"}){const auto value=field(tuple,name);decimal(value);bool ok=false;value.toULongLong(&ok);require(ok,"Checkpoint integer exceeds uint64");}
    const auto bytes=e.checkpoint.checkpointBytes;const auto b64=field(cp,"checkpoint_base64").toLatin1();
    const bool adaptive=r.dialect==SourceCheckpointDialect::ScoutGapBound;
    require(bytes.size()==64&&(adaptive?bytes.left(4)=="PGNS":bytes.left(8)=="PGNCG001")&&bytes.toBase64()==b64,"Checkpoint exact64 layout/base64 differs");
    if(adaptive){quint32 minimum=0;for(unsigned i=0;i<4;++i)minimum|=quint32(uchar(bytes[4+i]))<<(8*i);
        require(r.gapMinimum&&*r.gapMinimum==minimum&&minimum>=primeGapMinimumFloor()&&minimum<=2147483647U,"Checkpoint threshold differs from native stored readback");}
    hash(r.checkpointSha256);require(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()==r.checkpointSha256.toLatin1(),"Checkpoint SHA differs");
    QString start=QString::number(word(bytes,1));for(int i=0;i<64;++i)start=addUnsigned(start,start);start=addUnsigned(start,QString::number(word(bytes,0)));
    require(start==r.start&&QString::number(word(bytes,2))==r.length&&QString::number(word(bytes,3))==r.positions&&QString::number(word(bytes,4))==r.tests&&
        QString::number(word(bytes,5))==r.gaps&&QString::number(word(bytes,6))==r.batches,"Checkpoint bytes/counters differ");
    require(compareUnsigned(r.positions,r.length)<0&&addUnsigned(r.start,r.length)==r.end&&addUnsigned(r.start,r.positions)==r.nextStart&&subtractUnsigned(r.length,r.positions)==r.remaining,"Checkpoint partial arithmetic differs");
    text(r.checkpointLabel,1024);require(!r.checkpointLabel.isEmpty()&&!r.checkpointLabel.contains('\r')&&!r.checkpointLabel.contains('\n'),"Checkpoint label invalid");
}
// QJson is the grammar authority; this bounded walker ONLY rejects duplicate
// decoded keys (including escaped aliases) inside a successfully parsed result.
// Unlike native receipts, PowerShell results legitimately contain whitespace.
void skip(const QByteArray& b,qsizetype& i){while(i<b.size()&&(b[i]==' '||b[i]=='\r'||b[i]=='\n'||b[i]=='\t'))++i;}
qsizetype stringEnd(const QByteArray& b,qsizetype i){require(i<b.size()&&b[i]=='"',"JSON key missing");while(++i<b.size()){if(b[i]=='\\')++i;else if(b[i]=='"')return i+1;}fail("JSON string incomplete");}
void uniqueValue(const QByteArray& b,qsizetype& i,int depth,const QString& path,QHash<QString,QByteArray>& scalars){
    require(depth<=16,"Control JSON nesting exceeds16");skip(b,i);require(i<b.size(),"JSON value incomplete");
    if(b[i]=='"'){i=stringEnd(b,i);return;}
    const bool object=b[i]=='{';if(object||b[i]=='['){const auto end=object?'}':']';++i;skip(b,i);QSet<QString> seen;
        while(i<b.size()&&b[i]!=end){QString key="[]";if(object){const auto first=i;i=stringEnd(b,i);const auto parsed=QJsonDocument::fromJson("["+b.mid(first,i-first)+"]").array();require(parsed.size()==1,"JSON key decode failed");key=parsed[0].toString();require(!seen.contains(key),"Duplicate decoded control-result key");seen.insert(key);skip(b,i);require(i<b.size()&&b[i++]==':',"JSON separator missing");}
            uniqueValue(b,i,depth+1,path+'/'+key,scalars);skip(b,i);if(i<b.size()&&b[i]==','){++i;skip(b,i);}else break;}
        require(i<b.size()&&b[i++]==end,"JSON container incomplete");return;}
    const auto first=i;while(i<b.size()&&b[i]!=','&&b[i]!='}'&&b[i]!=']')++i;scalars.insert(path,b.mid(first,i-first).trimmed());
}
QJsonObject response(const QByteArray& raw,QHash<QString,QByteArray>& scalars){require(!raw.isEmpty()&&raw.size()<=NativeControlCommand::ResponseLimit&&QString::fromUtf8(raw).toUtf8()==raw,"Control response UTF8/size invalid");QJsonParseError error;const auto doc=QJsonDocument::fromJson(raw,&error);require(error.error==QJsonParseError::NoError&&doc.isObject(),"Complete control JSON object required");qsizetype at=0;uniqueValue(raw,at,0,{},scalars);skip(raw,at);require(at==raw.size(),"Trailing control response");return doc.object();}
qsizetype windowsArgumentLength(const QString& value){
    if(!value.isEmpty()&&!value.contains(' ')&&!value.contains('\t')&&!value.contains('"'))return value.size();
    qsizetype n=2,slashes=0;for(const auto c:value){if(c=='\\'){++slashes;continue;}n+=slashes*(c=='"'?2:1)+(c=='"'?2:1);slashes=0;}return n+slashes*2;
}
}
void NativeControlCommand::finish(const SshObservationEndpoint& endpoint,const QString& script){
    // Reuse EXACT existing direct-endpoint validation and trust-store policy;
    // this calls no QProcess and reads no key contents.
    ssh_=makeSshObservationCommand(endpoint,SshObservationRequest{});
    const auto body=nativeControlScriptForEndpoint(endpoint,script);
    const auto guarded=QString("[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);$OutputEncoding=[Console]::OutputEncoding;$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';try{%1}catch{[Console]::Error.WriteLine($_.Exception.Message);exit 1}").arg(body);
    const auto encoded=utf16le(guarded).toBase64();require(encoded.size()<=28000,"Windows encoded control argument exceeds28000");
    ssh_.arguments.last()="powershell.exe -NoProfile -NonInteractive -EncodedCommand "+QString::fromLatin1(encoded);
    qsizetype length=windowsArgumentLength(ssh_.executable)+1;for(const auto& arg:ssh_.arguments)length+=windowsArgumentLength(arg)+1;
    require(length<32767,"Windows process command line exceeds conservative bound");
}
NativeControlCommand NativeControlCommand::initial(const SshObservationEndpoint& endpoint,const InitialStartResult& prepared,const QByteArray& raw,const std::optional<NativeObservedBase>& base){
    require(prepared.status==InitialStartStatus::Prepared&&!prepared.replayed&&prepared.error.isEmpty()&&prepared.dispatch.has_value(),"Only newly native PREPARED initial admission, never reconciliation");
    const auto& d=*prepared.dispatch;target(d.device);uuid(d.bookingId);span(d.start,d.end);require(d.state=="PREPARED"&&d.origin=="INITIAL_START"&&!d.run&&!d.predecessor&&d.automatic.has_value(),"Initial dispatch tuple differs");
    const auto receipt=canonicalObject(raw,8192);keys(receipt,{"schema","request_id","device","input_fingerprint","observation_id","mode","booking_id","start","end","origin","scope","registry_provenance"});
    const auto request=field(receipt,"request_id"),observation=field(receipt,"observation_id");uuid(request);uuid(observation);
    require(receipt.value("schema")=="pgn-initial-start-receipt-1"&&receipt.value("origin")=="INITIAL_START"&&receipt.value("device")==d.device&&receipt.value("booking_id")==d.bookingId&&receipt.value("start")==d.start&&receipt.value("end")==d.end,"Initial native receipt/dispatch differs");
    const auto mode=*d.automatic?QString("automatic"):QString("manual-reservations");require(receipt.value("mode")==mode&&prepared.mode==(*d.automatic?MiningSessionMode::Automatic:MiningSessionMode::ManualReservations),"Initial mode differs");
    const auto scope=objectField(receipt,"scope");const QStringList scopeKeys{"execution_start","execution_end","auto_start","auto_end","block_anchor","block_width"};keys(scope,scopeKeys);for(const auto& name:scopeKeys)decimal(field(scope,name.toLatin1().constData()));
    span(field(scope,"execution_start"),field(scope,"execution_end"));span(field(scope,"auto_start"),field(scope,"auto_end"));require(compareUnsigned(field(scope,"auto_start"),field(scope,"execution_start"))>=0&&compareUnsigned(field(scope,"auto_end"),field(scope,"execution_end"))<=0&&compareUnsigned(d.start,field(scope,"execution_start"))>=0&&compareUnsigned(d.end,field(scope,"execution_end"))<=0,"Initial range outside admitted execution scope");
    require(field(scope,"block_width")!="0"&&compareUnsigned(field(scope,"block_anchor"),field(scope,"execution_start"))<=0,"Initial block width/anchor invalid");
    if(*d.automatic){const auto start=field(scope,"auto_start"),end=field(scope,"auto_end"),anchor=field(scope,"block_anchor"),width=field(scope,"block_width");const auto distance=compareUnsigned(d.start,anchor)>=0?subtractUnsigned(d.start,anchor):subtractUnsigned(anchor,d.start);require(compareUnsigned(d.start,start)>=0&&compareUnsigned(d.end,end)<=0&&subtractUnsigned(d.end,d.start)==width&&multiple(distance,width),"Initial Auto grid/range differs");}
    const auto provenance=objectField(receipt,"registry_provenance");keys(provenance,{"url","sha256","observed_utc","semantic_digest"});const auto url=field(provenance,"url"),observed=field(provenance,"observed_utc");require(!url.isEmpty()&&url.toUtf8().size()<=4096&&!url.contains('\r')&&!url.contains('\n'),"Registry URL absent/invalid");hash(field(provenance,"sha256"));hash(field(provenance,"semantic_digest"));utc(observed);require(QDateTime::fromString(observed,Qt::ISODateWithMs).toUTC().toString(Qt::ISODateWithMs)==observed,"Native registry timestamp differs from canonical Qt milliseconds");
    QJsonArray fingerprint{request,d.device.toLower()};for(const auto& name:scopeKeys)fingerprint.append(scope.value(name));fingerprint.append(observation);fingerprint.append(provenance.value("semantic_digest"));
    const auto actual=QCryptographicHash::hash(QJsonDocument(fingerprint).toJson(QJsonDocument::Compact)+'\n',QCryptographicHash::Sha256).toHex();hash(field(receipt,"input_fingerprint"));require(actual==field(receipt,"input_fingerprint").toLatin1(),"Initial native fingerprint differs");
    QJsonObject observedBase{{"base_run",QJsonValue::Null},{"base_birth",QJsonValue::Null},{"base_start",QJsonValue::Null},{"base_end",QJsonValue::Null}};if(base)observedBase=baseObject(*base);
    const auto admission=QJsonDocument(QJsonObject{{"schema","pgn-initial-start-admission-1"},{"receipt",receipt},{"base",observedBase}}).toJson(QJsonDocument::Compact);require(admission.size()<=12288,"Initial wire admission exceeds12KiB");
    NativeControlCommand result(NativeControlAction::Initial);result.device_=d.device;result.request_=request;result.booking_=d.bookingId;result.start_=d.start;result.end_=d.end;
    result.finish(endpoint,QString("& 'C:/PGN/runtime/native-control.ps1' -Action initial -Offset 0 -ExpectedRun '%1' -InitialAdmission '%2'").arg(base?base->run:QString{},QString::fromLatin1(admission.toBase64())));return result;
}
NativeControlCommand NativeControlCommand::resume(const SshObservationEndpoint& endpoint,const CheckpointResumeAdmission& prepared,const CheckpointResumeEpochRead& saved,const NativeObservedBase& base){
    require(prepared.status==CheckpointResumeAdmissionStatus::Prepared&&!prepared.replayed&&prepared.error.isEmpty()&&prepared.epoch&&saved.status==CheckpointResumeReadStatus::Present&&saved.error.isEmpty()&&saved.epoch&&prepared.receiptUtf8==saved.receiptUtf8,"Resume requires exact native PREPARED + immutable readback, never reconcile");
    const auto& e=*saved.epoch;target(e.device);uuid(e.requestUUID);uuid(e.observationUUID);uuid(e.bookingId);run(e.previousRun);run(e.run);require(e.previousRun!=e.run,"Resume must reserve a NEW run");
    const auto receipt=canonicalObject(saved.receiptUtf8,24576);keys(receipt,{"schema","origin","request_id","device","booking_id","previous_run","run","observation_id","predecessor","counter_basis","raw_conservation_requirement","checkpoint"});
    const auto& p=*prepared.epoch;require(p.requestUUID==e.requestUUID&&p.observationUUID==e.observationUUID&&p.device==e.device&&p.bookingId==e.bookingId&&p.previousRun==e.previousRun&&p.run==e.run&&p.predecessor==e.predecessor&&p.checkpoint.checkpointBytes==e.checkpoint.checkpointBytes,"Resume admission/readback identity differs");
    require(receipt.value("schema")=="pgn-checkpoint-resume-epoch-1"&&receipt.value("origin")=="RESUME_CHECKPOINT"&&receipt.value("counter_basis")== (e.checkpoint.resume.dialect==SourceCheckpointDialect::ScoutGapBound?"PGNS_GAP32_64B_CUMULATIVE_BASELINE":"PGNCG001_CUMULATIVE_BASELINE")&&receipt.value("raw_conservation_requirement")=="LINEAGE_RAW_CONSERVATION_REQUIRED"&&e.rawConservationRequirement=="LINEAGE_RAW_CONSERVATION_REQUIRED","Resume basis/schema differs");
    require(receipt.value("request_id")==e.requestUUID&&receipt.value("observation_id")==e.observationUUID&&receipt.value("device")==e.device&&receipt.value("booking_id")==e.bookingId&&receipt.value("previous_run")==e.previousRun&&receipt.value("run")==e.run,"Resume receipt identity differs");
    if(e.predecessor){run(*e.predecessor);require(receipt.value("predecessor")==*e.predecessor,"Resume completed predecessor rebound");}else require(receipt.value("predecessor").isNull(),"Resume predecessor is not exact null");
    checkpoint(objectField(receipt,"checkpoint"),e);checkpoint(objectField(receipt,"checkpoint"),p);const auto observed=baseObject(base);require(base.run==e.previousRun&&base.start==e.checkpoint.resume.start&&base.end==e.checkpoint.resume.end,"Resume observed base differs");
    const auto admission=QJsonDocument(QJsonObject{{"schema","pgn-checkpoint-resume-admission-1"},{"receipt",receipt},{"base",observed}}).toJson(QJsonDocument::Compact);require(admission.size()<=8192,"Resume wire admission exceeds8KiB");
    NativeControlCommand result(NativeControlAction::Resume);result.device_=e.device;result.request_=e.requestUUID;result.booking_=e.bookingId;result.run_=e.run;result.start_=base.start;result.end_=base.end;
    result.finish(endpoint,QString("& 'C:/PGN/runtime/native-control.ps1' -Action resume -Offset 0 -ExpectedRun '%1' -ResumeAdmission '%2'").arg(base.run,QString::fromLatin1(admission.toBase64())));return result;
}
NativeControlCommand NativeControlCommand::advanceQueued(const SshObservationEndpoint& endpoint,const NativeQueuedAdvance& q){
    target(q.device);uuid(q.bookingId);run(q.previousRun);span(q.start,q.end);span(q.previousStart,q.previousEnd);if(q.previousBookingId)uuid(*q.previousBookingId);
    NativeControlCommand result(NativeControlAction::AdvanceQueued);result.device_=q.device;result.booking_=q.bookingId;result.run_=q.previousRun;result.start_=q.start;result.end_=q.end;result.previousStart_=q.previousStart;result.previousEnd_=q.previousEnd;result.previousBooking_=q.previousBookingId;
    result.finish(endpoint,QString("& 'C:/PGN/runtime/native-control.ps1' -Action advance -Offset 0 -ExpectedRun '%1' -QueueId '%2' -QueueStart '%3' -QueueEnd '%4'").arg(q.previousRun,q.bookingId,q.start,q.end));return result;
}
NativeControlCommand NativeControlCommand::stopObservedIdentity(const SshObservationEndpoint& endpoint,const NativeStopIdentity& identity,const QString& protocol){
    if(protocol!="pgn-stop-observed-1")throw NativeControlError(NativeControlErrorCode::UnsupportedAbi,"Observed stop identity capability absent/unknown; legacy stop forbidden");
    target(identity.device);run(identity.run);utc(identity.birth);
    require(QRegularExpression("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\\.[0-9]{7}Z$").match(identity.birth).capturedLength()==identity.birth.size(),"Observed stop requires exact o7 birth");
    decimal(identity.pid);bool ok=false;const auto pid=identity.pid.toULongLong(&ok);require(ok&&pid>0&&pid<=4294967295ULL,"Observed stop PID outside positive uint32");
    require(identity.imageSha256.size()==64&&QRegularExpression("^[0-9A-Fa-f]{64}$").match(identity.imageSha256).hasMatch(),"Observed stop executable SHA invalid");
    NativeControlCommand result(NativeControlAction::StopObservedIdentity);result.device_=identity.device;result.run_=identity.run;result.stop_=identity;result.stop_.imageSha256=identity.imageSha256.toUpper();
    result.finish(endpoint,QString("& 'C:/PGN/runtime/native-control.ps1' -Action stop-observed -ExpectedRun '%1' -ExpectedPid '%2' -ExpectedBirth '%3' -ExpectedImageSha256 '%4'").arg(identity.run,identity.pid,identity.birth,result.stop_.imageSha256));return result;
}
NativeControlCommand NativeControlCommand::stopObservedSnapshot(const SshObservationEndpoint& endpoint,const SshObservationFrame& frame){
    const auto& p=frame.packet;
    require(p.value("schema")=="pgn-runtime-snapshot-1"&&p.value("owned").isBool()&&p.value("driver_valid").isBool(),"Known typed process observation required");
    // Driver metrics do not establish process ownership. A failed/skipped
    // metrics query cannot defeat an exact PID/birth/image guarded STOP.
    const auto state=field(p,"state");const bool owned=p.value("owned").toBool();
    require(owned?(state=="RUNNING"||state=="STOPPING"):state=="OFF","Observed ownership/state contradict");
    utc(field(p,"observed_utc"));const auto m=objectField(p,"manifest"),b=objectField(p,"binding");
    const auto device=field(m,"uuid");target(device);
    require(m.value("schema")=="pgn-native-process-1"&&b.value("uuid")==device,"Observed stop profile/device mismatch");
    const auto pid=m.value("pid");require(pid.toVariant().metaType().id()==QMetaType::LongLong&&pid.toInteger()>0&&pid.toInteger()<=4294967295LL,"Observed PID must be exact positive uint32 integer");
    const auto path=[](QString value){
        text(value);require(QRegularExpression("^[A-Za-z]:[\\\\/]").match(value).hasMatch(),"Observed image pin must use absolute Windows drive path");
        require(!value.contains('\r')&&!value.contains('\n'),"Observed image path has control characters");
        value.replace('\\','/');return QDir::cleanPath(value).toCaseFolded();
    };
    const auto executable=path(field(b,"executable",4096));require(b.value("pins").isArray(),"Observed executable pins missing");
    QString image;int matches=0;
    for(const auto& v:b.value("pins").toArray()){
        require(v.isObject(),"Observed executable pin is not object");const auto pin=v.toObject();const auto pinPath=path(field(pin,"path",4096)),sha=field(pin,"sha256");hash(sha.toLower());
        if(pinPath==executable){++matches;image=sha;}
    }
    require(matches==1,"Observed executable pin must be unique");
    return stopObservedIdentity(endpoint,{device,field(m,"run"),QString::number(pid.toInteger()),field(m,"birth"),image},field(p,"observed_stop_protocol"));
}
NativeControlCommand NativeControlCommand::finite3060Snapshot(const SshObservationEndpoint& endpoint){
    NativeControlCommand result(NativeControlAction::Finite3060Snapshot);result.device_=Finite3060Device;
    result.finish(endpoint,QString("& '%1' -Action snapshot").arg(Finite3060Controller));return result;
}
NativeControlCommand NativeControlCommand::finite3060Start(const SshObservationEndpoint& endpoint,const QString& request){
    uuid(request);NativeControlCommand result(NativeControlAction::Finite3060Start);result.device_=Finite3060Device;result.request_=request;
    // Remote active3090 owner, task, slot and image guards remain authoritative;
    // no native fallback or claim that a queued task has started/completed.
    result.finish(endpoint,QString("& '%1' -Action start -RequestId '%2'").arg(Finite3060Controller,request));return result;
}
NativeControlCommand NativeControlCommand::finiteObservedRunStop(const SshObservationEndpoint& endpoint,const QString& request,const QString& expected){
    uuid(request);run(expected);NativeControlCommand result(NativeControlAction::FiniteObservedRunStop);result.device_=Finite3060Device;result.request_=request;result.run_=expected;
    result.finish(endpoint,QString("& '%1' -Action stop -RequestId '%2' -ExpectedRun '%3'").arg(Finite3060Controller,request,expected));return result;
}
NativeControlCommand NativeControlCommand::configured3060StopLatch(const SshObservationEndpoint& endpoint,const QString& request,Configured3060OffReason reason){
    uuid(request);require(reason==Configured3060OffReason::ApplicationClose||reason==Configured3060OffReason::ExplicitCardOff,"Explicit configured3060 OFF caller authority required, never fallback");
    NativeControlCommand result(NativeControlAction::Configured3060StopLatch);result.device_=Finite3060Device;result.request_=request;
    result.finish(endpoint,QString("& '%1' -Action stop -RequestId '%2'").arg(Finite3060Controller,request));return result;
}
NativeControlCommand NativeControlCommand::configured3090Profile(const SshObservationEndpoint& endpoint){
    NativeControlCommand result(NativeControlAction::Configured3090Profile);result.finish(endpoint,"& 'C:/PGN/runtime/native-control.ps1' -Action configured-profile");return result;
}
NativeControlCommand NativeControlCommand::configured3090StopLatch(const SshObservationEndpoint& endpoint,const QString& request,Configured3090OffReason reason,const NativeConfiguredProfileObservation& profile){
    if(profile.protocol!="pgn-configured-stop-1")throw NativeControlError(NativeControlErrorCode::UnsupportedAbi,"Unsupported configured3090 OFF ABI; no legacy fallback");
    uuid(request);require(reason==Configured3090OffReason::ApplicationClose||reason==Configured3090OffReason::ExplicitCardOff,"Explicit configured3090 OFF reason required");
    configuredTuple(profile);configuredBinding(profile);
    NativeControlCommand result(NativeControlAction::Configured3090StopLatch);result.request_=request;result.configured_=profile;
    result.configuredReason_=reason==Configured3090OffReason::ApplicationClose?"AppClose":"CardOff";
    result.finish(endpoint,QString("& 'C:/PGN/runtime/native-control.ps1' -Action stop-configured -RequestId '%1' -ConfiguredReason '%2' -ConfiguredProtocol 'pgn-configured-stop-1' -ExpectedDevice '%3' -ExpectedExecutable %4 -ExpectedImageSha256 '%5' -ExpectedBindingSha256 '%6'")
        .arg(request,result.configuredReason_,profile.device,psQuote(profile.executable),profile.imageSha256,profile.bindingSha256));return result;
}
NativeControlResult NativeControlCommand::decode(const QByteArray& bytes,bool normalExit,int exitCode,const QByteArray& stderrBytes) const noexcept{
    NativeControlResult result;
    try{
        require(normalExit&&exitCode==0,"Control process failed/abnormal: outcome remains unconfirmed");require(stderrBytes.size()<=StderrLimit,"Control stderr exceeds bound");QHash<QString,QByteArray> scalars;const auto packet=response(bytes,scalars);
        if(action_==NativeControlAction::Configured3090Profile){result.configuredProfile=configuredObservation(packet);result.outcome=NativeControlOutcome::ConfiguredProfileReported;result.status="CONFIGURED_PROFILE_OBSERVED";result.packet=packet;return result;}
        if(action_==NativeControlAction::Configured3090StopLatch){
            keys(packet,{"protocol","status","request_id","reason","profile","physical_applied"});
            require(configured_&&packet.value("protocol")=="pgn-configured-stop-1"&&packet.value("status")=="CONFIGURED_OFF_LATCHED"&&packet.value("request_id")==request_&&packet.value("reason")==configuredReason_&&packet.value("profile")==configuredTuple(*configured_)&&packet.value("physical_applied").isBool()&&!packet.value("physical_applied").toBool(),"Configured OFF result tuple/level differs");
            result.status="CONFIGURED_OFF_LATCHED";result.requestId=request_;result.outcome=NativeControlOutcome::ConfiguredOffLatchReported;result.packet=packet;return result;
        }
        if(action_==NativeControlAction::Finite3060Snapshot||action_==NativeControlAction::Finite3060Start||action_==NativeControlAction::FiniteObservedRunStop||action_==NativeControlAction::Configured3060StopLatch){
            const auto observation=projectNativeFiniteObservation(packet,scalars);
            // Not the historical worker request_id: only LOCAL client correlation.
            result.requestId=request_;result.status="FINITE_OBSERVATION_REPORTED";
            if(action_==NativeControlAction::FiniteObservedRunStop)require(observation.manifest&&observation.manifest->run==run_,"Finite expected run changed in response; no retarget/fallback");
            if(observation.manifest)result.run=observation.manifest->run;
            result.outcome=NativeControlOutcome::ObservationReported;result.packet=packet;result.finiteObservation=observation;return result;
        }
        const auto status=field(packet,"status");result.status=status;
        if(action_==NativeControlAction::Initial||action_==NativeControlAction::Resume){
            require(field(packet,"request_id")==request_,"Control request UUID mismatch");result.requestId=request_;
            if(status=="ALREADY_RUNNING"){keys(packet,{"status","request_id","run"});const auto reported=field(packet,"run");run(reported);require(action_!=NativeControlAction::Resume||reported==run_,"Resume reported a DIFFERENT reserved run");result.run=reported;result.outcome=NativeControlOutcome::AlreadyRunningReported;}
            else if(status=="START_REQUESTED"){keys(packet,{"status","request_id","launch"});const auto launch=objectField(packet,"launch");keys(launch,{"status"});require(field(launch,"status")=="START_REQUESTED","Nested launch contradicts outer request");result.outcome=NativeControlOutcome::RequestReported;}
            else {require(status==(action_==NativeControlAction::Initial?"INITIAL_START_HELD":"CHECKPOINT_RESUME_HELD"),"Unknown initial/resume result");keys(packet,{"status","request_id","error"});result.error=field(packet,"error");require(!result.error.isEmpty()&&result.error.size()<=400,"Hold reason absent/oversize");result.outcome=NativeControlOutcome::Held;}
        }else if(action_==NativeControlAction::AdvanceQueued){
            keys(packet,{"status","launch","completion","predecessor_binding","binding","community_csv"});require(status=="SUCCESSOR_START_REQUESTED"||status=="SUCCESSOR_HELD","Unknown queued successor result");
            const auto launch=objectField(packet,"launch"),completion=objectField(packet,"completion"),before=objectField(packet,"predecessor_binding"),after=objectField(packet,"binding");
            require(completion.value("schema")=="pgn-source-range-completion-1"&&completion.value("run")==run_&&completion.value("start")==previousStart_&&completion.value("end")==previousEnd_&&
                completion.value("source_range_complete").isBool()&&completion.value("source_range_complete").toBool()&&completion.value("independent_coverage").isBool()&&!completion.value("independent_coverage").toBool()&&
                completion.value("proof_status")=="SOURCE_PRP_INDEPENDENT_PROOF_PENDING"&&field(completion,"source_positions")==subtractUnsigned(previousEnd_,previousStart_),"Queued SOURCE response tuple/flags differ (not independently verified)");
            require((previousBooking_&&completion.value("queued_intent")==*previousBooking_)||(!previousBooking_&&completion.value("queued_intent").isNull()),"Queued predecessor booking differs");
            require(scalars.value("/completion/source_exit_code")=="0","Queued successful source exit is absent/noncanonical/nonzero");
            for(const auto* name:{"source_tests","source_session_gaps"})decimal(field(completion,name));utc(field(completion,"source_exit_utc"));require(!field(completion,"source").isEmpty(),"Queued source identity missing");
            require(before.value("uuid")==device_&&before.value("start")==previousStart_&&before.value("end")==previousEnd_&&after.value("uuid")==device_&&after.value("start")==start_&&after.value("end")==end_&&after.value("queued_intent")==booking_,"Queued returned binding differs");
            const auto csv=field(packet,"community_csv",1398104).toLatin1();const auto raw=QByteArray::fromBase64(csv);require(raw.size()<=1048576&&raw.toBase64()==csv&&QString::fromUtf8(raw).toUtf8()==raw,"Community bytes/base64 invalid/oversize");
            const auto launched=field(launch,"status");const bool requested=launched=="START_REQUESTED"||launched=="ALREADY_RUNNING";require(requested||launched=="OPERATOR_STOP_HELD"||launched=="SOURCE_FINISHED_ADVANCE_REQUIRED","Unknown nested successor status");
            if(launched=="SOURCE_FINISHED_ADVANCE_REQUIRED"){keys(launch,{"status","run"});require(field(launch,"run")==run_,"Held successor epoch changed");}else keys(launch,{"status"});
            require((status=="SUCCESSOR_START_REQUESTED")==requested,"Successor launch/status contradict");result.outcome=requested?NativeControlOutcome::RequestReported:NativeControlOutcome::Held;result.run=run_;if(!requested)result.error="Queued successor held: "+launched;
        }else if(action_==NativeControlAction::StopObservedIdentity){
            keys(packet,{"protocol","status","observed","physical_applied"});require(packet.value("protocol")=="pgn-stop-observed-1"&&packet.value("physical_applied").isBool()&&!packet.value("physical_applied").toBool(),"Stop ABI/proof level invalid");
            const auto observed=objectField(packet,"observed");keys(observed,{"run","pid","birth","image_sha256"});
            require(field(observed,"run")==stop_.run&&field(observed,"pid")==stop_.pid&&field(observed,"birth")==stop_.birth&&field(observed,"image_sha256")==stop_.imageSha256,"Stop observed epoch tuple changed");
            require(status=="STOP_REQUESTED"||status=="ALREADY_EXITED_STOP_HELD","Unknown identity stop result");result.outcome=NativeControlOutcome::RequestReported;result.run=run_;
        }else fail("Unsupported control result ABI");
        result.packet=packet;return result;
    }catch(const std::exception& error){result=NativeControlResult{};result.error=QString::fromUtf8(error.what()).left(400);return result;}
}
}

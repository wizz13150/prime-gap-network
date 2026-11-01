#include "mining/prime_gap_engine.h"
#include "booking/decimal_range.h"
#include "mining/source_decimal.h"
#include "mining/retained_merit_policy_data.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace pgn::mining {
namespace {
void require(bool condition,const char* error){if(!condition)throw std::invalid_argument(error);}
const retention_policy::Step& retentionStep(const QString& position){
    require(source_decimal::canonical(position),"Canonical retention position required");
    for(const auto& step:retention_policy::Steps){
        if(!source_decimal::less(position,QString::fromLatin1(step.begin))
            &&source_decimal::less(position,QString::fromLatin1(step.end)))return step;
    }
    throw std::invalid_argument("Retention position leaves 67..95 bits");
}
quint64 integer64(const QString& value){bool ok=false;const auto n=value.toULongLong(&ok);require(ok,"Engine length/word exceeds UInt64");return n;}
quint64 highWord(QString value){for(int i=0;i<4;++i)value=booking::divideUnsignedSmall(value,65536);return integer64(value);}
QString baseOf(quint64 high){auto value=QString::number(high);for(int i=0;i<64;++i)value=booking::addUnsigned(value,value);return value;}
unsigned residue30(const QString& value){unsigned r=0;for(auto c:value)r=(10*r+c.unicode()-'0')%30;return r;}
bool exactNumber(const QJsonValue& v,quint64 n){return v.isDouble()&&std::isfinite(v.toDouble())&&v.toDouble()>=0&&v.toDouble()<=9007199254740991.0&&v.toDouble()==double(n);}
QString path(const QString& value){
    require(!value.isEmpty()&&value.size()<=4096&&!value.contains(QChar(0))&&!value.contains('\r')&&!value.contains('\n'),"Engine output path is invalid");
    require(QFileInfo(value).isAbsolute(),"Engine output paths must be explicit absolute paths");return value;
}
QStringList common(const PrimeGapEngineJob& job,int device){
    require(device>=0&&device<=255,"Invalid visible GPU index");
    const auto checked=primeGapEngineJob(job.start,job.end,job.descriptorK,job.minimumGap);
    require(checked.length==job.length&&checked.highWord==job.highWord&&checked.lowWord==job.lowWord&&checked.bits==job.bits&&checked.positions==job.positions,"Engine job tuple was modified");
    return {"--start",job.start,"--length",job.length,"--gap-min",QString::number(job.minimumGap),
        "--emit-min-gap",QString::number(job.minimumGap),"--device",QString::number(device)};
}
}
quint32 primeGapMinimumAt(const QString& position){return retentionStep(position).gap;}
QString primeGapNextThreshold(const QString& position){return QString::fromLatin1(retentionStep(position).end);}
QString primeGapSourceManifest(){return "96C600D3486DED57B400FCD4428E904F6A3743E49F587DC85E93847D9FFDDE13";}
std::optional<int> primeGapReleaseArchitecture(const QString& image){
    const auto hash=image.toUpper();
    if(hash=="F0B9F81B3F8B8316BEADAB8F04E90A85C412D6DABE2793548F05D66D1AF80DD5")return 86;
    if(hash=="23D8396FCBABE2955CF947BE399FFD8B707D8734BCF3BB11F97F1FC356339CED")return 120;
    return {};
}
QHash<QString,QString> primeGapRuntimePins(const QString& bundle,const QString& executable,const QString& image){
    require(primeGapReleaseArchitecture(image).has_value()&&QFileInfo(executable).absolutePath()==bundle+"/bin","Engine bundle/image identity differs");
    const auto proof=bundle+"/lib/proof/windows/";
    return {{executable,image.toUpper()},
        {bundle+"/bin/cudart64_12.dll","9D9B868955149875AC4EF43442AAAA8913D4A7B3E4D6DD60EE871626AA045768"},
        {proof+"manifest.json","644757BFD0525FA65E5F2D479E4D0F734A5A714CAFB3097C9EBAC47467832F19"},
        {proof+"libflint.20.0.0-325ff13fcb92fb5a438579ecfff5f1da.dll","76C9B206B94A409DC6C4F683B38EFE952FEE6BB354A5C669B85684550A59395A"},
        {proof+"libgcc_s_seh-1-bd1ea0fd3c3e2849e3e33dd77813f715.dll","B443E0AD17713750105675F317F5F5C7790282F551DD76279AAAABB71543AC3A"},
        {proof+"libgmp-10-3f2a57fb670147a982ef47f4f710bb31.dll","0032DAAFB74E92C1A423FE1169FA4A3CB2604634D977EE0D2E85D5F48FBD32FC"},
        {proof+"libmpfr-6-a244575d2981d61d439ca14ed667bc72.dll","79E4B2D050F6039EB055B341E9EE6FDF736C0FD68F06DB318B8CD8E478BF30D7"},
        {proof+"libwinpthread-1-cde765bd2b668ec30cb82d72611f9720.dll","5BBEF249A0D00E2D32C699D0BBE89F714EBEB872B3990A5CBECCB1D89F63E5E8"}};
}
PrimeGapEngineJob primeGapEngineJob(const QString& first,const QString& last,int k,std::optional<quint32> minimum){
    require(first.size()<=29&&last.size()<=29&&source_decimal::canonical(first)&&source_decimal::canonical(last)
        &&source_decimal::less(first,last),"Exact canonical increasing engine bounds required");
    const int bits=booking::bitLength(first),lastBits=booking::bitLength(booking::subtractUnsigned(last,"1"));
    require(bits>=67&&lastBits<=95,"Engine range leaves 67..95 bits");
    require(bits==lastBits,"Split at a bit boundary before selecting an engine policy");
    require(k==8||k==16||k==32||k==64,"Engine descriptor K must be 8,16,32 or64");
    const auto high=highWord(first);
    require(high==highWord(booking::subtractUnsigned(last,"1")),"Native low64 boundary requires an explicit bridge");
    const auto low=integer64(booking::subtractUnsigned(first,baseOf(high)));
    const auto length=booking::subtractUnsigned(last,first);const auto count=integer64(length);
    require(count<=std::numeric_limits<quint64>::max()-low
        ||(low>0&&count==std::numeric_limits<quint64>::max()-low+1),
        "Engine exclusive endpoint overflows the native low64 word");
    require(low>=residue30(first),"Native class30 origin crosses its low64 window");
    const auto gap=minimum.value_or(primeGapMinimumAt(first));
    require(gap>=primeGapMinimumFloor()&&gap<=2147483647U,"Engine minimum gap outside supported integer domain");
    require(minimum.has_value()||primeGapMinimumAt(booking::subtractUnsigned(last,"1"))==gap,
        "Split at a pointwise retention transition before selecting a physical job");
    return {first,last,length,high,low,count,bits,k,gap};
}
bool primeGapImageProfile(const QString& executable,int k){
    static const QRegularExpression suffix("-k(8|16|32|64)(?:\\.exe)?$");
    const auto match=suffix.match(QFileInfo(executable).fileName());return match.hasMatch()&&match.captured(1).toInt()==k;
}
QStringList primeGapPlanArguments(const PrimeGapEngineJob& job,int device){auto args=common(job,device);args.prepend("--plan");return args;}
QStringList primeGapRunArguments(const PrimeGapEngineJob& job,int device,const QString& checkpoint,const QString& log,const QString& stop,quint32 owner){
    require(owner>0&&owner<=2147483647U,"An actual owned controller PID is required");
    auto args=common(job,device);args.prepend("walk");args.prepend("--engine");
    // Windows controller ABI: retained child handles and the stop-file belong
    // to the supervisor. --owner-pid is implemented only by the Linux image.
    args.append({"--state",path(checkpoint),"--log",path(log),"--stop-file",path(stop),"--no-records"});return args;
}
QString validatePrimeGapPlan(const PrimeGapEngineJob& job,const QJsonObject& p){
    try{
        common(job,0);
        require(p.value("schema")=="pgn-memory-plan-1"&&p.value("gap_runtime_schema")=="PGN_NATIVE_GAP_RUNTIME_V1","Unsupported native plan ABI");
        require(p.value("optimized_domain_schema")=="SCOUT_WORD_DOMAINS_V1"
            &&exactNumber(p.value("optimized_high_begin"),4)
            &&exactNumber(p.value("optimized_high_end"),2147483648ULL)
            &&p.value("word_tail_clipping")==true&&p.value("bounded_scan")==true,
            "Qualified optimized full-domain engine plan required");
        require(p.value("range_valid")==true&&p.value("fits")==true,"Actual native range/memory plan refused this job");
        require(p.value("start")==job.start&&p.value("length")==job.length&&exactNumber(p.value("high_word"),job.highWord)
            &&exactNumber(p.value("gap_min"),job.minimumGap)&&exactNumber(p.value("profile_k"),quint64(job.descriptorK)),"Native plan scope, K or threshold echo differs");
        const auto required=p.value("required_device_bytes"),free=p.value("free_device_bytes");
        require(required.isDouble()&&free.isDouble()&&required.toDouble()>0&&required.toDouble()<=free.toDouble()
            &&free.toDouble()<=9007199254740991.0&&std::isfinite(free.toDouble())
            &&std::floor(required.toDouble())==required.toDouble()&&std::floor(free.toDouble())==free.toDouble(),"Native memory accounting is invalid");
        require(p.value("arithmetic_backend").isString()&&!p.value("arithmetic_backend").toString().isEmpty(),"Native arithmetic specialization was not declared");
        require(!p.value("arithmetic_backend").toString().contains("generic",Qt::CaseInsensitive),
            "Only qualified optimized arithmetic is admissible");
        return {};
    }catch(const std::exception& error){return QString::fromUtf8(error.what());}
}
}

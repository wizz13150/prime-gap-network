#include "mining/engine_module_registry.h"
#include "mining/source_decimal.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QThread>
#include <sqlite3.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace pgn::mining {
namespace {
void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
// Node UTF8 conversion replaces unpaired UTF16 surrogates by U+FFFD. SQL text
// uses this conversion; JSON.stringify instead preserves them as ASCII escapes.
QByteArray jsUtf8(const QString& text){
    QString well;well.reserve(text.size());
    for(qsizetype i=0;i<text.size();++i){const auto c=text[i];
        if(c.isHighSurrogate()){if(i+1<text.size()&&text[i+1].isLowSurrogate()){well+=c;well+=text[++i];}else well+=QChar(0xfffd);}
        else well+=c.isLowSurrogate()?QChar(0xfffd):c;
    }return well.toUtf8();
}
QByteArray quoted(const QString& text){
    QByteArray out="\"";static const char hex[]="0123456789abcdef";
    for(qsizetype i=0;i<text.size();++i){const auto c=text[i];const auto n=c.unicode();
        if(n=='"'||n=='\\'){out+='\\';out+=char(n);}
        else if(n=='\b')out+="\\b";else if(n=='\t')out+="\\t";else if(n=='\n')out+="\\n";else if(n=='\f')out+="\\f";else if(n=='\r')out+="\\r";
        else if(n<32||(c.isHighSurrogate()&&(i+1==text.size()||!text[i+1].isLowSurrogate()))||c.isLowSurrogate()){
            out+="\\u";for(int shift=12;shift>=0;shift-=4)out+=hex[(n>>shift)&15];
        }else if(c.isHighSurrogate()){QString pair;pair+=c;pair+=text[++i];out+=pair.toUtf8();}
        else out+=QString(c).toUtf8();
    }return out+'"';
}
QByteArray jsNumber(double value){
    need(std::isfinite(value),"Finite JSON number required");if(value==0)return "0";
    const bool negative=value<0;if(negative)value=-value;
    // Shortest round-trip, nearest/ties-even binary64 significand. Formatting
    // thresholds differ from C++ general/Qt JSON and are applied below per JS.
    char chars[64];const auto converted=std::to_chars(chars,chars+sizeof(chars),value,std::chars_format::scientific);
    need(converted.ec==std::errc{},"Native shortest number conversion failed");
    const QByteArray raw(chars,converted.ptr-chars);const auto e=raw.indexOf('e');need(e>0,"Scientific shortest representation missing exponent");
    bool ok=false;const int exponent=raw.mid(e+1).toInt(&ok);need(ok,"Shortest exponent invalid");
    auto digits=raw.left(e);digits.replace(".","");const int n=exponent+1;QByteArray out;
    if(n>=-5&&n<=21){
        if(n>=digits.size())out=digits+QByteArray(n-digits.size(),'0');
        else if(n>0)out=digits.left(n)+'.'+digits.mid(n);
        else out="0."+QByteArray(-n,'0')+digits;
    }else{out=digits.left(1);if(digits.size()>1)out+='.'+digits.mid(1);out+='e';if(exponent>=0)out+='+';out+=QByteArray::number(exponent);}
    return negative?'-'+out:out;
}
bool utf16Less(const QString& a,const QString& b){return std::lexicographical_compare(a.begin(),a.end(),b.begin(),b.end(),[](QChar x,QChar y){return x.unicode()<y.unicode();});}
QByteArray canonical(const QJsonValue& value,int depth){
    need(depth<=1024,"JSON exceeds native Qt parser depth");
    if(value.isNull())return "null";if(value.isBool())return value.toBool()?"true":"false";
    if(value.isDouble())return jsNumber(value.toDouble());if(value.isString())return quoted(value.toString());
    QByteArray out;
    if(value.isArray()){out="[";bool first=true;for(const auto& v:value.toArray()){if(!first)out+=',';first=false;out+=canonical(v,depth+1);}return out+']';}
    need(value.isObject(),"JSON data required, no undefined extension");out="{";const auto object=value.toObject();auto names=object.keys();std::sort(names.begin(),names.end(),utf16Less);bool first=true;
    for(const auto& key:names){if(!first)out+=',';first=false;out+=quoted(key)+':'+canonical(object.value(key),depth+1);}return out+'}';
}
QJsonValue clone(const QJsonValue& v,int depth=0){
    need(depth<=1024&&!v.isUndefined(),"JSON data required");
    if(v.isDouble()){const double d=v.toDouble();need(std::isfinite(d),"Finite JSON number required");return d==0?QJsonValue(0.0):QJsonValue(d);}
    if(v.isObject()){QJsonObject out;const auto object=v.toObject();for(auto it=object.begin();it!=object.end();++it)out.insert(it.key(),clone(it.value(),depth+1));return out;}
    if(v.isArray()){QJsonArray out;for(const auto& item:v.toArray())out.append(clone(item,depth+1));return out;}return v;
}
bool jsSpace(QChar c){const auto n=c.unicode();return (n>=9&&n<=13)||n==0x20||n==0xa0||n==0x1680||(n>=0x2000&&n<=0x200a)||n==0x2028||n==0x2029||n==0x202f||n==0x205f||n==0x3000||n==0xfeff;}
bool nonempty(const QJsonValue& v){if(!v.isString())return false;for(const auto c:v.toString())if(!jsSpace(c))return true;return false;}
QString field(const QJsonObject& o,const char* name){const auto v=o.value(QLatin1String(name));need(v.isString(),"Explicit string required");return v.toString();}
const QString U64="18446744073709551615",U128="340282366920938463463374607431768211455";
QString decimal(const QJsonValue& value,const QString& max){need(value.isString(),"Canonical decimal string required");const auto s=value.toString();need(source_decimal::canonical(s)&&!source_decimal::less(max,s),"Canonical decimal out of range");return s;}
QString add(QString a,QString b){QString out;int carry=0;qsizetype i=a.size(),j=b.size();while(i||j||carry){int d=carry;if(i)d+=a[--i].unicode()-'0';if(j)d+=b[--j].unicode()-'0';out.prepend(QChar('0'+d%10));carry=d/10;}return out;}
QString half(const QString& a){QString out;int rem=0;for(auto c:a){const int d=rem*10+c.unicode()-'0';if(!out.isEmpty()||d/2)out+=QChar('0'+d/2);rem=d%2;}return out.isEmpty()?QString("0"):out;}
QString highWord(const QJsonValue& value){if(value.isString())return decimal(value,U64);need(value.isDouble(),"High word must be unsigned safe number/string");const double n=value.toDouble();need(std::isfinite(n)&&n>=0&&n<=9007199254740991.0&&std::floor(n)==n,"High word number exceeds JS safe integer");return QString::number(quint64(n));}
bool truthy(const QJsonValue& v){if(v.isNull()||v.isUndefined())return false;if(v.isBool())return v.toBool();if(v.isString())return !v.toString().isEmpty();if(v.isDouble())return v.toDouble()!=0;return true;}
void requestId(const QString& id){need(id.size()>=1&&id.size()<=96&&QRegularExpression("^[a-zA-Z0-9._:-]+$").match(id).capturedLength()==id.size(),"Bounded module request identity required");}
void moduleKey(const QString& key){need(key.size()==64&&QRegularExpression("^[a-f0-9]{64}$").match(key).hasMatch(),"Canonical module key required");}
QString sha(const QJsonValue& value){return QString::fromLatin1(QCryptographicHash::hash(canonicalEngineModuleJson(value),QCryptographicHash::Sha256).toHex());}
std::optional<QJsonObject> parsedObject(const QByteArray& value){QJsonParseError error;const auto doc=QJsonDocument::fromJson(value,&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())return std::nullopt;return doc.object();}
QJsonObject parseObject(const QByteArray& value){const auto parsed=parsedObject(value);need(parsed.has_value(),"Stored JSON unavailable to native Qt parser");return *parsed;}
QString timestamp(){return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);}
struct Statement {
    sqlite3_stmt* value{};
    Statement(sqlite3* db,const char* sql){if(sqlite3_prepare_v2(db,sql,-1,&value,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    ~Statement(){if(value)sqlite3_finalize(value);}
    void bind(int index,const QString& text){const auto bytes=jsUtf8(text);need(sqlite3_bind_text(value,index,bytes.constData(),bytes.size(),SQLITE_TRANSIENT)==SQLITE_OK,"SQLite text binding failed");}
    void bind(int index,const QJsonValue& v){if(v.isNull())need(sqlite3_bind_null(value,index)==SQLITE_OK,"SQLite null binding failed");else bind(index,v.toString());}
    int step(){const int result=sqlite3_step(value);if(result!=SQLITE_ROW&&result!=SQLITE_DONE)throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(value)));return result;}
    QJsonValue text(int col){if(sqlite3_column_type(value,col)==SQLITE_NULL)return QJsonValue::Null;need(sqlite3_column_type(value,col)==SQLITE_TEXT,"Registry text column type invalid");return QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(value,col)),sqlite3_column_bytes(value,col));}
};
}
QByteArray canonicalEngineModuleJson(const QJsonValue& value){return canonical(value,0);}
QJsonObject validateEngineModuleCapabilities(const QJsonValue& input){
    need(input.isObject(),"Module descriptor object required");const auto o=input.toObject();need(o.value("schema")=="pgn-engine-module-1","Expected pgn-engine-module-1");
    for(const auto* key:{"id","protocol"})need(nonempty(o.value(key)),"Module identity/protocol nonempty string required");
    for(const auto* key:{"host_refill","host_verification","whole_job_gpu_owned"})need(o.value(key).isBool(),"Explicit capability boolean required");
    if(o.contains("single_low64_window"))need(o.value("single_low64_window").isBool(),"Low64 window boolean required");
    if(o.contains("supported_high_word"))highWord(o.value("supported_high_word"));
    if(o.contains("cuda_arch")){
        const auto value=o.value("cuda_arch");
        if(value.isArray()){const auto architectures=value.toArray();need(!architectures.isEmpty(),"CUDA architecture list must not be empty");for(const auto& arch:architectures)need(nonempty(arch),"CUDA architecture string required");}
        else need(nonempty(value),"CUDA architecture string or list required");
    }
    if(o.contains("supported_high_word_range")){
        need(o.value("supported_high_word_range").isArray(),"High word range array required");const auto bounds=o.value("supported_high_word_range").toArray();need(bounds.size()==2,"High word range needs two bounds");
        need(!source_decimal::less(highWord(bounds[1]),highWord(bounds[0])),"High word range must increase");
    }
    need(o.contains("bits_min")==o.contains("bits_max"),"Bit domain needs both limits");
    if(o.contains("bits_min")){
        const auto a=o.value("bits_min"),b=o.value("bits_max");
        need(a.isDouble()&&b.isDouble()&&std::floor(a.toDouble())==a.toDouble()&&std::floor(b.toDouble())==b.toDouble()&&a.toDouble()>=1&&b.toDouble()<=128&&a.toDouble()<=b.toDouble(),"Bit domain must be an increasing integer interval1..128");
    }
    for(const auto* key:{"prime_certificate","coverage"})if(o.contains(key))need(nonempty(o.value(key)),"Optional capability nonempty string required");
    return clone(input).toObject();
}
QJsonObject observeEngineModule(const QJsonValue& capabilities,const QJsonObject& provenance){
    for(const auto* key:{"observed_utc","artifact_sha256","source"})if(provenance.contains(key))need(provenance.value(key).isNull()||provenance.value(key).isString(),"Observation provenance must be string/null");
    return {{"schema","pgn-engine-module-observation-1"},{"capabilities",validateEngineModuleCapabilities(capabilities)},{"provenance",clone(provenance)},{"evidence_scope","PRODUCER_DECLARATION_ONLY"}};
}
QJsonObject createEngineModuleRequest(const QJsonObject& observation,const QJsonObject& intent){
    need(observation.value("schema")=="pgn-engine-module-observation-1","Module observation required");const auto c=validateEngineModuleCapabilities(observation.value("capabilities"));need(c.value("protocol")=="phase0-source-1","Unsupported job protocol; descriptor remains inspectable");
    for(const auto* key:{"request_id","device_id","owner"})need(nonempty(intent.value(key)),"Explicit request identity required");
    const auto start=decimal(intent.value("start"),U128),length=decimal(intent.value("length"),U64);need(length!="0"&&!source_decimal::less(U128,add(start,length)),"Interval must be nonempty/representable");
    auto high=start;for(int i=0;i<64;++i)high=half(high);
    if(c.contains("supported_high_word"))need(high==highWord(c.value("supported_high_word")),"Unsupported high word");
    const auto finalPosition=add(start,source_decimal::difference(length,"1"));
    if(c.contains("supported_high_word_range")){
        const auto range=c.value("supported_high_word_range").toArray();auto lastHigh=finalPosition;for(int i=0;i<64;++i)lastHigh=half(lastHigh);
        need(!source_decimal::less(high,highWord(range[0]))&&!source_decimal::less(highWord(range[1]),lastHigh),"Interval outside declared high word range");
    }
    if(c.contains("bits_min")){
        const auto bits=[](QString value){int count=0;while(value!="0"){value=half(value);++count;}return count;};
        need(bits(start)>=c.value("bits_min").toInt()&&bits(finalPosition)<=c.value("bits_max").toInt(),"Interval outside declared bit domain");
    }
    if(c.value("single_low64_window")==true){QString boundary=high;for(int i=0;i<64;++i)boundary=add(boundary,boundary);const auto low=source_decimal::difference(start,boundary);need(!source_decimal::less(source_decimal::difference(U64,low),length),"Interval crosses declared low64 window");}
    return {{"schema","pgn-engine-module-request-1"},{"request_id",intent.value("request_id")},{"module_id",c.value("id")},{"protocol",c.value("protocol")},{"owner",intent.value("owner")},{"device_id",intent.value("device_id")},
        {"job",QJsonObject{{"start",intent.value("start")},{"length",intent.value("length")}}},{"state","REQUESTED"},{"observation",clone(observation)},{"admission","CONTROLLER_VALIDATION_REQUIRED"}};
}
class EngineModuleRegistry::Impl {
public:
    QString file;sqlite3* db{};QThread* const thread=QThread::currentThread();
    explicit Impl(QString path):file(QFileInfo(path).absoluteFilePath()){need(!path.isEmpty()&&!path.contains(QChar(0)),"Explicit registry database path required");}
    ~Impl(){if(db)sqlite3_close(db);}
    void owner(){need(QThread::currentThread()==thread,"Registry called outside creating writer thread");}
    void exec(const char* sql){if(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    sqlite3* open(){owner();if(db)return db;need(QDir().mkpath(QFileInfo(file).absolutePath()),"Registry directory unavailable");sqlite3* opened{};
        const auto path=jsUtf8(file);if(sqlite3_open_v2(path.constData(),&opened,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)!=SQLITE_OK){const auto error=QString::fromUtf8(opened?sqlite3_errmsg(opened):"SQLite unavailable");if(opened)sqlite3_close(opened);throw std::runtime_error(error.toStdString());}db=opened;
        try{exec("PRAGMA busy_timeout=1000;PRAGMA journal_mode=WAL;PRAGMA synchronous=FULL;CREATE TABLE IF NOT EXISTS modules(module_key TEXT PRIMARY KEY,module_id TEXT NOT NULL,descriptor_sha256 TEXT NOT NULL,artifact_sha256 TEXT,capabilities TEXT NOT NULL,source_ref TEXT NOT NULL,added_utc TEXT NOT NULL,artifact_ref TEXT);CREATE TABLE IF NOT EXISTS module_requests(request_id TEXT PRIMARY KEY,module_key TEXT NOT NULL,intent TEXT NOT NULL,state TEXT NOT NULL,recorded_utc TEXT NOT NULL);CREATE INDEX IF NOT EXISTS module_requests_recent ON module_requests(recorded_utc DESC,request_id DESC);");
            exec("BEGIN IMMEDIATE");try{bool found=false;{Statement info(db,"PRAGMA table_info(modules)");while(info.step()==SQLITE_ROW)found|=info.text(1)=="artifact_ref";}if(!found)exec("ALTER TABLE modules ADD COLUMN artifact_ref TEXT");exec("COMMIT");}catch(...){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }catch(...){sqlite3_close(db);db=nullptr;throw;}return db;
    }
};
EngineModuleRegistry::EngineModuleRegistry(QString database):d_(std::make_unique<Impl>(std::move(database))){}
EngineModuleRegistry::~EngineModuleRegistry()=default;
void EngineModuleRegistry::close(){d_->owner();if(d_->db){need(sqlite3_close(d_->db)==SQLITE_OK,"Registry close failed");d_->db=nullptr;}}
QJsonObject EngineModuleRegistry::registerModule(const QJsonValue& input,const QString& sourceRef){
    d_->owner();need(input.isObject(),"Module package required");const auto o=input.toObject();const bool package=o.value("schema")=="pgn-engine-package-1";const auto c=validateEngineModuleCapabilities(package?o.value("capabilities"):input);
    for(const auto* key:{"id","protocol"})need(jsUtf8(c.value(key).toString()).size()<=128,"Bounded module identity required");
    auto artifact=package?o.value("artifact_sha256"):QJsonValue::Null;auto reference=package?o.value("artifact_ref"):QJsonValue::Null;
    if(artifact.isUndefined())artifact=QJsonValue::Null;if(reference.isUndefined())reference=QJsonValue::Null;
    need(artifact.isNull()||(artifact.isString()&&artifact.toString().size()==64&&QRegularExpression("^[a-fA-F0-9]{64}$").match(artifact.toString()).hasMatch()),"Exact artifact digest or unknown required");
    need(reference.isNull()||(reference.isString()&&!reference.toString().isEmpty()&&jsUtf8(reference.toString()).size()<=2048&&!reference.toString().contains(QChar(0))),"Bounded opaque artifact reference or unknown required");
    need(jsUtf8(sourceRef).size()<=2048,"Bounded source reference required");if(artifact.isString())artifact=artifact.toString().toLower();
    const auto canonicalDescriptor=canonicalEngineModuleJson(c);need(canonicalDescriptor.size()<=32768,"Module descriptor byte budget exceeded, no truncation");const auto descriptorHash=sha(c);
    QJsonObject identity{{"module_id",c.value("id")},{"descriptor_sha256",descriptorHash},{"artifact_sha256",artifact}};if(!reference.isNull())identity.insert("artifact_ref",reference);const auto key=sha(identity);
    Statement insert(d_->open(),"INSERT OR IGNORE INTO modules(module_key,module_id,descriptor_sha256,artifact_sha256,capabilities,source_ref,added_utc,artifact_ref) VALUES(?,?,?,?,?,?,?,?)");
    insert.bind(1,key);insert.bind(2,c.value("id"));insert.bind(3,descriptorHash);insert.bind(4,artifact);insert.bind(5,QString::fromUtf8(canonicalDescriptor));insert.bind(6,sourceRef);insert.bind(7,timestamp());insert.bind(8,reference);need(insert.step()==SQLITE_DONE,"Registry insert incomplete");const bool created=sqlite3_changes(d_->db)==1;
    return {{"module_key",key},{"created",created},{"module_id",c.value("id")},{"descriptor_sha256",descriptorHash},{"artifact_sha256",artifact},{"artifact_ref",reference},{"scope","DECLARATION_ONLY_NOT_QUALIFIED_OR_APPLIED"},{"gpu_control_calls",0}};
}
QJsonObject EngineModuleRegistry::descriptor(const QString& key){
    d_->owner();moduleKey(key);Statement row(d_->open(),"SELECT module_key,module_id,descriptor_sha256,artifact_sha256,capabilities,source_ref,added_utc,artifact_ref FROM modules WHERE module_key=?");row.bind(1,key);need(row.step()==SQLITE_ROW,"Module not registered");
    const auto raw=row.text(4).toString();const auto parsed=parsedObject(raw.toUtf8());
    QJsonObject result{{"module_key",row.text(0)},{"module_id",row.text(1)},{"descriptor_sha256",row.text(2)},{"artifact_sha256",row.text(3)},{"capabilities",parsed?QJsonValue(*parsed):QJsonValue(QJsonValue::Null)},{"source_ref",row.text(5)},{"added_utc",row.text(6)},{"artifact_ref",row.text(7)}};
    if(!parsed){result.insert("protocol",QJsonValue::Null);result.insert("descriptor_unavailable","NATIVE_QT_JSON_UNAVAILABLE_NOT_A_DECLARATION");result.insert("capabilities_json",raw);}return result;
}
QJsonObject EngineModuleRegistry::receipt(const QString& id){
    d_->owner();requestId(id);Statement row(d_->open(),"SELECT request_id,module_key,intent,state,recorded_utc FROM module_requests WHERE request_id=?");row.bind(1,id);need(row.step()==SQLITE_ROW,"Module request not found");
    return {{"schema","pgn-engine-selection-receipt-1"},{"request_id",row.text(0)},{"module_key",row.text(1)},{"intent",parseObject(row.text(2).toString().toUtf8())},{"state",row.text(3)},{"recorded_utc",row.text(4)},
        {"reason","Controller qualification/admission required; current source unchanged"},{"gpu_control_calls",0},{"reservation_changes",0},{"applied",false}};
}
QJsonObject EngineModuleRegistry::request(const QJsonObject& intent){
    d_->owner();for(const auto* key:{"request_id","module_key","device_id","owner","expected_run"})need(intent.value(key).isString()&&!intent.value(key).toString().isEmpty(),"Explicit module user intent fields required");
    const auto id=field(intent,"request_id"),key=field(intent,"module_key");requestId(id);need(intent.value("boundary")=="NEXT_JOB_ONLY","Hot engine replacement not admitted");moduleKey(key);
    if(truthy(intent.value("job"))){const auto declaration=descriptor(key),job=intent.value("job").toObject();createEngineModuleRequest(observeEngineModule(declaration.value("capabilities"),{{"artifact_sha256",declaration.value("artifact_sha256")},{"source",declaration.value("source_ref")},{"observed_utc",QJsonValue::Null}}),{{"request_id",id},{"device_id",intent.value("device_id")},{"owner",intent.value("owner")},{"start",job.value("start")},{"length",job.value("length")}});}
    else{Statement exists(d_->open(),"SELECT module_key FROM modules WHERE module_key=?");exists.bind(1,key);need(exists.step()==SQLITE_ROW,"Module not registered");} // metadata intent, no DOM/job admission
    const auto payload=canonicalEngineModuleJson(intent);need(payload.size()<=32768,"Module intent byte budget exceeded");const auto db=d_->open();
    {Statement old(db,"SELECT module_key,intent FROM module_requests WHERE request_id=?");old.bind(1,id);if(old.step()==SQLITE_ROW){need(old.text(0)==key&&old.text(1).toString().toUtf8()==payload,"Conflicting immutable request identity, no overwrite");return receipt(id);}}
    {Statement insert(db,"INSERT OR IGNORE INTO module_requests VALUES(?,?,?,?,?)");insert.bind(1,id);insert.bind(2,key);insert.bind(3,QString::fromUtf8(payload));insert.bind(4,QString("REQUESTED_NOT_ADMITTED"));insert.bind(5,timestamp());need(insert.step()==SQLITE_DONE,"Request insert incomplete");}
    const auto result=receipt(id);need(result.value("module_key")==key&&canonicalEngineModuleJson(result.value("intent"))==payload,"Concurrent conflicting request identity, no overwrite");return result;
}
QJsonObject EngineModuleRegistry::page(const QString& after,int size){
    d_->owner();if(!after.isEmpty())moduleKey(after);need(size>0&&size<=128,"Explicit bounded module page required");
    if(!d_->db&&!QFileInfo::exists(d_->file))return {{"items",QJsonArray{}},{"next_cursor",QJsonValue::Null},{"latest_request",QJsonValue::Null}};
    QJsonArray items;{Statement rows(d_->open(),"SELECT module_key,module_id,descriptor_sha256,artifact_sha256,capabilities,added_utc,source_ref,artifact_ref FROM modules WHERE module_key>? ORDER BY module_key LIMIT ?");rows.bind(1,after);need(sqlite3_bind_int(rows.value,2,size+1)==SQLITE_OK,"Page limit binding failed");while(rows.step()==SQLITE_ROW){
        const auto parsed=parsedObject(rows.text(4).toString().toUtf8());QJsonObject item{{"module_key",rows.text(0)},{"module_id",rows.text(1)},{"descriptor_sha256",rows.text(2)},{"artifact_sha256",rows.text(3)},{"added_utc",rows.text(5)},{"protocol",parsed?parsed->value("protocol"):QJsonValue(QJsonValue::Null)},{"scope","DECLARATION_ONLY_NOT_QUALIFIED_OR_APPLIED"}};
        if(!parsed){item.insert("descriptor_unavailable","NATIVE_QT_JSON_UNAVAILABLE_NOT_A_DECLARATION");item.insert("source_ref",rows.text(6));item.insert("artifact_ref",rows.text(7));}items.append(item);
    }}
    const bool more=items.size()>size;if(more)items.removeLast();QJsonValue latest=QJsonValue::Null;{Statement row(d_->db,"SELECT request_id FROM module_requests ORDER BY recorded_utc DESC,request_id DESC LIMIT 1");if(row.step()==SQLITE_ROW)latest=receipt(row.text(0).toString());}
    return {{"items",items},{"next_cursor",more?QJsonValue(items.last().toObject().value("module_key")):QJsonValue(QJsonValue::Null)},{"latest_request",latest}};
}
QJsonObject handleModuleRegistryCommand(EngineModuleRegistry& registry,const QJsonObject& command){
    const auto request=field(command,"request_id");requestId(request);QJsonObject result{{"type","module_registry"},{"request_id",request},{"scope","LOCAL_MODULE_DECLARATIONS_AND_INTENTS_ONLY"},{"gpu_control_calls",0},{"applied",false}};
    const auto action=field(command,"action");
    if(action=="module_list"){auto after=command.value("after");if(after.isNull()||after.isUndefined())after="";need(after.isString(),"Module cursor string required");result.insert("page",registry.page(after.toString(),32));}
    else if(action=="module_register"){auto source=command.value("source_ref");if(source.isUndefined())source="User supplied descriptor";need(source.isString(),"Source reference string required");result.insert("registration",registry.registerModule(command.value("package"),source.toString()));result.insert("page",registry.page());}
    else if(action=="module_select")result.insert("selection",registry.request({{"request_id",request},{"module_key",command.value("module_key")},{"device_id",command.value("device_id")},{"owner",command.value("owner")},{"expected_run",command.value("expected_run")},{"boundary","NEXT_JOB_ONLY"}}));
    else if(action=="module_receipt")result.insert("selection",registry.receipt(field(command,"selection_id")));
    else need(false,"Unsupported module registry command, no executor operation");return result;
}
}

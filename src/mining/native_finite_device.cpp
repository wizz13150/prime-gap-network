#include "mining/native_finite_device.h"
#include <QDateTime>
#include <QDir>
#include <QMetaType>
#include <QRegularExpression>
#include <stdexcept>

namespace pgn::mining {
namespace {
void require(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
void keys(const QJsonObject& object,QStringList expected){auto actual=object.keys();actual.sort();expected.sort();require(actual==expected,"Finite observation keys differ from existing ABI");}
QString string(const QJsonObject& object,const char* key,qsizetype limit=4096){
    const auto v=object.value(QLatin1String(key));require(v.isString(),"Finite observation string missing/type invalid");
    const auto text=v.toString();require(text.size()<=limit&&!text.contains(QChar(0))&&QString::fromUtf8(text.toUtf8())==text,"Finite observation UTF8/text bound invalid");return text;
}
bool boolean(const QJsonObject& object,const char* key){const auto v=object.value(QLatin1String(key));require(v.isBool(),"Finite observation boolean missing/type invalid");return v.toBool();}
QString run(const QString& value){require(QRegularExpression("^[0-9a-f]{32}$").match(value).capturedLength()==32,"Finite run must be exact lowercase32hex");return value;}
QString utc(const QString& value){
    const auto match=QRegularExpression("^([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2})\\.[0-9]{7}Z$").match(value);
    require(match.hasMatch()&&match.capturedLength()==value.size(),"Finite observation requires exact UTC o7 timestamp");
    const auto date=QDateTime::fromString(match.captured(1)+'Z',Qt::ISODate);
    require(date.isValid()&&date.toUTC().toString("yyyy-MM-dd'T'HH:mm:ss")==match.captured(1),"Finite observation UTC calendar invalid");return value;
}
qint64 integer(const QJsonObject& object,const char* key,const QString& path,const QHash<QString,QByteArray>& scalars,qint64 low,qint64 high){
    const auto value=object.value(QLatin1String(key));const auto lexeme=scalars.value(path);
    require(!lexeme.isEmpty()&&QRegularExpression("^(?:0|[1-9][0-9]*|-[1-9][0-9]*)$").match(QString::fromLatin1(lexeme)).capturedLength()==lexeme.size(),"Finite integer lexeme noncanonical/absent");
    bool ok=false;const auto result=lexeme.toLongLong(&ok);
    require(ok&&result>=low&&result<=high&&value.toVariant().metaType().id()==QMetaType::LongLong&&value.toInteger()==result,"Finite exact integer/type/range differs");return result;
}
QString profilePath(const QString& value,const QString& expected){
    require(!value.contains('\r')&&!value.contains('\n'),"Finite path control characters invalid");
    auto path=value;path.replace('\\','/');
    require(QDir::cleanPath(path).compare(expected,Qt::CaseInsensitive)==0,"Finite job path differs from fixed run/profile");return value;
}
}
NativeFiniteObservation projectNativeFiniteObservation(const QJsonObject& packet,const QHash<QString,QByteArray>& scalars){
    keys(packet,{"schema","device","scope","observed_utc","owned","manifest","stop_requested","desired_enabled","last_exit","last_error","source_credit","auto_advance"});
    require(string(packet,"schema")=="pgn-device-control-observation-1"&&string(packet,"device")==Finite3060Device&&string(packet,"scope")=="BOUNDED_SCIENCE_TEST","Finite observation schema/device/scope incompatible");
    require(!boolean(packet,"source_credit")&&!boolean(packet,"auto_advance"),"Finite observation cannot grant SOURCE credit/automatic advance");
    NativeFiniteObservation result;result.device=string(packet,"device");result.scope=string(packet,"scope");result.observedUtc=utc(string(packet,"observed_utc",40));
    result.owned=boolean(packet,"owned");result.desiredEnabled=boolean(packet,"desired_enabled");result.stopRequested=boolean(packet,"stop_requested");
    const auto manifest=packet.value("manifest");require(manifest.isNull()||manifest.isObject(),"Finite manifest absent/type invalid");
    if(manifest.isObject()){
        const auto m=manifest.toObject();keys(m,{"run","uuid","pid","birth","image_sha256","start","length","stop","stdout","stderr","request_id"});
        NativeFiniteManifest typed;typed.run=run(string(m,"run",32));typed.pid=QString::number(integer(m,"pid","/manifest/pid",scalars,1,4294967295LL));typed.birth=utc(string(m,"birth",40));
        typed.imageSha256=string(m,"image_sha256",64);typed.start=string(m,"start",160);typed.length=string(m,"length",160);typed.requestId=string(m,"request_id",96);
        require(string(m,"uuid")==Finite3060Device&&typed.imageSha256==Finite3060ImageSha256&&typed.start==Finite3060Start&&typed.length==Finite3060Length,"Finite manifest profile/namespace/pin differs");
        require(!typed.requestId.isEmpty()&&QRegularExpression("^[a-zA-Z0-9_-]{1,96}$").match(typed.requestId).capturedLength()==typed.requestId.size(),"Finite worker request identity invalid");
        const auto job=QString("C:/PGN/runtime/devices/test-3060/jobs/")+typed.run;
        typed.stopPath=profilePath(string(m,"stop"),job+".stop");typed.stdoutPath=profilePath(string(m,"stdout"),job+".stdout");typed.stderrPath=profilePath(string(m,"stderr"),job+".stderr");result.manifest=typed;
    }else require(!result.owned&&!result.stopRequested,"Finite ownership/stop marker without manifest is ambiguous");
    const auto exit=packet.value("last_exit");require(exit.isNull()||exit.isObject(),"Finite last exit absent/type invalid");
    if(exit.isObject()){const auto e=exit.toObject();keys(e,{"run","exit_code","utc"});NativeFiniteExit typed;typed.run=run(string(e,"run",32));typed.utc=utc(string(e,"utc",40));typed.exitCode=qint32(integer(e,"exit_code","/last_exit/exit_code",scalars,-2147483648LL,2147483647LL));result.lastExit=typed;}
    const auto error=packet.value("last_error");require(error.isNull()||error.isObject(),"Finite last error absent/type invalid");
    if(error.isObject()){const auto e=error.toObject();keys(e,{"utc","error"});NativeFiniteError typed;typed.utc=utc(string(e,"utc",40));typed.error=string(e,"error",8192);require(!typed.error.isEmpty(),"Finite error text missing");result.lastError=typed;}
    result.packet=packet;return result;
}
}

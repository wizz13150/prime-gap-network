#include "mining/native_runtime_configuration.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <stdexcept>

namespace pgn::mining {
namespace {
class Failure final:public std::runtime_error {
public:QString code;Failure(QString c,const char* message):std::runtime_error(message),code(std::move(c)){}
};
void need(bool value,const char* code,const char* message){if(!value)throw Failure(QString::fromLatin1(code),message);}
void text(const QString& value,qsizetype max,const char* code){need(value.size()<=max&&value.isValidUtf16()&&!value.contains(QChar(0))&&!value.contains('\r')&&!value.contains('\n'),code,"Bounded Unicode text without NUL/line controls required");}
void absoluteLocal(const QString& value,const char* code){
    text(value,4096,code);need(!value.isEmpty()&&QFileInfo(value).isAbsolute(),code,"Explicit absolute local path required, no inferred working directory");
#ifdef Q_OS_WIN
    need(QRegularExpression("^[A-Za-z]:[\\\\/]").match(value).hasMatch(),code,"Explicit local Windows drive-absolute path required, no drive-relative/UNC access");
#endif
}
void remoteLabel(const QString& label){text(label,4096,"CheckpointLabel");need(QRegularExpression("^[A-Za-z]:[\\\\/]").match(label).hasMatch(),"CheckpointLabel","Explicit REMOTE Windows drive-absolute checkpoint label required");} // no QFileInfo/local stat
void validatePaths(const NativeRuntimePaths& paths){
    for(const auto& path:{paths.bookingDatabase,paths.sourceHistoryDatabase,paths.moduleRegistryDatabase}){
        absoluteLocal(path,"RuntimePath");const QFileInfo info(path);need(!info.exists()||info.isFile(),"RuntimePathType","Existing database path is not a regular file");
    }
    absoluteLocal(paths.sourceMirrorDirectory,"RuntimePath");const QFileInfo mirror(paths.sourceMirrorDirectory);need(!mirror.exists()||mirror.isDir(),"RuntimePathType","Existing mirror path is not a directory");remoteLabel(paths.checkpointLabel);
}
void skip(const QByteArray& b,qsizetype& at){while(at<b.size()&&(b[at]==' '||b[at]=='\t'||b[at]=='\r'||b[at]=='\n'))++at;}
qsizetype stringEnd(const QByteArray& b,qsizetype at){need(at<b.size()&&b[at]=='"',"JsonShape","JSON object key required");while(++at<b.size()){if(b[at]=='\\')++at;else if(b[at]=='"')return at+1;}throw Failure("InvalidJson","Incomplete JSON string");}
// Qt is the JSON grammar authority. This small bounded walk only rejects
// duplicate DECODED keys before Qt's last-wins object can erase evidence.
void unique(const QByteArray& b,qsizetype& at,int depth){
    need(depth<=8,"JsonDepth","Recovery schema JSON nesting exceeds8");skip(b,at);need(at<b.size(),"InvalidJson","Incomplete configuration JSON");
    if(b[at]=='"'){at=stringEnd(b,at);return;}
    if(b[at]=='{'||b[at]=='['){const bool object=b[at]=='{';const char end=object?'}':']';++at;skip(b,at);QSet<QString> seen;
        while(at<b.size()&&b[at]!=end){if(object){const auto first=at;at=stringEnd(b,at);const auto decoded=QJsonDocument::fromJson("["+b.mid(first,at-first)+"]");need(decoded.isArray()&&decoded.array().size()==1&&decoded.array().first().isString(),"InvalidJson","Configuration key cannot be decoded exactly");const auto key=decoded.array().first().toString();need(!seen.contains(key),"DuplicateKey","Duplicate decoded configuration key; no last-wins fallback");seen.insert(key);skip(b,at);need(at<b.size()&&b[at++]==':',"InvalidJson","Configuration JSON separator missing");}
            unique(b,at,depth+1);skip(b,at);if(at<b.size()&&b[at]==','){++at;skip(b,at);}else break;}
        need(at<b.size()&&b[at++]==end,"InvalidJson","Incomplete configuration JSON container");return;
    }
    while(at<b.size()&&b[at]!=','&&b[at]!='}'&&b[at]!=']')++at;
}
void keys(const QJsonObject& o,QStringList wanted){auto got=o.keys();got.sort();wanted.sort();need(got==wanted,"SchemaFields","Configuration fields missing/unknown; no silently ignored port or settings");}
QString field(const QJsonObject& o,const char* key,qsizetype max){const auto value=o.value(QLatin1String(key));need(value.isString(),"FieldType","Configuration field must be an explicit string, no token conversion");const auto result=value.toString();text(result,max,"FieldValue");return result;}
void captured(NativeRuntimeConfigurationResult& result,const QByteArray& bytes){result.sourceBytes=bytes.size();if(bytes.size()<=NativeRuntimeConfigurationLimit){result.sourceUtf8=bytes;result.sourceSha256=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}}
void error(NativeRuntimeConfigurationResult& result,const std::exception& e){result.configuration.reset();if(const auto* failure=dynamic_cast<const Failure*>(&e))result.errorCode=failure->code;else result.errorCode="EndpointOrInternal";result.error=QString::fromUtf8(e.what()).left(400);}
}
NativeRuntimeConfigurationResult decodeNativeRuntimeConfiguration(const QByteArray& bytes,const QString& program,const NativeRuntimePaths& paths,const QString& origin) noexcept{
    NativeRuntimeConfigurationResult result;result.sourcePath=origin;
    try{
        captured(result,bytes);need(!bytes.isEmpty()&&bytes.size()<=NativeRuntimeConfigurationLimit,"InputBudget","Configuration must be nonempty and at most16384bytes, no truncation");
        need(!bytes.startsWith("\xef\xbb\xbf")&&QString::fromUtf8(bytes).toUtf8()==bytes,"InvalidUtf8","Strict original UTF8 required, no BOM/encoding replacement");
        QJsonParseError parseError;const auto doc=QJsonDocument::fromJson(bytes,&parseError);need(parseError.error==QJsonParseError::NoError&&doc.isObject(),"InvalidJson","Complete configuration JSON object required");
        qsizetype at=0;unique(bytes,at,0);skip(bytes,at);need(at==bytes.size(),"InvalidJson","Trailing configuration bytes");const auto object=doc.object();
        const auto schema=field(object,"schema",128);const bool scoped=schema=="pgn-native-host-transport-2";
        need(scoped||schema=="pgn-existing-host-transport-recovery-1","UnsupportedSchema","Unsupported explicit native transport configuration");keys(object,{"schema","research","qualification"});
        need(object.value("research").isObject(),"JsonShape","research object required");const auto research=object.value("research").toObject();keys(research,{"cuda"});need(research.value("cuda").isObject(),"JsonShape","research.cuda object required");const auto cuda=research.value("cuda").toObject();
        if(scoped){QStringList fields{"host","key","runtime_root","gpu_lease_root","host_key_alias"};if(cuda.contains("source_only"))fields.append("source_only");keys(cuda,fields);if(cuda.contains("source_only"))need(cuda.value("source_only").isBool(),"ManagedDomain","source_only must be an explicit local boolean");}else keys(cuda,{"host","key"});
        const auto target=field(cuda,"host",512),key=field(cuda,"key",4096),qualification=field(object,"qualification",2048);
        need(target.count('@')==1,"HostIdentity","Exactly one explicit user@host required");const auto user=target.section('@',0,0),host=target.section('@',1,1);
        need(!user.isEmpty()&&!host.isEmpty()&&QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.-]{0,252}$").match(host).capturedLength()==host.size(),"HostIdentity","Recovery-1 requires direct hostname/IPv4, no option/URL/port/IPv6/shell syntax");
        absoluteLocal(program,"SshProgram");absoluteLocal(key,"KeyPath");validatePaths(paths);
        const QFileInfo sshInfo(program);need(sshInfo.isFile()&&sshInfo.isExecutable(),"SshProgram","Explicit SSH executable file unavailable; no program fallback");need(QFileInfo(key).isFile(),"KeyPath","Explicit existing key file metadata unavailable; contents never opened");
        SshObservationEndpoint endpoint{program,key,host,user,22};
        if(scoped){endpoint.runtimeRoot=field(cuda,"runtime_root",4096);endpoint.gpuLeaseRoot=field(cuda,"gpu_lease_root",4096);endpoint.hostKeyAlias=field(cuda,"host_key_alias",253);need(!endpoint.runtimeRoot.isEmpty()&&!endpoint.gpuLeaseRoot.isEmpty(),"RuntimeRoot","Explicit scoped runtime andshared physical GPU lease roots required");}
        // PURE factory reuses explicit DIRECT argv policy/stat validation. No
        // process is instantiated, no ssh-G/config/remote/DNS operation occurs,
        // key/program contents are never opened. Default noDriver remainsfalse.
        (void)makeSshObservationCommand(endpoint,SshObservationRequest{});
        NativeRuntimeConfiguration config;config.endpoint=std::move(endpoint);config.paths=paths;config.schema=schema;config.sourcePath=origin;config.sourceUtf8=bytes;config.sourceSha256=*result.sourceSha256;config.qualificationOpaque=qualification;
        config.sourceOnly=scoped&&cuda.value("source_only").toBool();
        if(scoped){config.paths.checkpointLabel=nativeRemoteRuntimeRoot(config.endpoint)+"/current-job/checkpoint";config.paths.checkpointLabel.replace('/','\\');}
        result.configuration=std::move(config);
    }catch(const std::exception& e){error(result,e);}catch(...){result.errorCode="Internal";result.error="Configuration unavailable; no fallback";}
    return result;
}
NativeRuntimeConfigurationResult loadNativeRuntimeConfiguration(const QString& path,const QString& program,const NativeRuntimePaths& paths) noexcept{
    NativeRuntimeConfigurationResult result;result.sourcePath=path;
    try{
        absoluteLocal(path,"ConfigurationPath");const QFileInfo info(path);need(info.isFile(),"ConfigurationMissing","Explicit configuration regular file unavailable");QFile file(path);need(file.open(QIODevice::ReadOnly),"ConfigurationRead","Configuration file cannot be opened read-only");
        const auto before=file.size();result.sourceBytes=before;need(before>0&&before<=NativeRuntimeConfigurationLimit,"InputBudget","Configuration file size outside1..16384bytes");
        const auto bytes=file.read(NativeRuntimeConfigurationLimit+1);need(file.error()==QFileDevice::NoError,"ConfigurationRead","Configuration read failed");
        need(bytes.size()==before&&file.size()==before&&file.atEnd(),"ConfigurationChanged","Configuration changed/grew/truncated during bounded read");
        return decodeNativeRuntimeConfiguration(bytes,program,paths,path);
    }catch(const std::exception& e){error(result,e);}catch(...){result.errorCode="Internal";result.error="Configuration unavailable; no fallback";}return result;
}
}

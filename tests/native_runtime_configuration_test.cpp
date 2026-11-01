#include "mining/native_runtime_configuration.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace pgn::mining;
namespace {
int checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
QByteArray json(const QJsonObject& object){return QJsonDocument(object).toJson(QJsonDocument::Compact);}
QString sha(const QByteArray& b){return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());}
void write(const QString& path,const QByteArray& bytes){QFile file(path);check(file.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&file.write(bytes)==bytes.size()&&file.flush(),"create OWN fixture file once");}
struct Fixture {
    QTemporaryDir temp;QString program,key,config;NativeRuntimePaths paths;
    Fixture(){check(temp.isValid(),"OWN temporary fixture only");program=temp.filePath("fake-ssh-NOT-EXECUTED.exe");key=temp.filePath("PUBLIC-DATA-key-path-standin");config=temp.filePath("config.json");
        write(program,"PUBLIC FIXTURE NOT AN EXECUTABLE");write(key,"PUBLIC FIXTURE NOT A PRIVATE KEY");
        paths={temp.filePath("missing/booking.sqlite"),temp.filePath("missing/history.sqlite"),temp.filePath("missing/mirror"),temp.filePath("missing/modules.sqlite"),"C:\\REMOTE-LABEL-NOT-LOCAL\\current-job\\checkpoint"};
    }
    QJsonObject object(){return {{"schema","pgn-existing-host-transport-recovery-1"},{"research",QJsonObject{{"cuda",QJsonObject{{"host","wizz@wizz"},{"key",key}}}}},{"qualification","ORIGINAL opaque claim; NOT current connection/admission $(never-execute)"}};}
    void absent(){check(!QFile::exists(paths.bookingDatabase)&&!QFile::exists(paths.sourceHistoryDatabase)&&!QFile::exists(paths.moduleRegistryDatabase)&&!QFile::exists(paths.sourceMirrorDirectory)&&!QFile::exists(temp.filePath("missing")),"loader did not create/open/init runtime DB/directory");}
};
void refused(Fixture& f,const QByteArray& raw,const QString& code={}){
    const auto r=decodeNativeRuntimeConfiguration(raw,f.program,f.paths,"original-provenance-label");
    check(!r.available()&&!r.configuration&&!r.error.isEmpty()&&r.error.size()<=400&&!r.errorCode.isEmpty(),"explicit unavailable result, never fallback endpoint");
    if(!code.isEmpty())check(r.errorCode==code,"precise failure code");
    check(r.sourcePath=="original-provenance-label"&&r.sourceBytes&&*r.sourceBytes==raw.size(),"original provenance/size retained on rejection");
    if(raw.size()<=NativeRuntimeConfigurationLimit)check(r.sourceUtf8&&*r.sourceUtf8==raw&&r.sourceSha256&&*r.sourceSha256==sha(raw),"failed original bounded bytes+SHA retained exactly");
    else check(!r.sourceUtf8&&!r.sourceSha256,"oversize input not copied/truncated/hash invented");f.absent();
}
QString decodedScript(const SshObservationCommand& command){const auto remote=command.arguments.last();const auto b=QByteArray::fromBase64(remote.mid(remote.lastIndexOf(' ')+1).toLatin1());check(b.size()%2==0&&!b.isEmpty(),"existing pure factory UTF16LE command");QString s;for(int i=0;i<b.size();i+=2)s+=QChar(quint8(b[i])|(quint16(quint8(b[i+1]))<<8));return s;}
void successAndProvenance(){
    Fixture f;const auto raw=" \n"+json(f.object())+"\r\n";const auto r=decodeNativeRuntimeConfiguration(raw,f.program,f.paths,"ORIGINAL source reference");
    check(r.available()&&r.error.isEmpty()&&r.errorCode.isEmpty(),"strict actual recovery schema available as data, not Ready");const auto& c=*r.configuration;
    check(c.endpoint.executable==f.program&&c.endpoint.keyFile==f.key&&c.endpoint.host=="wizz"&&c.endpoint.user=="wizz"&&c.endpoint.port==22,"exact explicit DIRECT endpoint, only legacy port22");
    check(c.sourceUtf8==raw&&c.sourceSha256==sha(raw)&&c.sourcePath=="ORIGINAL source reference"&&c.qualificationOpaque==f.object().value("qualification"),"original bytes/hash/opaque historical qualification unchanged");
    check(c.paths.bookingDatabase==f.paths.bookingDatabase&&c.paths.sourceHistoryDatabase==f.paths.sourceHistoryDatabase&&c.paths.moduleRegistryDatabase==f.paths.moduleRegistryDatabase&&c.paths.sourceMirrorDirectory==f.paths.sourceMirrorDirectory&&c.paths.checkpointLabel==f.paths.checkpointLabel,"all5paths explicit, remote label not local-opened/statrequired");
    const auto command=makeSshObservationCommand(c.endpoint,SshObservationRequest{});const auto args=command.arguments;
    for(const auto& token:QStringList{"none","StrictHostKeyChecking=yes","UpdateHostKeys=no","CheckHostIP=no","ClearAllForwardings=yes","PermitLocalCommand=no","RequestTTY=no","BatchMode=yes"})check(args.contains(token),"existing DIRECT/trust/forwarding constraints unchanged");
    check(args.first()=="-F"&&args.contains(f.key)&&args[args.size()-3]=="--"&&args[args.size()-2]=="wizz","separate argv no shell/config interpretation");check(!decodedScript(command).contains("-NoDriver"),"default request.noDriver staysfalse; no new flag inferred by configuration");
    f.absent();write(f.config,raw);const auto fromFile=loadNativeRuntimeConfiguration(f.config,f.program,f.paths);
    check(fromFile.available()&&fromFile.configuration->sourceUtf8==raw&&fromFile.sourcePath==f.config,"bounded read-only same config, original file provenance");QFile checkFile(f.config);check(checkFile.open(QIODevice::ReadOnly)&&checkFile.readAll()==raw,"configuration source bytes not rewritten");f.absent();
#ifdef Q_OS_WIN
    // Deny data-open of our fake key while metadata queries remain possible.
    // This is PUBLIC own fixture data, never a real key or SSH executable.
    HANDLE held=CreateFileW(reinterpret_cast<LPCWSTR>(f.key.utf16()),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(held!=INVALID_HANDLE_VALUE,"own fake key exclusive handle");
    {QFile forbidden(f.key);check(!forbidden.open(QIODevice::ReadOnly),"test handle actually denies key content reads");}
    const auto underLock=decodeNativeRuntimeConfiguration(raw,f.program,f.paths);CloseHandle(held);check(underLock.available(),"loader works under content-read denial: metadata only");
#endif
}
void syntax(){
    Fixture f;const auto good=json(f.object());
    for(const auto& raw:QList<QByteArray>{QByteArray{},"null","[]","{}",good.left(good.size()-1),good+"{}","\xff"+good,QByteArray("\xef\xbb\xbf")+good,good+'\0'})refused(f,raw);
    refused(f,QByteArray(NativeRuntimeConfigurationLimit+1,' '),"InputBudget");
    auto raw=good;raw.replace("\"schema\":","\"schema\":\"unknown\",\"sch\\u0065ma\":");refused(f,raw,"DuplicateKey");
    raw=good;raw.replace("\"host\":","\"host\":\"attacker@wrong\",\"h\\u006fst\":");refused(f,raw,"DuplicateKey");
    raw=good;raw.replace("\"cuda\":","\"cuda\":{},\"c\\u0075da\":");refused(f,raw,"DuplicateKey");
    for(const auto& token:QJsonArray{QJsonValue::Null,QJsonValue(false),QJsonValue(1.0),QJsonObject{},QJsonArray{}}){auto o=f.object();o["schema"]=token;refused(f,json(o),"FieldType");}
    auto o=f.object();o["schema"]="pgn-new-configuration-1";refused(f,json(o),"UnsupportedSchema");
    for(const auto* key:{"research","qualification"}){o=f.object();o.remove(key);refused(f,json(o),"SchemaFields");}
    o=f.object();o["fallback_node"]=true;refused(f,json(o),"SchemaFields");
    for(const auto& token:QList<QByteArray>{"22","22.0","2.2e1","\"22\"","null"}){raw=good;raw.replace("\"host\":","\"port\":"+token+",\"host\":");refused(f,raw,"SchemaFields");}
    for(const auto& target:QStringList{"wizz","@wizz","wizz@","a@b@c","wizz@-option","wizz@host:22","wizz@[::1]","wizz@https://host","wizz@host;echo BAD","wizz@host $(BAD)","wizz@host\n","wizz;BAD@host"}){o=f.object();auto research=o.value("research").toObject();auto cuda=research.value("cuda").toObject();cuda["host"]=target;research["cuda"]=cuda;o["research"]=research;refused(f,json(o));}
    o=f.object();o["qualification"]=QString(QChar(0xd800));raw=json(o); // Qt writer may replace; raw escaped poison is explicit below.
    raw=good;raw.replace("\"qualification\":", "\"qualification\":\"\\ud800\",\"unknown_original\":");refused(f,raw,"SchemaFields");
    raw=good;const auto old=QJsonDocument(QJsonArray{f.object().value("qualification")}).toJson(QJsonDocument::Compact);raw.replace(old.mid(1,old.size()-2),"\"\\ud800\"");refused(f,raw,"FieldValue");
}
void pathsAndFiles(){
    Fixture f;const auto raw=json(f.object());
    for(auto* path:{&f.paths.bookingDatabase,&f.paths.sourceHistoryDatabase,&f.paths.moduleRegistryDatabase,&f.paths.sourceMirrorDirectory}){const auto prior=*path;*path="relative/path";const auto r=decodeNativeRuntimeConfiguration(raw,f.program,f.paths);check(!r.available()&&r.errorCode=="RuntimePath","no implicit localpath default/workingdirectory");*path=prior;}
    auto paths=f.paths;paths.checkpointLabel="relative-checkpoint";check(!decodeNativeRuntimeConfiguration(raw,f.program,paths).available(),"remote checkpoint label cannot be relative");paths=f.paths;paths.checkpointLabel=QString("C:/remote/")+QChar(0);check(!decodeNativeRuntimeConfiguration(raw,f.program,paths).available(),"remote label NUL refused without local stat");
    check(!decodeNativeRuntimeConfiguration(raw,"ssh.exe",f.paths).available(),"SSH program path cannot be inferred from PATH");check(!decodeNativeRuntimeConfiguration(raw,f.temp.filePath("missing.exe"),f.paths).available(),"missing explicit program does not fall back to system/Node");
    auto o=f.object();auto research=o.value("research").toObject();auto cuda=research.value("cuda").toObject();cuda["key"]="relative-key";research["cuda"]=cuda;o["research"]=research;refused(f,json(o),"KeyPath");
    cuda["key"]=f.temp.filePath("missing-key");research["cuda"]=cuda;o["research"]=research;refused(f,json(o),"KeyPath");
    const auto missing=loadNativeRuntimeConfiguration(f.config,f.program,f.paths);check(!missing.available()&&missing.errorCode=="ConfigurationMissing"&&!missing.sourceBytes&&!missing.sourceUtf8&&!missing.sourceSha256&&missing.sourcePath==f.config,"absent config has absent bytes/hash, never default or0");
    write(f.config,QByteArray(NativeRuntimeConfigurationLimit+1,'x'));const auto oversized=loadNativeRuntimeConfiguration(f.config,f.program,f.paths);check(!oversized.available()&&oversized.errorCode=="InputBudget"&&oversized.sourceBytes==NativeRuntimeConfigurationLimit+1&&!oversized.sourceUtf8&&!oversized.sourceSha256,"oversize file stat rejects before full read/no truncated success");QFile checkFile(f.config);check(checkFile.open(QIODevice::ReadOnly)&&checkFile.size()==NativeRuntimeConfigurationLimit+1,"oversize source preserved");
    const auto wrongDir=f.temp.filePath("database-is-directory");check(QDir().mkdir(wrongDir),"own directory fixture");paths=f.paths;paths.bookingDatabase=wrongDir;check(!decodeNativeRuntimeConfiguration(raw,f.program,paths).available(),"existing DBpath directory refused");paths=f.paths;paths.sourceMirrorDirectory=f.key;check(!decodeNativeRuntimeConfiguration(raw,f.program,paths).available(),"existing mirror filepath refused");f.absent();
}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{successAndProvenance();syntax();pathsAndFiles();std::cout<<"NATIVE_RUNTIME_CONFIGURATION "<<checks<<" checks / 0 failures; own metadata/config files + PURE argv only, no SSH/keyread/process/GPU/UI/DB\n";return 0;}catch(const std::exception& e){std::cerr<<"NATIVE_RUNTIME_CONFIGURATION "<<checks<<" failure="<<e.what()<<'\n';return 1;}}

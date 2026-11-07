#pragma once
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <stdexcept>

namespace pgn::ui {
// Deployment resolution only. No key access, process launch or mutable state.
inline QJsonObject loadVanityRuntime(const QString& codeRoot,const QString& dataRoot,const QString& applicationDir){
 const auto fail=[](const char* text)->void{throw std::runtime_error(text);};
 const QString name=
#ifdef Q_OS_WIN
  "pgn-vanity-session.exe";
#else
  "pgn-vanity-session";
#endif
 QString base=codeRoot,specPath=dataRoot+"/vanity-runtime.json";
 const bool privateProfile=QFileInfo::exists(specPath);
 if(!privateProfile){
  specPath=base+"/EngineRuntime/vanity/runtime.json";
  if(!QFileInfo::exists(specPath)){base=applicationDir;specPath=base+"/EngineRuntime/vanity/runtime.json";}
 }
 QFile spec(specPath);if(!spec.open(QIODevice::ReadOnly)||spec.size()>8192)fail("Generator deployment unavailable.");
 auto profile=QJsonDocument::fromJson(spec.readAll()).object();
 if(profile.value("schema")!="pgn-vanity-runtime-1")fail("Generator deployment schema is invalid.");
 const auto resolve=[&](const QString& input){return QFileInfo(QDir::isAbsolutePath(input)?input:QDir(base).filePath(input)).canonicalFilePath();};
 const auto checkedHash=[&](const QString& path,const QString& expected,qint64 limit){
  QFile file(path);if(!QRegularExpression("^[0-9a-f]{64}$").match(expected).hasMatch()||!file.open(QIODevice::ReadOnly)||file.size()<=0||file.size()>limit)fail("Native generator fingerprint is unavailable or mismatched.");
  QCryptographicHash digest(QCryptographicHash::Sha256);if(!digest.addData(&file)||QString::fromLatin1(digest.result().toHex())!=expected)fail("Native generator fingerprint is unavailable or mismatched.");
 };
 const auto helper=resolve(profile.value("executable").toString());bool permitted=false;
 for(const auto& folder:{codeRoot+"/wallet/native-runtime",codeRoot+"/EngineRuntime/vanity",base+"/EngineRuntime/vanity"}){
  const auto canonical=QFileInfo(folder).canonicalFilePath();permitted|=!canonical.isEmpty()&&helper.startsWith(canonical+"/",Qt::CaseInsensitive);
 }
 const auto bundle=QFileInfo(base).canonicalFilePath();
 permitted|=!privateProfile&&!bundle.isEmpty()&&helper==bundle+"/"+name;
 if(!permitted||QFileInfo(helper).fileName()!=name)fail("Native generator path is outside its deployment.");
 checkedHash(helper,profile.value("sha256").toString(),67108864);profile.insert("executable",helper);
 const auto module=profile.value("local_gpu_module").toString();
 if(!module.isEmpty()&&!privateProfile){
  const auto resolved=resolve(module),scope=QFileInfo(base+"/EngineRuntime/vanity").canonicalFilePath();
  if(scope.isEmpty()||!resolved.startsWith(scope+"/",Qt::CaseInsensitive)||QFileInfo(resolved).fileName()!="pgn-vanity-gpu.dll")fail("GPU generator path is outside its deployment.");
  checkedHash(resolved,profile.value("local_gpu_module_sha256").toString(),134217728);profile.insert("local_gpu_module",resolved);
 }
 // Remote strings become shell arguments later: retain one canonical, unquoted
 // Windows-path alphabet and reject traversal/metacharacters before use.
 for(const auto& key:{"remote_executable","remote_gpu_module","remote_gpu_lease_file"}){
  const auto value=profile.value(key).toString();
  if(!value.isEmpty()&&(!QRegularExpression("^[A-Za-z]:/[A-Za-z0-9_./-]+$").match(value).hasMatch()||QDir::cleanPath(value)!=value||value.contains("/../")))fail("Remote generator deployment path is invalid.");
 }
 if(!profile.value("remote_executable").toString().isEmpty()&&QFileInfo(profile.value("remote_executable").toString()).fileName()!=name)fail("Remote generator executable is invalid.");
 return profile;
}
}

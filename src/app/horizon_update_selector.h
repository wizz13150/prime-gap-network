#pragma once
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <optional>
#include <stdexcept>

namespace pgn::app {
struct SelectedCohort {QString executable,error;};
// The selector was committed only after reference-TUF verification and native
// startup health. Re-check its binding and UI files when an old shortcut runs.
// This is local installation integrity, not a new signature/trust authority.
inline SelectedCohort selectedUpdateCohort(const QString& applicationDirectory,const QString& store,
        const QString& network,const QString& genesis,bool healthCheck){
 if(healthCheck||!QFileInfo::exists(store+"/current.json"))return {};
 const auto require=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
 const auto read=[&](const QString& path,qint64 maximum){QFile file(path);const QFileInfo info(path);require(info.isFile()&&!info.isSymLink()&&!info.isJunction()&&file.open(QIODevice::ReadOnly)&&file.size()<=maximum,"Update selection file is unavailable");return file.readAll();};
 const auto digest=[](const QByteArray& raw){return QString::fromLatin1(QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex());};
 try{
  const auto pointer=QJsonDocument::fromJson(read(store+"/current.json",128*1024)).object();
  require(pointer.value("schema")=="PGN_ACTIVE_COHORT_V1"&&pointer.value("network")==network&&pointer.value("genesis_hash")==genesis,"Update selection belongs to another network");
  const auto directory=QFileInfo(pointer.value("cohort").toString()).canonicalFilePath();
  require(!directory.isEmpty(),"Selected update is absent");
  if(directory==QFileInfo(applicationDirectory).canonicalFilePath())return {};
  const auto parent=QFileInfo(store+"/cohorts").canonicalFilePath();
  require(!parent.isEmpty()&&QFileInfo(directory).canonicalPath()==parent&&!QFileInfo(directory).isSymLink()&&!QFileInfo(directory).isJunction(),"Selected update is outside its installation store");
  const auto raw=read(directory+"/components.json",128*1024);
  require(digest(raw)==pointer.value("inventory_sha256").toString(),"Selected update inventory changed");
  const auto manifest=QJsonDocument::fromJson(raw).object();
  require(manifest.value("schema")=="PGN_COMPONENT_MANIFEST_V1"&&manifest.value("network")==network,"Selected update inventory is invalid");
#ifdef Q_OS_WIN
  const QString primaryName="PGN-Horizon.exe";
#else
  const QString primaryName="PGN-Horizon";
#endif
  bool primary=false;int count=0;qint64 total=0;
  for(const auto& value:manifest.value("components").toArray()){
   const auto component=value.toObject();if(component.value("id")!="ui")continue;
   require(++count==1,"Duplicate selected UI component");
   const auto files=component.value("files").toArray();require(!files.isEmpty()&&files.size()<=512,"Selected UI inventory exceeds its bound");
   for(const auto& value:files){
    const auto row=value.toObject();const auto relative=row.value("path").toString();
    require(!relative.isEmpty()&&!QFileInfo(relative).isAbsolute()&&!relative.contains('\\')&&!relative.contains(':')&&!relative.split('/').contains("..")&&!relative.split('/').contains("."),"Selected UI path escapes its cohort");
    const auto path=directory+"/"+relative;const QFileInfo info(path);
    require(info.canonicalFilePath().startsWith(directory+"/")&&info.isFile()&&!info.isSymLink()&&!info.isJunction()&&info.size()==row.value("bytes").toInteger(-1)&&info.size()<=128*1024*1024,"Selected UI file differs");
    total+=info.size();require(total<=512*1024*1024,"Selected UI exceeds its byte budget");
    QFile file(path);require(file.open(QIODevice::ReadOnly),"Selected UI file cannot be read");QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file)&&QString::fromLatin1(hash.result().toHex())==row.value("sha256").toString(),"Selected UI fingerprint differs");
    if(relative==primaryName)primary=true;
   }
  }
  require(primary,"Selected UI entry point is absent");
  return {directory+"/"+primaryName,{}};
 }catch(const std::exception& error){return {{},QString::fromUtf8(error.what())};}
}
}

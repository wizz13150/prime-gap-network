#pragma once
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <stdexcept>
namespace pgn::update {
// One update transaction, one network/data scope, one consumption. This file
// never grants a normal application launch permission to start computation.
inline QString miningResumePath(const QString& data){return data+"/.update-mining-resume.json";}
inline bool validMiningResumeDevices(const QJsonArray& devices){
 if(devices.size()>64)return false;QSet<QString> seen;
 for(const auto& value:devices){
  if(!value.isObject())return false;const auto row=value.toObject();const auto id=row.value("device").toString(),kind=row.value("kind").toString();
  if(id.isEmpty()||id.size()>256||seen.contains(id.toLower())||(kind!="gpu"&&kind!="cpu"))return false;seen.insert(id.toLower());
  if(kind=="cpu"){const auto threads=row.value("threads");if(!threads.isDouble()||threads.toDouble()!=threads.toInt()||threads.toInt()<1||threads.toInt()>256)return false;}
 }
 return true;
}
inline void saveMiningUpdateResume(const QString& data,const QString& genesis,const QJsonArray& devices,qint64 now=QDateTime::currentSecsSinceEpoch()){
 if(genesis.size()!=64||!validMiningResumeDevices(devices))throw std::runtime_error("Mining update state is invalid; update was not started");
 QSaveFile file(miningResumePath(data));const auto bytes=QJsonDocument(QJsonObject{{"schema","PGN_UPDATE_MINING_RESUME_V1"},{"genesis",genesis},{"saved_at",now},{"devices",devices}}).toJson(QJsonDocument::Compact);
 if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())throw std::runtime_error("Cannot save mining state for the update; free disk space and retry");
}
inline QJsonArray takeMiningUpdateResume(const QString& data,const QString& genesis,bool updateRestart,qint64 now=QDateTime::currentSecsSinceEpoch()){
 QFile file(miningResumePath(data));if(!file.exists())return {};
 if(!updateRestart){if(!file.remove())throw std::runtime_error("Cannot retire unused update mining state");return {};}
 if(!file.open(QIODevice::ReadOnly)||file.size()>32768)throw std::runtime_error("Update mining state is unavailable or oversized");
 QJsonParseError error;const auto document=QJsonDocument::fromJson(file.readAll(),&error);file.close();const auto object=document.object();
 const auto saved=object.value("saved_at").toInteger(-1);const auto devices=object.value("devices").toArray();
 const bool valid=error.error==QJsonParseError::NoError&&object.value("schema")=="PGN_UPDATE_MINING_RESUME_V1"&&object.value("genesis")==genesis&&object.value("devices").isArray()&&saved>=0&&now>=saved&&now-saved<=900&&validMiningResumeDevices(devices);
 // Consume before issuing any ON request, including an empty/expired state.
 if(!file.remove())throw std::runtime_error("Cannot consume update mining state; automatic restart withheld");
 if(!valid)throw std::runtime_error("Saved update mining state is invalid or expired; start the selected devices manually");
 return devices;
}
}

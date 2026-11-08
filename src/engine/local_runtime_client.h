#pragma once
#include "ui/render/telemetry.h"
#include "mining/source_age.h"
#include <QProcess>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <functional>
#include <memory>
#include <cmath>

namespace pgn::engine {
// QProcess can emit finished from its base destructor while killing a child.
// The owner may already have destroyed the widgets captured by consume.
class LocalRuntimeQueryProcess final : public QProcess {
public:
 using QProcess::QProcess;
 ~LocalRuntimeQueryProcess() override {
  for(auto* child:children())if(auto* timer=qobject_cast<QTimer*>(child))timer->stop();
  QObject::disconnect(this,nullptr,nullptr,nullptr);
  if(state()!=QProcess::NotRunning){kill();waitForFinished(1000);}
 }
};

// Finite discovery/preparation only. No work is opened by these commands.
inline void localRuntimeQuery(QObject* owner,const QString& program,const QStringList& arguments,
                             const QJsonObject& input,std::function<void(QJsonObject,QString)> consume){
 auto* process=new LocalRuntimeQueryProcess(owner);auto* deadline=new QTimer(process);deadline->setSingleShot(true);
 auto bytes=std::make_shared<QByteArray>();auto done=std::make_shared<bool>(false);
 auto errors=std::make_shared<QByteArray>();
 const auto finish=[process,deadline,done,consume](QJsonObject value,const QString& error){
  if(*done)return;*done=true;deadline->stop();
  if(process->state()!=QProcess::NotRunning)process->kill();
  // The callback may synchronously delete owner, including its children.
  // Keep this signal emitter alive until Qt has returned from delivery.
  process->setParent(nullptr);process->deleteLater();consume(value,error);
 };
 QObject::connect(process,&QProcess::readyReadStandardOutput,process,[process,bytes,finish]{
  bytes->append(process->readAllStandardOutput());if(bytes->size()>262144)finish({},"Local runtime response exceeds 256 KiB");
 });
 QObject::connect(process,&QProcess::readyReadStandardError,process,[process,errors]{errors->append(process->readAllStandardError());if(errors->size()>2048)errors->remove(0,errors->size()-2048);});
 QObject::connect(process,&QProcess::errorOccurred,process,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish({},"Installed local runtime could not start");});
 QObject::connect(deadline,&QTimer::timeout,process,[finish]{finish({},"Local runtime discovery/preparation timed out");});
 QObject::connect(process,&QProcess::started,process,[process,input]{
  if(!input.isEmpty())process->write(QJsonDocument(input).toJson(QJsonDocument::Compact)+'\n');
  process->closeWriteChannel();
 });
 QObject::connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,[process,bytes,errors,finish,done](int code,QProcess::ExitStatus status){
  if(*done)return;bytes->append(process->readAllStandardOutput());QJsonParseError parse;
  const auto document=QJsonDocument::fromJson(*bytes,&parse);
  if(bytes->size()>262144||code||status!=QProcess::NormalExit||parse.error!=QJsonParseError::NoError||!document.isObject()){
   errors->append(process->readAllStandardError());
   const auto lastLine=QString::fromUtf8(errors->right(2048)).trimmed().section('\n',-1).trimmed().left(600);
   finish({},lastLine.isEmpty()?QString("Installed local runtime returned an invalid response"):QString("Mining controller: %1").arg(lastLine));return;
  }
  finish(document.object(),{});
 });
 process->setProgram(program);process->setArguments(arguments);deadline->start(20000);process->start();
}

inline std::optional<FrontierResourceTelemetrySnapshot> localHardwareSnapshot(const QJsonObject& packet){
 if(packet.value("schema")!="PGN_LOCAL_HARDWARE_V1"||packet.value("available")!=true||!packet.value("devices").isArray())return {};
 const auto devices=packet.value("devices").toArray();if(devices.size()>32)return {};
 FrontierResourceTelemetrySnapshot result;result.valid=true;result.observedUtc=QDateTime::currentDateTimeUtc();
 result.sequence=result.observedUtc.toMSecsSinceEpoch();result.receivedMonotonicNs=pgn::mining::receiptMonotonicNs();result.stale=false;
 const auto number=[](const QJsonObject& value,const char* key)->std::optional<double>{const auto n=value.value(key);return n.isDouble()&&std::isfinite(n.toDouble())&&n.toDouble()>=0?std::optional<double>(n.toDouble()):std::nullopt;};
 QSet<QString> identities;
 for(const auto& item:devices){
  const auto value=item.toObject();const auto id=value.value("id").toString();
  if(!id.startsWith("GPU-")||id.size()!=40||identities.contains(id))return {};identities.insert(id);
  FrontierResourceDevice device;device.kind="GPU";device.deviceId=id;device.name=value.value("name").toString().left(128);
  device.utilizationPercent=number(value,"utilization_percent");device.powerWatts=number(value,"power_watts");device.powerLimitWatts=number(value,"power_limit_watts");
  device.temperatureC=number(value,"temperature_c");device.smClockMhz=number(value,"core_clock_mhz");device.memoryClockMhz=number(value,"memory_clock_mhz");
  device.memoryUsedBytes=value.value("memory_used_bytes").toString();device.memoryTotalBytes=value.value("memory_total_bytes").toString();
  // No throttle observation is available from this inventory; never invent NONE.
  device.throttleTelemetryAuthoritative=false;result.devices.append(device);
 }
 return result;
}
}

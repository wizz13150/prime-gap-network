#pragma once
#include "ui/render/telemetry.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <functional>
class QWidget;
namespace pgn::ui {
QWidget* createDiagnosticCaptureView(QString exporter,std::function<QJsonObject()> sample,QWidget* parent=nullptr);
void saveDiagnosticCapture(QWidget*,const QString& filename);
// Reuse the verified component inventory. Never copy file paths, arbitrary
// reasons or credentials into a tester capture, and never invent a version.
inline QJsonObject diagnosticComponentSnapshot(const QJsonObject& snapshot){
 QJsonArray components;QSet<QString> seen;
 const auto unavailable=[] {return QJsonObject{{"schema","PGN_DIAGNOSTIC_COMPONENTS_V1"},{"available",false},{"components",QJsonArray{}}};};
 if(snapshot.value("schema")!="PGN_LOCAL_COMPONENT_VIEW_V1"||!snapshot.value("components").isArray())return unavailable();
 const auto rows=snapshot.value("components").toArray();if(rows.size()>16)return unavailable();
 static const QRegularExpression version("^[0-9]+(?:[.][0-9]+){1,4}(?:[-+][A-Za-z0-9._-]+)?$");
 for(const auto& value:rows){
  const auto row=value.toObject();const auto id=row.value("id").toString();
  if(!QStringList{"ui","core","gpu","cpu"}.contains(id))continue;
  if(seen.contains(id))return unavailable();seen.insert(id);
  const auto text=row.value("version").toString();
  const bool verified=row.value("state")=="matched_local_manifest"&&text.size()<=64&&version.match(text).hasMatch();
  components.append(QJsonObject{{"id",id},{"state",verified?"matched_local_manifest":"unidentified"},
   {"version",verified?QJsonValue(text):QJsonValue{}},{"primary_bound",verified&&row.value("primary_bound").toBool()}});
 }
 return {{"schema","PGN_DIAGNOSTIC_COMPONENTS_V1"},{"available",!components.isEmpty()},{"components",components}};
}
inline QJsonObject diagnosticResourceSnapshot(const FrontierResourceTelemetrySnapshot& resource){
 QJsonArray devices;
 const auto number=[](std::optional<double> value){return value?QJsonValue(*value):QJsonValue{};};
 for(const auto& device:resource.devices){
  if(devices.size()>=32)break;
  devices.append(QJsonObject{{"id",device.deviceId},{"name",device.name},{"kind",device.kind},
   {"utilization_percent",number(device.utilizationPercent)},{"power_watts",number(device.powerWatts)},{"power_limit_watts",number(device.powerLimitWatts)},
   {"temperature_c",number(device.temperatureC)},{"core_clock_mhz",number(device.smClockMhz)},{"memory_clock_mhz",number(device.memoryClockMhz)},
   {"memory_used_bytes",device.memoryUsedBytes},{"memory_total_bytes",device.memoryTotalBytes},{"throttle_class",device.throttleClass},{"throttle_authoritative",device.throttleTelemetryAuthoritative}});
 }
 return {{"observed_utc",resource.observedUtc.toString(Qt::ISODateWithMs)},{"stale",resource.stale},{"devices",devices}};
}
}

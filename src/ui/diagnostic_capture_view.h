#pragma once
#include "ui/render/telemetry.h"
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
class QWidget;
namespace pgn::ui {
QWidget* createDiagnosticCaptureView(QString exporter,std::function<QJsonObject()> sample,QWidget* parent=nullptr);
void saveDiagnosticCapture(QWidget*,const QString& filename);
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

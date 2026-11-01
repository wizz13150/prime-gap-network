#pragma once
#include "ui/position_formats.h"
#include "ui/render/metric_caption.h"
#include <QJsonArray>
#include <QColor>
#include <QJsonObject>
#include <QSet>
#include <QVector>
#include <optional>
#include <cmath>
namespace pgn::ui {
struct CpuCardMetric {std::optional<double> value;double scale{};QString text,label,tooltip;QColor color;};
inline QJsonArray cpuMetricSnapshot(const QVector<CpuCardMetric>& metrics){
 QJsonArray rows;for(const auto& metric:metrics)rows.append(QJsonObject{{"label",metric.label},{"text",metric.text},{"value",metric.value?QJsonValue(*metric.value):QJsonValue{}}});return rows;
}
inline std::optional<double> cpuMetricNumber(const QJsonObject& object,const char* key){
 const auto value=object.value(key);if(!value.isDouble()||!std::isfinite(value.toDouble())||value.toDouble()<0)return {};return value.toDouble();
}
inline std::optional<double> cpuMetricInteger(const QJsonObject& object,const char* key){
 const auto text=object.value(key).toString();bool okay=false;const auto value=text.toULongLong(&okay);if(!okay||QString::number(value)!=text)return {};return double(value);
}
inline QString cpuActivityCaption(const QJsonObject& status,bool connected){
 if(!connected)return "DISCONNECTED · activity unconfirmed";
 const auto state=status.value("state").toString();const QString role=status.value("validation_only")==true?"LOCAL CHECK":"AUDIT";
 if(state=="RUNNING")return role+" · RUNNING";
 if(state=="BUDGET_PAUSED")return role+" · PAUSED · CPU load protection";
 if(state=="DRAINING_BUDGET")return role+" · PAUSING · CPU load protection";
 if(state=="WAITING_AUDIT"||state=="IDLE")return status.value("enabled")==true?"READY · waiting for an assigned audit":"OFF · no calculation";
 if(state=="DRAINING_CONTROL"||state=="RECONFIGURING")return "CHANGING THREAD BUDGET";
 if(state=="DRAINING_CLOSE"||state=="STOPPING")return "STOPPING · saving progress";
 if(status.value("result_ready")==true)return role+" · COMPUTATION COMPLETE";
 if(state=="PAUSED"||state=="PAUSED_RECOVERED"||state=="OFF"||state=="CLOSED")return role+" · PAUSED · progress saved";
 return role+" · "+(state.isEmpty()?QString("STARTING"):state);
}
inline QVector<CpuCardMetric> cpuCardMetrics(const QJsonObject& status,bool connected,bool expanded,const QColor& accent){
 const auto telemetry=status.value("cpu_telemetry").toObject();const bool typed=telemetry.value("schema")=="PGN_CPU_TELEMETRY_V1";
 const auto busy=connected&&typed?cpuMetricNumber(telemetry,"worker_busy_percent"):std::nullopt;
 const auto load=connected?cpuMetricNumber(status,"machine_load_percent"):std::nullopt;
 const auto memoryTotal=connected&&typed?cpuMetricInteger(telemetry,"host_memory_total_bytes"):std::nullopt;
 const auto memoryUsed=connected&&typed?cpuMetricInteger(telemetry,"host_memory_used_bytes"):std::nullopt;
 const auto engineMemory=connected&&typed?cpuMetricInteger(telemetry,"engine_working_set_bytes"):std::nullopt;
 auto rate=connected?cpuMetricNumber(status,"rate_positions_per_second"):std::nullopt;
 const auto state=status.value("state").toString();const auto resident=status.value("resident_workers").toInt(-1),selected=status.value("requested_threads").toInt();
 const bool stopped=connected&&resident==0&&QSet<QString>{"IDLE","OFF","PAUSED","PAUSED_RECOVERED","WAITING_AUDIT","BUDGET_PAUSED","CLOSED","AUDIT_COMPUTED_SOURCE_PROOF_PENDING","PROBE_COMPLETE"}.contains(state);
 if(stopped)rate=0.;
 const auto done=cpuMetricInteger(status,"durable_completed_positions"),total=cpuMetricInteger(status,"total_positions");
 const auto progress=done&&total&&*total>0&&*done<=*total?std::optional<double>(*done/ *total*100.):std::nullopt;
 const auto percent=[](std::optional<double> value){return value?QString::number(*value,'f',0)+"%":QString("—");};
 const auto workerValue=connected&&resident>=0&&selected>0?std::optional<double>(resident):std::nullopt;
 const auto workerText=workerValue?QString("%1/%2").arg(resident).arg(selected):QString("—");
 const auto memoryFraction=memoryUsed&&memoryTotal&&*memoryTotal>0?std::optional<double>(*memoryUsed/ *memoryTotal):std::nullopt;
 const QString rateText=rate?compactMetricCaption(rate):(connected&&state=="RUNNING"?QString("START"):QString("—"));
 const auto position=positionFormats(status.value("durable_completed_positions").toString());
 const CpuCardMetric activity{busy,100.,percent(busy),"WORKER BUSY","CPU time used by owned native workers, divided by the selected thread budget. Host load is separate.",accent};
 const CpuCardMetric workers{workerValue,double(std::max(1,selected)),workerText,"WORKERS","Resident native workers / selected thread budget; waiting and paused modes may have zero workers.",QColor("#62cfff")};
 const CpuCardMetric throughput{rate,0.,rateText,"pos/s","Mean over up to five seconds of closed position counts; confirmed idle/pause is zero. It is not a primality-test count.",QColor("#b58cff")};
 const CpuCardMetric completed{done,total.value_or(0),done?position.human:QString("—"),connected?"DONE":"SAVED",position.decimal+" · "+position.binary+" · exact "+position.exact,QColor("#69efc5")};
 const CpuCardMetric completion{progress,100.,progress?QString::number(*progress,'f',1)+"%":QString("NO JOB"),"AUDIT %","Durable completed positions / assigned positions. Completion is not network acceptance.",QColor("#ffd166")};
 const CpuCardMetric workingSet{engineMemory,memoryTotal.value_or(0),engineMemory?QString::number(*engineMemory/1048576.,'f',*engineMemory<104857600?1:0):QString("—"),"RAM MiB","Sum of controller and worker working sets; shared pages may be counted more than once.",QColor("#69efc5")};
 if(!expanded)return {activity,workers,throughput,completion};
 return {activity,{load,100.,percent(load),"HOST LOAD","Total machine CPU utilization, including other applications.",accent},
  {memoryFraction,1.,memoryFraction?QString::number(*memoryFraction*100.,'f',0)+"%":QString("—"),"HOST RAM","Physical RAM used by all applications / total physical RAM.",QColor("#62cfff")},
  workingSet,workers,throughput,completed,completion};
}
}

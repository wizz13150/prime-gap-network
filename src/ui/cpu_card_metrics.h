#pragma once
#include "ui/position_formats.h"
#include "ui/render/card_metric_caption.h"
#include <QJsonArray>
#include <QColor>
#include <QJsonObject>
#include <QSet>
#include <QVector>
#include <optional>
#include <cmath>
namespace pgn::ui {
inline bool hasCpuThreadBudget(const QString& kind,const QJsonObject& raw){
 return kind=="CPU"&&raw.value("pgn_cpu_inventory").isObject()&&raw.value("pgn_cpu_maximum_workers").toInt()>0;
}
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
struct CpuMemoryPagesPresentation {bool active{};QString text,tooltip;QColor color;};
inline CpuMemoryPagesPresentation cpuMemoryPagesPresentation(const QJsonObject& status,bool connected){
 const auto pages=status.value("memory_pages").toObject();const auto bytes=cpuMetricInteger(pages,"large_page_bytes_in_use");
 const int resident=status.value("resident_workers").toInt(-1),usingPages=pages.value("large_page_workers").toInt(-1);
 const bool valid=connected&&pages.value("schema")=="PGN_CPU_MEMORY_PAGES_V1"&&resident>0&&usingPages>=0&&usingPages<=resident&&bytes.has_value();
 const bool active=valid&&usingPages==resident&&*bytes>0;
 const auto cause=pages.value("cause").toString();
 QString explanation=active?QString("All %1 resident workers confirm large-page memory in use (%2 bytes).").arg(resident).arg(pages.value("large_page_bytes_in_use").toString()):
  !connected?QString("CPU controller disconnected; current memory-page usage cannot be confirmed."):
  valid?QString("%1 of %2 resident workers use large-page memory.").arg(usingPages).arg(resident):
  QString("No worker large-page allocation is confirmed. The current CPU worker uses ordinary memory pages.");
 if(!active&&connected){
  if(cause=="no_task"||cause=="off_or_paused")explanation="No CPU worker is currently computing. Memory will be allocated when assigned work starts.";
  else if(cause=="missing_lock_memory_privilege")explanation="The worker token lacks Lock pages in memory. After granting the account right, sign out and sign in before restarting the worker with the required privileges.";
  else if(cause=="disabled")explanation="Large-page allocation is disabled in this CPU worker profile.";
  else if(cause=="stale_measurement")explanation="The last memory-page observation is stale; active large-page usage is unconfirmed.";
  else if(cause=="allocation_refused")explanation="The operating system refused large-page allocation; ordinary memory pages remain in use.";
  else if(cause=="awaiting_bitmap_use")explanation="The CPU worker is starting; large pages become active only after verified use of its calculation buffer.";
  else if(cause=="allocator_unsupported")explanation="This CPU worker does not support large-page allocation.";
  else if(cause.isEmpty())explanation+=" Windows requires an integrated allocator and the Lock pages in memory right; administrator launch alone does not supply a missing right.";
 }
 return {active,active?QString("HUGE PAGES ON"):QString("HUGE PAGES OFF"),explanation,QColor(active?"#69efc5":"#ff718b")};
}
inline QJsonObject cpuResourceRates(const QJsonObject& status,bool connected){
 const auto data=status.value("resource_rates").toObject();
 if(!connected||data.value("schema")!="PGN_CPU_RESOURCE_RATES_V1"||data.value("source")!="verified_closed_native_pieces"||data.value("session")!=status.value("session")||!data.value("session").isString()||data.value("positions")!=status.value("resource_completed_positions"))return {};
 return data;
}
inline bool cpuResourceSessionActive(const QJsonObject& status,bool connected){
 const auto rates=cpuResourceRates(status,connected);const auto speed=cpuMetricNumber(rates,"positions_per_second");
 return connected&&status.value("compute_started")==true&&status.value("enabled")==true&&status.value("resident_workers").toInt()>0&&status.value("resource_dispatch_threads").toInt()>0&&status.value("result_resident_policy")==true&&!status.value("budget_refused").toBool()&&status.value("error").toString().isEmpty()&&speed&&*speed>0;
}
inline QString cpuActivityCaption(const QJsonObject& status,bool connected,const QString& assignmentStatus={}){
 if(!connected)return "DISCONNECTED · activity unconfirmed";
 if(status.value("normal_cycle_mode")=="qualification"){
  const auto state=status.value("state").toString();
  if(cpuResourceSessionActive(status,connected))return "QUALIFICATION · RUNNING · no network credit";
  if(status.value("qualification_reason_code")=="unsupported_domain")return "CPU INACTIVE · unsupported ticket domain";
  if(state=="RUNNING")return status.value("compute_started")==false?"QUALIFICATION · waiting for CPU work":"QUALIFICATION · RUNNING · no network credit";
  if(state=="BUDGET_PAUSED"||state=="DRAINING_BUDGET")return "QUALIFICATION · CPU load protection";
  if(status.value("result_ready")==true)return "QUALIFICATION · COMPUTED · no network credit";
  if(state.startsWith("DRAINING"))return "QUALIFICATION · saving CPU frontier";
  if(status.value("resource_selected")==true)return "QUALIFICATION · waiting normal cycle";
  return "OFF · CPU qualification inactive";
 }

 const auto state=status.value("state").toString();const QString role=status.value("validation_only")==true?"LOCAL CHECK":"AUDIT";
 if(state=="RUNNING")return role+(status.value("compute_started")==false?" · waiting for CPU work":" · RUNNING");
 if(state=="BUDGET_PAUSED")return role+" · PAUSED · CPU load protection";
 if(state=="DRAINING_BUDGET")return role+" · PAUSING · CPU load protection";
 if(state=="WAITING_AUDIT"||state=="IDLE"){
  if(status.value("enabled")!=true)return "OFF · no calculation";
  return assignmentStatus.isEmpty()?QString("READY · waiting for an assigned audit"):"AUDIT · "+assignmentStatus;
 }
 if(state=="DRAINING_CONTROL"||state=="RECONFIGURING")return "CHANGING THREAD BUDGET";
 if(state=="DRAINING_CLOSE"||state=="STOPPING")return "STOPPING · saving progress";
 if(status.value("result_ready")==true)return role+" · COMPUTATION COMPLETE";
 if(state=="PAUSED"||state=="PAUSED_RECOVERED"||state=="OFF"||state=="CLOSED")return role+" · PAUSED · progress saved";
 return role+" · "+(state.isEmpty()?QString("STARTING"):state);
}
inline QJsonObject cpuMiningRates(const QJsonObject& status,bool connected){
 const auto data=status.value("mining_rates").toObject();
 if(!connected||data.value("schema")!="PGN_CPU_MINING_RATES_V1"||data.value("source")!="durable_completed_counts"||!data.contains("request_sha256")||data.value("request_sha256")!=status.value("request_sha256")||data.value("positions")!=status.value("durable_completed_positions"))return {};
 return data;
}
inline QJsonObject cpuVisibleRates(const QJsonObject& status,bool connected){
 const auto resource=cpuResourceRates(status,connected);return resource.isEmpty()?cpuMiningRates(status,connected):resource;
}
inline QVector<CpuCardMetric> cpuCardMetrics(const QJsonObject& status,bool connected,bool expanded,const QColor& accent){
 const auto telemetry=status.value("cpu_telemetry").toObject();const bool typed=telemetry.value("schema")=="PGN_CPU_TELEMETRY_V1";
 const auto busy=connected&&typed?cpuMetricNumber(telemetry,"worker_busy_percent"):std::nullopt;
 const auto load=connected?cpuMetricNumber(status,"machine_load_percent"):std::nullopt;
 const auto memoryTotal=connected&&typed?cpuMetricInteger(telemetry,"host_memory_total_bytes"):std::nullopt;
 const auto memoryUsed=connected&&typed?cpuMetricInteger(telemetry,"host_memory_used_bytes"):std::nullopt;
 const auto engineMemory=connected&&typed?cpuMetricInteger(telemetry,"engine_working_set_bytes"):std::nullopt;
 auto rate=connected?cpuMetricNumber(status,"rate_positions_per_second"):std::nullopt;
 if(connected&&status.value("resident_result_handoff")==true&&status.value("enabled")==true&&status.value("resident_workers").toInt()>0&&cpuMetricInteger(status,"resource_completed_positions"))rate=cpuMetricNumber(status,"resource_positions_per_second");
 const auto state=status.value("state").toString();const int resident=status.value("resident_workers").toInt(-1);
 const bool stopped=connected&&resident==0&&QSet<QString>{"IDLE","OFF","PAUSED","PAUSED_RECOVERED","WAITING_AUDIT","WAITING_NORMAL_CYCLE","BUDGET_PAUSED","CLOSED","AUDIT_COMPUTED_SOURCE_PROOF_PENDING","CPU_WORK_SOURCE_PROOF_PENDING","PROBE_COMPLETE"}.contains(state);
 const bool idle=connected&&status.value("compute_started")==false;
 if(stopped||idle)rate=0.;
 const auto mining=cpuVisibleRates(status,connected);
 auto tests=cpuMetricNumber(mining,"tests_per_second"),gaps=cpuMetricNumber(mining,"gaps_per_second");
 if((stopped||idle)&&!mining.isEmpty()){tests=0.;gaps=0.;}
 const auto percent=[](std::optional<double> value){return value?QString::number(*value,'f',0)+"%":QString("—");};
 const auto memoryFraction=memoryUsed&&memoryTotal&&*memoryTotal>0?std::optional<double>(*memoryUsed/ *memoryTotal):std::nullopt;
 const QString rateText=rate?compactCardMetricCaption(rate):(connected&&state=="RUNNING"?QString("START"):QString("—"));
 const CpuCardMetric activity{busy,100.,percent(busy),"CPU BUSY","CPU time used by owned native workers / selected thread budget. Host load remains separate.",accent};
 const CpuCardMetric throughput{rate,0.,rateText,"pos/s","Mean of closed native position counts; confirmed idle/pause is zero. Not a primality-test count.",QColor("#b58cff")};
 const CpuCardMetric testRate{tests,0.,compactCardMetricCaption(tests),"tests/s","Mean native candidate tests over the same active-time counter window as gap statistics. Missing producer samples stay unavailable.",QColor("#46c8ee")};
 const CpuCardMetric gapRate{gaps,0.,compactCardMetricCaption(gaps),"gaps/s","Mean retained gap counts above the assigned minimum, over up to 60 seconds of active computation. Display counters do not imply network acceptance.",QColor("#69efc5")};
 if(!expanded)return {activity,throughput,testRate};
 return {activity,{load,100.,percent(load),"HOST LOAD","Total machine CPU utilization, including other applications.",accent},
  {memoryFraction,1.,memoryFraction?QString::number(*memoryFraction*100.,'f',0)+"%":QString("—"),"HOST RAM","Physical RAM used by all applications / total physical RAM.",QColor("#62cfff")},
  {engineMemory,memoryTotal.value_or(0),engineMemory?QString::number(*engineMemory/1048576.,'f',*engineMemory<104857600?1:0):QString("—"),"RAM MiB","Sum of controller and worker working sets; shared pages may be counted more than once.",QColor("#69efc5")},
  throughput,testRate,gapRate};
}
}

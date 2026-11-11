#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QRegularExpression>
#include <QWidget>

namespace pgn::ui {
// Hardware telemetry and Core connectivity do not imply an attached worker.
inline void presentMiningStartup(QWidget* canvas,QLabel* notice,const QString& stage,const QString& detail){
 canvas->setProperty("pgn_local_runtime_stage",stage);
 canvas->setProperty("pgn_local_runtime_status",detail.left(600));
 const bool ready=stage=="ready";
 notice->setText(ready?QString{}:QString("Mining · %1").arg(detail.left(600)));
 notice->setVisible(!ready);
}
inline QString diagnosticStatusText(QString text){
 text=text.left(600);
 text.replace(QRegularExpression("(?i)(?:authorization|cookie|password|secret|private[ _-]?key)\\s*[:=][^\\r\\n]*"),"<redacted>");
 text.replace(QRegularExpression("(?i)[a-z]:[\\\\/][^\\r\\n\\\"<>|]*"),"<local path>");
 text.replace(QRegularExpression("/(?:home|Users|tmp|var)/[^\\r\\n\\\"<>|]*"),"<local path>");
 return text;
}
inline QJsonObject diagnosticPublicationMaintenance(const QJsonObject& source){
 QJsonObject safe;
 for(const auto* lane:{"owner","auditors"}){
  const auto row=source.value(lane).toObject();if(row.isEmpty())continue;
  QJsonObject value;
  if(row.value("active").isBool())value.insert("active",row.value("active"));
  for(const auto* key:{"elapsed_seconds","next_in_seconds"})if(row.value(key).isDouble()){
   const double n=row.value(key).toDouble();if(n>=0&&n<=31536000)value.insert(key,n);
  }
  if(row.value("error").isString())value.insert("error",diagnosticStatusText(row.value("error").toString()));
  else if(row.value("error").isNull())value.insert("error",QJsonValue{});
  safe.insert(lane,value);
 }
 return safe;
}
inline QJsonObject diagnosticMiningStartup(QWidget* canvas){
 // Explicit allowlist: never serialize the wallet connection or profile.
 const auto process=canvas->property("pgn_worker_process_state").toJsonObject();
 auto stage=canvas->property("pgn_local_runtime_stage").toString();
 auto detail=canvas->property("pgn_local_runtime_status").toString();
 // Preparation is a one-time inventory state, not the current work state.
 // Use observed controller facts, never infer GPU activity from an ON request.
 if(stage=="ready"){
  const auto phase=process.value("phase").toString();
  if(process.value("role")=="network"&&process.value("running").toBool()&&!phase.isEmpty()){
   stage="network";detail=phase;const auto notice=process.value("notice").toString();if(!notice.isEmpty())detail+=" · "+notice;
  }else detail=canvas->property("pgn_network_desired_on").toBool()?"ON requested; waiting for network controller":"Prepared; computation is OFF";
 }
 QJsonObject value{{"schema","PGN_MINING_STARTUP_STATUS_V1"},
  {"stage",stage},
  {"detail",diagnosticStatusText(detail)},
  {"gpu_profiles",canvas->property("pgn_local_gpu_profiles").toInt()},
  {"desired_on",canvas->property("pgn_network_desired_on").toBool()},
  {"stream_state",canvas->property("pgn_worker_stream_state").toInt()},
  {"stream_packets",canvas->property("pgn_worker_stream_packets").toLongLong()},
  {"stream_phase",canvas->property("pgn_worker_stream_phase").toString().left(80)},
  {"stream_error",diagnosticStatusText(canvas->property("pgn_worker_stream_error").toString())},
  {"stream_notice",diagnosticStatusText(canvas->property("pgn_worker_stream_notice").toString())},
  {"reconnect_attempts",canvas->property("pgn_worker_reconnect_count").toInt()},
  {"reconnect_delay_ms",canvas->property("pgn_worker_reconnect_delay_ms").toInt()},
  {"process",canvas->property("pgn_worker_process_state").toJsonObject()},
  {"events",canvas->property("pgn_worker_process_events").toJsonArray()}};
 QJsonArray unsupported;
 for(const auto& item:canvas->property("pgn_local_runtime_unsupported").toJsonArray()){
  if(unsupported.size()>=32)break;
  const auto row=item.toObject();unsupported.append(QJsonObject{{"id",row.value("id").toString().left(64)},
   {"reason",diagnosticStatusText(row.value("reason").toString())}});
 }
 value.insert("unsupported_devices",unsupported);
 const auto frame=canvas->property("pgn_network_ticket_frame").toJsonObject();
 const auto maintenance=diagnosticPublicationMaintenance(frame.value("audit_service").toObject().value("publication_maintenance").toObject());
 if(!maintenance.isEmpty())value.insert("publication_maintenance",maintenance);
 return value;
}
}

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
inline QJsonObject diagnosticMiningStartup(QWidget* canvas){
 // Explicit allowlist: never serialize the wallet connection or profile.
 QJsonObject value{{"schema","PGN_MINING_STARTUP_STATUS_V1"},
  {"stage",canvas->property("pgn_local_runtime_stage").toString()},
  {"detail",diagnosticStatusText(canvas->property("pgn_local_runtime_status").toString())},
  {"gpu_profiles",canvas->property("pgn_local_gpu_profiles").toInt()},
  {"desired_on",canvas->property("pgn_network_desired_on").toBool()},
  {"stream_state",canvas->property("pgn_worker_stream_state").toInt()},
  {"stream_packets",canvas->property("pgn_worker_stream_packets").toLongLong()},
  {"stream_phase",canvas->property("pgn_worker_stream_phase").toString().left(80)},
  {"stream_error",diagnosticStatusText(canvas->property("pgn_worker_stream_error").toString())}};
 QJsonArray unsupported;
 for(const auto& item:canvas->property("pgn_local_runtime_unsupported").toJsonArray()){
  if(unsupported.size()>=32)break;
  const auto row=item.toObject();unsupported.append(QJsonObject{{"id",row.value("id").toString().left(64)},
   {"reason",diagnosticStatusText(row.value("reason").toString())}});
 }
 value.insert("unsupported_devices",unsupported);return value;
}
}

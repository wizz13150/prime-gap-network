#pragma once
#include "ui/mining_startup_status.h"
#include <QDateTime>
#include <QFileInfo>
#include <QProcess>
#include <functional>

namespace pgn::ui {
// Collected from owned process signals from launch onward, independently of
// manual recording. No command line, wallet context or arbitrary files copied.
class MiningProcessDiagnostics final:public QObject {
 QWidget* canvas_;QProcess* process_;QString device_,role_{"observer"},stderr_;
 QJsonObject state_;
 void event(const QString& kind,QString detail={}){
  state_.insert("role",role_);state_.insert("device",device_);
  canvas_->setProperty("pgn_worker_process_state",state_);
  auto events=canvas_->property("pgn_worker_process_events").toJsonArray();
  events.append(QJsonObject{{"at_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
   {"device",device_},{"role",role_},{"kind",kind},{"detail",diagnosticStatusText(detail)}});
  while(events.size()>24)events.removeFirst();canvas_->setProperty("pgn_worker_process_events",events);
 }
public:
 std::function<void(const QString&)> failure;
 MiningProcessDiagnostics(QWidget* canvas,QProcess* process,QString device):QObject(process),canvas_(canvas),process_(process),device_(std::move(device)){
  connect(process_,&QProcess::started,this,[this]{
   state_.insert("pid",qint64(process_->processId()));state_.insert("running",true);event("started");
  });
  connect(process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
   const auto reason=diagnosticStatusText(process_->errorString());
   if(role_=="observer"&&error==QProcess::Crashed){event("observer_replaced");return;}
   state_.insert("process_error",int(error));state_.insert("last_failure",reason);event("process_error",reason);
   canvas_->setProperty("pgn_worker_stream_error",reason);if(failure)failure(reason);
  });
  connect(process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
   const auto finalError=process_->readAllStandardError();if(!finalError.isEmpty())standardError(finalError);
   state_.insert("running",false);state_.insert("exit_code",code);state_.insert("exit_kind",status==QProcess::NormalExit?"normal":"crash");
   event("exited",QString("Exit %1 (%2)").arg(code).arg(status==QProcess::NormalExit?"normal":"crash"));
   if(role_=="network"&&(code!=0||status!=QProcess::NormalExit)){
    const auto reason=QString("Mining controller stopped: exit %1 (%2). %3").arg(code).arg(status==QProcess::NormalExit?"normal":"crash",state_.value("last_failure").toString());
    canvas_->setProperty("pgn_worker_stream_error",reason);if(failure)failure(reason);
   }
  });
 }
 void launching(const QString& role){role_=role;stderr_.clear();for(const auto* key:{"exit_code","exit_kind","process_error","phase","notice","pid"})state_.remove(key);state_.insert("attempt",state_.value("attempt").toInt()+1);state_.insert("running",false);state_.insert("entry_program",QFileInfo(process_->program()).fileName());state_.insert("entry_present",QFileInfo::exists(process_->program()));state_.insert("stderr_tail",QString{});event("launch_requested");}
 void intent(bool on){event(on?"on_requested":"off_requested");}
 void standardError(const QByteArray& bytes){
  // Sanitize before retaining. Keep enough context for Python/import, loader,
  // CUDA, file/space and network failures, even when recording starts later.
  stderr_=(stderr_+QString::fromUtf8(bytes)).right(4096);
  QStringList safe;for(const auto& line:stderr_.split('\n'))safe.append(diagnosticStatusText(line));
  const auto tail=safe.join('\n').right(1800);state_.insert("stderr_tail",tail);state_.insert("last_failure",tail);
  canvas_->setProperty("pgn_worker_stream_error",tail);event("stderr",safe.isEmpty()?QString{}:safe.last());
 }
 void packet(const QJsonObject& packet){
  const auto phase=packet.value("phase").toString();if(phase.isEmpty())return;
  const auto fault=packet.value("failure").toObject();
  if(fault.value("schema")=="PGN_RUNTIME_FAILURE_V1"){
   QJsonObject safe{{"schema","PGN_RUNTIME_FAILURE_V1"},{"at_utc",diagnosticStatusText(fault.value("at_utc").toString())},{"step",diagnosticStatusText(fault.value("step").toString())}};QJsonArray causes;
   for(const auto& entry:fault.value("causes").toArray()){
    if(causes.size()>=3)break;const auto cause=entry.toObject();QJsonObject item;for(const auto* key:{"error_type","resource","module","summary"})item.insert(key,diagnosticStatusText(cause.value(key).toString()));
    for(const auto* key:{"os_code","os_win_code"})if(cause.value(key).isDouble())item.insert(key,cause.value(key));QJsonArray frames;
    for(const auto& f:cause.value("frames").toArray()){if(frames.size()>=6)break;const auto frame=f.toObject();frames.append(QJsonObject{{"file",diagnosticStatusText(frame.value("file").toString())},{"line",frame.value("line").toInt()},{"function",diagnosticStatusText(frame.value("function").toString())}});}item.insert("frames",frames);causes.append(item);
   }safe.insert("causes",causes);state_.insert("failure_context",safe);
  }
  const auto message=diagnosticStatusText(packet.value("message").toString());
  if(phase!=state_.value("phase").toString())event("phase",phase+": "+message);
  state_.insert("phase",phase);state_.insert("notice",message);
  if(phase=="Needs attention"||phase=="FAILED")state_.insert("last_failure",message);
  canvas_->setProperty("pgn_worker_process_state",state_);
 }
};
}

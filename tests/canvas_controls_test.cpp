#include "ui/mining_canvas.h"
#include "ui/mining_control_binding.h"
#include "telemetry.h"
#include "booking/booking_model.h"
#include <QApplication>
#include <QMouseEvent>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <QTemporaryDir>
#include <QSettings>
#include <QPainter>
#include <QUuid>
#include <memory>
#include <iostream>
int main(int argc,char** argv){
 QApplication app(argc,argv);QCoreApplication::setOrganizationName("PGN-QA");QCoreApplication::setApplicationName("click-control-test");
 QTemporaryDir isolated;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,isolated.path());
 {
  std::unique_ptr<QWidget> fresh(pgn::ui::createMiningCanvas());pgn::ui::configureMiningDashboard(fresh.get());
  pgn::booking::BookingModel model(isolated.path());pgn::ui::bindBookingModel(fresh.get(),&model);fresh->resize(1040,180);
  const QString gpu="GPU-startup-control-fixture";FrontierTelemetrySnapshot source;source.gpuUuid=gpu;source.gpuName="Startup control fixture";
  source.sourceMode="live";source.observedUtc=QDateTime::currentDateTimeUtc();
  pgn::ui::presentDevice(fresh.get(),source);
  int commands=0;QString pending;bool requested=false;
  pgn::ui::bindMiningControls(fresh.get(),[&](const QString&,const QString& id,bool on){++commands;pending=id;requested=on;});
  const auto paint=[&]{QImage image(fresh->size(),QImage::Format_ARGB32_Premultiplied);QPainter painter(&image);fresh->render(&painter);};
  paint();if(fresh->property("pgn_total_gpu_state").toString()!="unknown"||commands){std::cerr<<"FAIL unobserved startup falsely claims GPU ON/OFF\n";return 20;}
  QJsonObject inventory{{"host","QA"},{"model","Startup CPU fixture"},{"maximum_workers",4}};
  QJsonObject status{{"state","IDLE"},{"enabled",false},{"requested_threads",2},{"resident_workers",0},{"active_workers",0}};
  source.raw={{"pgn_device_enabled",false},{"legacy_miner_bound",false}};
  pgn::ui::presentDevice(fresh.get(),source);pgn::ui::presentCpuController(fresh.get(),inventory,status,true);paint();
  if(fresh->property("pgn_total_gpu_state").toString()!="off"||fresh->property("pgn_total_cpu_state").toString()!="off"||commands){std::cerr<<"FAIL first confirmed OFF frame has stale aggregate ON\n";return 21;}
  source.raw["pgn_device_enabled"]=true;source.raw["legacy_miner_bound"]=true;source.observedUtc=source.observedUtc.addMSecs(1);
  status["enabled"]=true;pgn::ui::presentDevice(fresh.get(),source);pgn::ui::presentCpuController(fresh.get(),inventory,status,true);paint();
  if(fresh->property("pgn_total_gpu_state").toString()!="on"||fresh->property("pgn_total_cpu_state").toString()!="on"||commands){std::cerr<<"FAIL first confirmed ON frame has stale aggregate\n";return 22;}
  const auto rect=pgn::ui::miningToggleRect(fresh.get(),gpu);if(rect.isEmpty())return 23;
  QMouseEvent stop(QEvent::MouseButtonPress,rect.center(),fresh->mapToGlobal(rect.center().toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(fresh.get(),&stop);
  if(commands!=1||requested)return 24;
  pgn::ui::resolveDeviceControl(fresh.get(),gpu,pending,true,"Fixture OFF acknowledgement");paint();
  if(fresh->property("pgn_total_gpu_state").toString()!="off"){std::cerr<<"FAIL repaint of older ON telemetry undoes newer OFF acknowledgement\n";return 25;}
 }
 {
  std::unique_ptr<QWidget> local(pgn::ui::createMiningCanvas());pgn::ui::configureMiningDashboard(local.get());local->resize(1380,220);
  const QString a="GPU-local-a",b="GPU-local-b";QStringList delivered;
  pgn::ui::bindMiningControls(local.get(),[&](const QString& device,const QString&,bool on){if(on)delivered.append(device);});
  FrontierTelemetrySnapshot sample;sample.gpuUuid=a;sample.gpuName="Local GPU A";sample.sourceMode="live";sample.observedUtc=QDateTime::currentDateTimeUtc();sample.raw={{"pgn_device_enabled",false},{"legacy_miner_bound",false}};
  pgn::ui::presentDevice(local.get(),sample);sample.gpuUuid=b;sample.gpuName="Local GPU B";pgn::ui::presentDevice(local.get(),sample,true);
  const auto paint=[&]{QImage image(local->size(),QImage::Format_ARGB32_Premultiplied);QPainter painter(&image);local->render(&painter);};
  local->setProperty("pgn_source_only_control",true);paint();
  if(!pgn::ui::miningToggleRect(local.get(),a).isEmpty()){std::cerr<<"FAIL unbound device unexpectedly has control\n";return 26;}
  // Same local worker registration as main.cpp; no SSH/hardware endpoint.
  pgn::ui::registerMiningSourceDevice(local.get(),a);pgn::ui::registerMiningSourceDevice(local.get(),b);paint();
  for(const auto& device:{a,b}){
   const auto rect=pgn::ui::miningToggleRect(local.get(),device);if(rect.isEmpty()){std::cerr<<"FAIL registered local GPU has an inert OFF pill\n";return 27;}
   QMouseEvent press(QEvent::MouseButtonPress,rect.center(),local->mapToGlobal(rect.center().toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(local.get(),&press);
  }
  if(delivered!=QStringList{a,b}){std::cerr<<"FAIL local GPU clicks did not reach the matching controllers\n";return 28;}
 }
 std::unique_ptr<QWidget> canvas(pgn::ui::createMiningCanvas());canvas->resize(1380,900);
 const QString id="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
 FrontierTelemetrySnapshot lane;lane.gpuUuid=id;lane.gpuName="NVIDIA GeForce RTX3090";lane.runId="qa-live";lane.sourceMode="live";
 // Keep both samples inside the same displayed decimal bin and with the
 // same rounding marker. Exact100G ->100G+1 correctly changes to approximate
 //100G and is a semantic text change, not a render-clock flicker.
 lane.authoritative=true;lane.observedUtc=QDateTime::currentDateTimeUtc();lane.state="running";lane.producerStatus="running";lane.e2ePositionsPerSecond=1e11+1.;lane.powmPerSecond=1e8+1.;
 lane.raw.insert("legacy_miner_bound",true);pgn::ui::presentDevice(canvas.get(),lane);
 int calls=0;bool requested=true;QString request;bool canonicalRequests=true;QString previousRequest;
 pgn::ui::bindMiningControls(canvas.get(),[&](const QString& device,const QString& r,bool enabled){if(device==id){
  const QUuid parsed(r);canonicalRequests=canonicalRequests&&!parsed.isNull()&&parsed.toString(QUuid::WithoutBraces)==r&&r!=previousRequest;
  previousRequest=r;++calls;requested=enabled;request=r;}});
 canvas->show();app.processEvents();canvas->repaint();
 auto hasProcessed=[&]{const auto descriptions=pgn::ui::miningTooltips(canvas.get()).join('\n');
  return descriptions.contains("Processed gaps/s:")||descriptions.contains("Owned gaps above the GPU minimum-gap gate");};
 if(hasProcessed()){std::cerr<<"FAIL missing processed-gap counter left an unavailable gauge visible\n";return 2;}
 lane.raw.insert("pgn_processed_gaps_per_second",0.0);pgn::ui::presentDevice(canvas.get(),lane);canvas->repaint();
 if(!hasProcessed()){std::cerr<<"FAIL valid processed-gap raw zero hidden\n";return 3;}
 lane.raw.remove("pgn_processed_gaps_per_second");pgn::ui::presentDevice(canvas.get(),lane);canvas->repaint();
 if(hasProcessed()){std::cerr<<"FAIL obsolete processed-gap gauge/tooltip retained after counter absence\n";return 4;}
 lane.engineId="pgn-native-walk";
 lane.raw.insert("pgn_processed_gaps_per_second",42.0);
 lane.raw.insert("pgn_retained_gaps_per_billion_positions",0.5);
 pgn::ui::presentDevice(canvas.get(),lane);canvas->repaint();
 if(!hasProcessed()){std::cerr<<"FAIL requested native Gaps/sec counter was removed with redundant kept/sec\n";return 7;}
 const auto nativeDescriptions=pgn::ui::miningTooltips(canvas.get()).join('\n');
 if(!nativeDescriptions.contains("Not all short prime gaps")&&!nativeDescriptions.contains("NOT every small prime gap")){
  std::cerr<<"FAIL restored gap counter lost its actual producer scope\n";return 9;}
 lane.raw.insert("pgn_processed_gaps_per_second",0.0);pgn::ui::presentDevice(canvas.get(),lane);canvas->repaint();
 if(!hasProcessed()){std::cerr<<"FAIL valid native gap rate zero hidden\n";return 10;}
 lane.raw.remove("pgn_processed_gaps_per_second");pgn::ui::presentDevice(canvas.get(),lane);canvas->repaint();
 if(hasProcessed()){std::cerr<<"FAIL missing native gap rate fabricated from retained output\n";return 11;}
 if(!nativeDescriptions.contains("Input/output yield:")&&!nativeDescriptions.contains("Retained output per billion input positions")){
  std::cerr<<"FAIL removing misleading input count also removed the valid input/output ratio\n";return 8;}
 lane.engineId.clear();lane.raw.remove("pgn_processed_gaps_per_second");
 pgn::ui::presentDevice(canvas.get(),lane);canvas->repaint();
 canvas->setProperty("pgn_values_audit_enabled",true);
 const auto firstImage=canvas->grab().toImage();
 const auto valueRects=canvas->property("pgn_values_audit_rects").toJsonArray();
 if(valueRects.isEmpty())return 5;
 // A render-clock step cannot blank/retype unchanged instrument values.
 for(int frame=0;frame<4;++frame){QThread::msleep(130);app.processEvents();
  lane.e2ePositionsPerSecond=*lane.e2ePositionsPerSecond+1.;lane.powmPerSecond=*lane.powmPerSecond+1.;
  pgn::ui::presentDevice(canvas.get(),lane);const auto current=canvas->grab().toImage();
  const auto currentRects=canvas->property("pgn_values_audit_rects").toJsonArray();
  if(currentRects.size()!=valueRects.size()){std::cerr<<"FAIL stable-value fixture changed gauge count\n";return 29;}
  for(int i=0;i<valueRects.size();++i)if(valueRects[i].toObject()["value"]!=currentRects[i].toObject()["value"]){std::cerr<<"FAIL stable-value fixture crossed a displayed decimal/rounding boundary\n";return 30;}
  for(const auto entry:valueRects){const auto value=entry.toObject();const auto dpr=firstImage.devicePixelRatio();
   const QRect rectangle(qRound(value["x"].toDouble()*dpr),qRound(value["y"].toDouble()*dpr),qRound(value["w"].toDouble()*dpr),qRound(value["h"].toDouble()*dpr));
   if(firstImage.copy(rectangle)!=current.copy(rectangle)){std::cerr<<"FAIL unchanged value pixels flicker with render clock\n";return 6;}}
 }
 canvas->setProperty("pgn_values_audit_enabled",false);
 auto click=[&]{auto rect=pgn::ui::miningToggleRect(canvas.get(),id);if(rect.isEmpty())return false;
  QMouseEvent e(QEvent::MouseButtonPress,rect.center(),canvas->mapToGlobal(rect.center().toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
  QApplication::sendEvent(canvas.get(),&e);return true;};
 if(!click()||calls!=1||requested){std::cerr<<"FAIL visible ON pill did not request owned STOP\n";return 1;}
 pgn::ui::resolveDeviceControl(canvas.get(),id,request,true,"QA stop acknowledged");
 lane.state="stopped";lane.terminalSealed=true;lane.raw.insert("legacy_miner_bound",false);pgn::ui::presentDevice(canvas.get(),lane);canvas->repaint();
 if(!click()||calls!=2||!requested){std::cerr<<"FAIL visible OFF pill did not request owned START\n";return 1;}
 if(!canonicalRequests){std::cerr<<"FAIL actual canvas emits IDs rejected by native initial-start admission\n";return 10;}
 std::cout<<"PASS actual painted ON/OFF hit targets and callback directions\n";return 0;
}

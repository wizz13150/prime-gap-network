#include "ui/mining_canvas.h"
#include "booking/booking_model.h"
#include <QDir>
#include <QJsonDocument>
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QPixmap>
#include <QMouseEvent>
#include <iostream>
int main(int argc,char** argv){
 QApplication app(argc,argv);QTemporaryDir temporary;QCoreApplication::setOrganizationName("PGN-RENDER-TEST");QCoreApplication::setApplicationName("cpu-cards");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
 auto* canvas=pgn::ui::createMiningCanvas();pgn::ui::configureMiningDashboard(canvas);canvas->setProperty("pgn_card_layout_audit",true);auto* model=new pgn::booking::BookingModel(temporary.path(),canvas);pgn::ui::bindBookingModel(canvas,model);canvas->resize(1040,180);
 QJsonObject inv{{"host","QA"},{"model","Long CPU model name for narrow and expanded cards"},{"processor_group",0},{"maximum_workers",9},{"logical_cpus_available",12},{"topology",QJsonArray{QJsonObject{{"cpus",QJsonArray{0,1}}},QJsonObject{{"cpus",QJsonArray{2,3}}}}}};
 QJsonObject metrics{{"schema","PGN_CPU_TELEMETRY_V1"},{"worker_busy_percent",96.},{"host_memory_total_bytes","16000000000"},{"host_memory_used_bytes","8000000000"},{"engine_working_set_bytes","104857600"}};
 QJsonObject state{{"state","RUNNING"},{"enabled",true},{"compute_started",true},{"active_workers",6},{"threads_active",6},{"resident_workers",6},{"requested_threads",6},{"rate_positions_per_second",2.1e9},{"machine_load_percent",64.},{"durable_completed_positions","8594900000000"},{"completed_positions","8594900000000"},{"total_positions","10000000000000"},{"request_sha256",QString(64,'a')},{"cpu_telemetry",metrics},{"network_accepted",false}};
 state.insert("mining_rates",QJsonObject{{"schema","PGN_CPU_MINING_RATES_V1"},{"source","durable_completed_counts"},{"request_sha256",QString(64,'a')},{"positions","8594900000000"},{"tests_per_second",6300000.},{"gaps_per_second",4.2},{"gaps_per_billion_positions",2.}});
 pgn::ui::presentCpuController(canvas,inv,state,true);canvas->show();app.processEvents();QPixmap initial(canvas->size());canvas->render(&initial);
 int checked=0;
 for(bool available:{true,false})for(bool expanded:{false,true}){
  auto layout=canvas->property("pgn_card_layout").toJsonArray();QJsonObject card;
  for(const auto& row:layout)if(row.toObject().value("kind")=="CPU")card=row.toObject();
  if(card.isEmpty()){std::cerr<<"layout="<<QJsonDocument(layout).toJson(QJsonDocument::Compact).constData()<<" metrics="<<QJsonDocument(canvas->property("pgn_cpu_card_metrics").toJsonObject()).toJson(QJsonDocument::Compact).constData()<<" size="<<canvas->width()<<"x"<<canvas->height();return 2;}
  if(card.value("expanded").toBool()!=expanded){
   auto r=card.value("rect").toArray();QPointF at(r[0].toDouble()+r[2].toDouble()-18.5,r[1].toDouble()+15.5);
   QMouseEvent press(QEvent::MouseButtonPress,at,canvas->mapToGlobal(at.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&press);
   QMouseEvent release(QEvent::MouseButtonRelease,at,canvas->mapToGlobal(at.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(canvas,&release);app.processEvents();
  }
  for(const auto& phase:QStringList{"RUNNING","BUDGET_PAUSED","WAITING_AUDIT"}){
   for(int cardWidth:{640,1040,1920}){canvas->resize(cardWidth,canvas->height());
   auto sample=state;sample.insert("state",phase);
   if(phase!="RUNNING"){sample.insert("resident_workers",0);sample.insert("active_workers",0);sample.insert("threads_active",0);auto idleMetrics=metrics;idleMetrics.insert("worker_busy_percent",0.);sample.insert("cpu_telemetry",idleMetrics);sample.insert("compute_started",false);sample.insert("rate_positions_per_second",0.);}
   pgn::ui::presentCpuController(canvas,inv,sample,available);app.processEvents();QPixmap image(canvas->size());canvas->render(&image);
   const auto rows=canvas->property("pgn_cpu_card_metrics").toJsonObject().value("CPU/QA/0").toArray();if(rows.size()!=(expanded?7:3))return 3;
   if(rows[expanded?4:1].toObject().value("label")!="pos/s"||rows[expanded?5:2].toObject().value("label")!="tests/s")return 10;
   for(const auto& row:rows)if(row.toObject().value("label")=="gaps/Gpos")return 11;
   for(const auto& value:canvas->property("pgn_card_layout").toJsonArray()){
    auto row=value.toObject();if(row.value("kind")!="CPU")continue;
    if(row.value("header_end").toDouble()>=row.value("gauge_top").toDouble()||row.value("gauge_bottom").toDouble()>=row.value("footer_start").toDouble())return 4;
   }
   const auto rect=[](const QJsonValue& value){const auto a=value.toArray();return QRectF(a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble());};
   const auto footer=canvas->property("pgn_cpu_footer_geometry").toJsonObject().value("CPU/QA/0").toObject();
   if(footer.isEmpty()){std::cerr<<"CPU footer geometry missing";return 6;}
   const auto budget=rect(footer.value("budget")),pages=rect(footer.value("pages")),slider=rect(footer.value("slider"));
   if(budget.intersects(pages)||budget.intersects(slider)||pages.intersects(slider)){std::cerr<<"CPU footer text/control overlap";return 7;}
   QRectF pod;
   for(const auto& value:canvas->property("pgn_card_layout").toJsonArray())if(value.toObject().value("kind")=="CPU")pod=rect(value.toObject().value("rect"));
   int measuredLabels=0;
   for(const auto& value:canvas->property("pgn_gauge_label_rects").toJsonArray()){
    const auto label=rect(value.toObject().value("rect"));if(!pod.contains(label.center()))continue;++measuredLabels;
    if(label.intersects(budget)||label.intersects(pages)||label.intersects(slider)||!pod.contains(label)){std::cerr<<"Actual CPU gauge glyphs escape or overlap footer";return 8;}
   }
   if(measuredLabels!=(expanded?7:3)){std::cerr<<"Missing actual CPU gauge label bounds";return 9;}
   if(argc>1){QDir().mkpath(QString::fromLocal8Bit(argv[1]));if(!image.save(QString::fromLocal8Bit(argv[1])+QString("/cpu-%1-%2-%3-%4.png").arg(expanded?"expanded":"compact",phase,available?"ready":"unavailable").arg(cardWidth)))return 5;}
   ++checked;
   }
  }
 }
 delete canvas;std::cout<<"PASS "<<checked<<" actual CPU card renders: compact/expanded, controller ready/unavailable, three widths, measured gauge glyph/footer/slider separation, no controller or scientific work launched\n";
}

#include "ui/mining_canvas.h"
#include "telemetry.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QTimeZone>
#include <iostream>
#include <memory>
int main(int argc,char** argv){
 QApplication app(argc,argv);std::unique_ptr<QWidget> canvas(pgn::ui::createMiningCanvas());canvas->resize(1380,850);
 const auto now=QDateTime::currentDateTimeUtc();QElapsedTimer timer;timer.start();
 pgn::ui::beginHistoryBatch(canvas.get());
 for(int i=0;i<82009;++i){FrontierGapEvent event;event.runId="qa-history";event.domainId="pgn-renderer-test";
  event.gpuUuid="GPU-qa";event.sequence=i+1;event.observedUtc=now.addMSecs(-i*20);
  event.gap=QString::number(800+(i%250)*2);event.merit=QString::number(17.0+(i%150)/15.0,'g',12);
  event.endpointLeft=QString::number(1000000000ULL+i*2000ULL);event.endpointRight=QString::number(1000000000ULL+i*2000ULL+event.gap.toULongLong());
  event.candidateId=event.endpointLeft+":"+event.endpointRight;event.proofState="prp_pending";
  pgn::ui::presentGap(canvas.get(),event);
 }
 pgn::ui::endHistoryBatch(canvas.get());const auto ingest=timer.nsecsElapsed();
 FrontierTelemetrySnapshot lane;lane.runId="qa-history";lane.gpuUuid="GPU-qa";
 lane.engineId="pgn-native-walk";lane.authoritative=true;lane.state="running";
 lane.observedUtc=now;lane.raw.insert("pgn_process_running",true);
 pgn::ui::presentDevice(canvas.get(),lane);
 QImage image(canvas->size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
 timer.restart();{QPainter painter(&image);canvas->render(&painter);}const auto first=timer.nsecsElapsed();
 const auto plot=canvas->property("pgn_plot_rectangle").toRect();
 const auto originalPlot=image.copy(plot);
 timer.restart();{QPainter painter(&image);canvas->render(&painter);}const auto cached=timer.nsecsElapsed();
 if(!canvas->property("pgn_plot_cache_hit").toBool()||image.copy(plot)!=originalPlot){
  std::cerr<<"FAIL cached graph changed visible pixels\n";return 3;
 }
 if(canvas->property("pgn_graph_current_points").toInt()!=82009 ||
    canvas->property("pgn_graph_history_points").toInt()!=82009){
  std::cerr<<"FAIL current run was truncated at GUI birth\n";return 2;
 }
 if(argc>1&&!image.save(QString::fromLocal8Bit(argv[1])))return 1;
 FrontierGapEvent next;next.runId="qa-history";next.domainId="pgn-renderer-test";next.gpuUuid="GPU-qa";
 next.sequence=82010;next.observedUtc=now.addMSecs(1);next.gap="880";next.merit="19";
 next.endpointLeft="2000000000";next.endpointRight="2000000880";
 next.candidateId=next.endpointLeft+":"+next.endpointRight;next.proofState="prp_pending";
 pgn::ui::beginHistoryBatch(canvas.get());pgn::ui::presentGap(canvas.get(),next);pgn::ui::endHistoryBatch(canvas.get());
 {QPainter painter(&image);canvas->render(&painter);}
 if(canvas->property("pgn_plot_cache_hit").toBool()||canvas->property("pgn_graph_current_points").toInt()!=82010)return 4;
 lane.runId="qa-other-run";pgn::ui::presentDevice(canvas.get(),lane);
 {QPainter painter(&image);canvas->render(&painter);}
 if(canvas->property("pgn_plot_cache_hit").toBool()||canvas->property("pgn_graph_current_points").toInt()!=0||
    canvas->property("pgn_graph_history_points").toInt()!=82010)return 5;
 canvas->resize(1400,850);image=QImage(canvas->size(),QImage::Format_ARGB32_Premultiplied);
 {QPainter painter(&image);canvas->render(&painter);}
 if(canvas->property("pgn_plot_cache_hit").toBool())return 6;
 std::cout<<"PASS canvas82009 source events; ingest_ms="<<ingest/1e6<<" first_frame_ms="<<first/1e6<<" cached_frame_ms="<<cached/1e6<<" (individual measured executions)\n";
 return 0;
}

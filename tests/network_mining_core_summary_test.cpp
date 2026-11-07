#include "ui/mining_session_summary.h"
#include <QApplication>
#include <QImage>
#include <QStandardPaths>
#include <iostream>
#include <limits>
#include <stdexcept>
int main(int argc,char** argv){
 QApplication app(argc,argv);QStandardPaths::setTestModeEnabled(true);int checks=0;const auto need=[&](bool b){++checks;if(!b)throw std::runtime_error("network summary check "+std::to_string(checks));};
 try{
  const QString genesis(64,'a'),hash(64,'b');
  QJsonObject point{{"hash",hash},{"height",42},{"time",1000000},{"window_seconds",600},{"reported_positions","600000000000000"},{"audited_positions","10000"},{"merit_target_q16",29*65536}};
  QJsonObject metrics{{"schema","PGN_NETWORK_METRICS_V1"},{"genesis",genesis},{"bestblockhash",hash},{"height",42},{"median_time_past",1000000},
   {"rate_scope","reported_search_positions_per_chain_second_excludes_audits"},{"audit_validation_scope","historical_reports_qualified_at_snapshot_tip"},
   {"audit_enabled_operators",3},{"reporting_operators_in_window",2},{"audit_queue",QJsonObject{{"awaiting",4},{"awaiting_seed",1},{"assigned",2},{"committed",2}}},{"points",QJsonArray{point}}};
  using pgn::ui::networkMiningCoreSummary;const auto ready=networkMiningCoreSummary(metrics,genesis);need(ready.value("available").toBool());const auto rows=ready.value("rows").toArray();need(rows.size()==5);need(rows[0].toObject().value("value")=="3");need(rows[1].toObject().value("value")=="2");need(rows[2].toObject().value("value")=="1 Tpos/s");need(rows[3].toObject().value("value")=="29.0000");need(rows[4].toObject().value("value")=="9");
  need(networkMiningCoreSummary(metrics,genesis,90000).value("available").toBool());need(!networkMiningCoreSummary(metrics,genesis,90001).value("available").toBool());need(!networkMiningCoreSummary(metrics,genesis,-1).value("available").toBool());need(!networkMiningCoreSummary(metrics,QString(64,'c')).value("available").toBool());need(!networkMiningCoreSummary(metrics,{}).value("available").toBool());
  const auto bad=[&](QJsonObject value){need(!networkMiningCoreSummary(value,genesis).value("available").toBool());};
  for(const auto& key:{"schema","bestblockhash","rate_scope","audit_validation_scope"}){auto m=metrics;m.insert(key,"wrong");bad(m);}
  for(const auto& key:{"height","median_time_past","audit_enabled_operators","reporting_operators_in_window"}){auto m=metrics;m.insert(key,-1);bad(m);m.insert(key,1.5);bad(m);}
  for(const auto& key:{"height","time","hash"}){auto m=metrics,p=point;p.insert(key,QJsonValue{});m.insert("points",QJsonArray{p});bad(m);}
  for(const auto& key:{"reported_positions","audited_positions"}){auto m=metrics,p=point;p.insert(key,"01");m.insert("points",QJsonArray{p});bad(m);}
  {auto m=metrics,p=point;p.insert("audited_positions","600000000000001");m.insert("points",QJsonArray{p});bad(m);}
  for(const auto& key:{"window_seconds","merit_target_q16"}){auto m=metrics,p=point;p.insert(key,0);m.insert("points",QJsonArray{p});bad(m);}
  {auto m=metrics,q=m.value("audit_queue").toObject();q.insert("assigned",.5);m.insert("audit_queue",q);bad(m);}
  {auto m=metrics;QJsonArray points;for(int i=0;i<257;++i)points.append(point);m.insert("points",points);bad(m);}
  pgn::ui::MiningSessionSummary widget;widget.resize(300,560);QJsonObject context{{"genesis",genesis},{"source_stale",true},{"phase","Paused"}};widget.present({}, {},context);widget.presentNetworkMetrics(metrics,genesis);widget.show();app.processEvents();QImage image(widget.size(),QImage::Format_ARGB32);widget.render(&image);need(widget.property("pgn_network_summary_available").toBool());
  context.insert("phase","Starting");widget.present({}, {},context);widget.render(&image);need(widget.property("pgn_network_summary_available").toBool());
  context.insert("genesis",QString(64,'c'));widget.present({}, {},context);widget.render(&image);need(!widget.property("pgn_network_summary_available").toBool());
  context.insert("genesis",genesis);widget.present({}, {},context);widget.presentNetworkMetrics({},genesis);widget.render(&image);need(!widget.property("pgn_network_summary_available").toBool());
  widget.presentNetworkMetrics(metrics,genesis);widget.render(&image);need(widget.property("pgn_network_summary_available").toBool());
  context.remove("genesis");widget.present({}, {},context);widget.render(&image);need(widget.property("pgn_network_summary_available").toBool());
  std::cout<<"PASS "<<checks<<" coherent Core summary checks; paused/stale mining independent, own freshness clock, wrong chain/tip/count/volume/error refused; no RPC or mining\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

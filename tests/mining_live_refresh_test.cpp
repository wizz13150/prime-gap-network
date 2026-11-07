#include "ui/mining_results_view.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPersistentModelIndex>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QJsonObject frame(){
 QJsonArray rows;
 for(int i=0;i<30;++i)rows.append(QJsonObject{{"gap",1300-i*2},{"lower_prime",QString("150000000000000%1").arg(i*2000,6,10,QChar('0'))},
   {"upper_prime",QString("150000000000000%1").arg(i*2000+1300-i*2,6,10,QChar('0'))},{"merit",QString::number(29.-i*.07,'f',8)}});
 QJsonObject stats{{"count_total",30},{"threshold_counts",QJsonObject{{"954",30},{"1000",20},{"1100",10}}},
   {"merit_band_counts",QJsonObject{{"20",1},{"25",4},{"26",5},{"27",7},{"28",8}}},
   {"at_or_above_target",1},{"statistics_target",29.0}};
 QJsonObject scope{{"statistics",stats},{"best",rows},{"recent",rows}};
 return {{"schema","PGN_HORIZON_MINING_VIEW_V1"},{"ticket_id",QString(64,'a')},{"request_id",QString(64,'b')},
   {"genesis",QString(64,'c')},{"title","Synthetic live refresh fixture"},{"start","150000000000000000000"},
   {"end","150000200000000000000"},{"positions_total","200000000000000"},{"positions","0"},{"tests","0"},
   {"bits",68},{"minimum_gap",954},{"target_merit",29.0},{"phase","Searching"},{"source_stale",false},
   {"scopes",QJsonObject{{"ticket",scope}}}};
}
QJsonObject run(QSize size,bool baseline){
 QTemporaryDir root;require(root.isValid(),"temporary root");
 std::unique_ptr<QWidget> view(pgn::ui::createMiningResultsView(root.path()));view->resize(size);view->show();
 auto packet=frame();pgn::ui::presentMiningTicketData(view.get(),packet);QApplication::processEvents();
 auto* best=view->findChild<QTableWidget*>("pgn-results-best");
 auto* recent=view->findChild<QTableWidget*>("pgn-results-recent");
 require(best&&recent&&best->item(0,0),"real Best/Recent tables missing");
 require(!best->item(0,0)->icon().isNull()&&best->item(0,0)->data(Qt::AccessibleDescriptionRole).toString()=="Gold discovery","Gold discovery rosette is absent or mislabelled");
 const auto initialIcon=best->item(0,0)->icon().cacheKey();
 best->setCurrentCell(2,0);best->selectRow(2);
 const QString selected=best->item(2,0)->data(Qt::UserRole).toString();
 QPersistentModelIndex stable(best->model()->index(0,0));
 int resets=0,changes=0;
 auto reset=QObject::connect(best->model(),&QAbstractItemModel::modelReset,view.get(),[&]{++resets;});
 auto changed=QObject::connect(best->model(),&QAbstractItemModel::dataChanged,view.get(),[&]{++changes;});
 constexpr int packets=200;
 QElapsedTimer elapsed;elapsed.start();
 for(int i=1;i<=packets;++i){
  packet.insert("positions",QString::number(qint64(i)*1000000000));packet.insert("tests",QString::number(qint64(i)*1000000));
  pgn::ui::presentMiningTicketData(view.get(),packet);
  if(i%20==0)QApplication::processEvents();
 }
 QApplication::processEvents();
 const double milliseconds=elapsed.nsecsElapsed()/1e6;
 QObject::disconnect(reset);QObject::disconnect(changed);
 if(!baseline){
  require(stable.isValid(),"unchanged stream reset a persistent table index");
  require(resets==0&&changes==0,"progress-only packets mutated the Best table");
  require(best->item(0,0)->icon().cacheKey()==initialIcon,"progress-only refresh recreated its rosette");
  require(best->currentItem()&&best->currentItem()->data(Qt::UserRole).toString()==selected,"selection lost during live progress");
  auto scopes=packet.value("scopes").toObject(),scope=scopes.value("ticket").toObject();
  auto rows=scope.value("best").toArray();auto first=rows[0].toObject();first.insert("gap",1400);first.insert("merit","30.0");rows[0]=first;scope.insert("best",rows);
  auto stats=scope.value("statistics").toObject();stats.insert("count_total",31);scope.insert("statistics",stats);scopes.insert("ticket",scope);packet.insert("scopes",scopes);
  pgn::ui::presentMiningTicketData(view.get(),packet);
  require(best->item(0,0)->text()=="1400","new result was deferred instead of shown immediately");
  require(stable.isValid(),"a new result reset the entire table model");
  require(view->property("pgn_results_points").toInt()==31,"statistics did not update with result");
  require(best->rowCount()<rows.size(),"fixture needs a Best row outside the visible prefix");
  scope.insert("recent",QJsonArray{rows.last()});scopes.insert("ticket",scope);packet.insert("scopes",scopes);
  pgn::ui::presentMiningTicketData(view.get(),packet);
  require(recent->item(0,0)&&recent->item(0,0)->foreground().color()==QColor("#e7f9f1"),"offscreen Best row colored a Recent row");
  packet.insert("target_merit",QJsonValue(QJsonValue::Null));stats.insert("statistics_target",QJsonValue(QJsonValue::Null));scope.insert("statistics",stats);scopes.insert("ticket",scope);packet.insert("scopes",scopes);
  pgn::ui::presentMiningTicketData(view.get(),packet);
  require(best->item(0,0)->icon().isNull(),"unknown target retained an inferred medal");
  pgn::ui::presentMiningRecordEligibility(view.get(),QJsonArray{first});
  require(best->item(0,0)->text().startsWith("★"),"record eligibility did not refresh cached data");
  require(!best->item(0,0)->icon().isNull()&&best->item(0,0)->data(Qt::AccessibleDescriptionRole).toString()=="Reference improvement","verified reference improvement needs a distinct rosette independently of the ticket target");
  packet.insert("ticket_id",QString(64,'d'));scope.insert("best",QJsonArray{});scope.insert("recent",QJsonArray{});scopes.insert("ticket",scope);packet.insert("scopes",scopes);
  pgn::ui::presentMiningTicketData(view.get(),packet);QApplication::processEvents();
  require(!best->item(0,0)&&!recent->item(0,0),"old rows survived the next empty ticket");
 }
 return {{"width",size.width()},{"height",size.height()},{"packets",packets},{"milliseconds",milliseconds},
   {"milliseconds_per_packet",milliseconds/packets},{"best_model_resets",resets},{"best_data_changes",changes},
   {"persistent_index_survived",stable.isValid()},{"boundary","Actual MiningResultsView input+archives+Qt event processing; no engine, GPU, network or sensor work"}};
}
}
int main(int argc,char** argv){
 QApplication app(argc,argv);QApplication::setOrganizationName("PGN-Isolated-Cadence-QA");QApplication::setApplicationName("MiningLiveRefresh");QStandardPaths::setTestModeEnabled(true);
 try{QSettings{}.clear();const bool baseline=app.arguments().contains("--baseline");
  const QJsonObject result{{"PASS",true},{"mode",baseline?"baseline":"candidate"},{"samples",QJsonArray{run({1500,760},baseline),run({1060,540},baseline)}}};
  std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

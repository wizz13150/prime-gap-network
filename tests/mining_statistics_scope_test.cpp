#include "ui/mining_results_view.h"
#include <QApplication>
#include <QGroupBox>
#include <QJsonArray>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
#include <stdexcept>
static void need(bool b,const char* text){if(!b)throw std::runtime_error(text);}
static void settle(QWidget* v){for(int n=0;n<8;++n)QApplication::processEvents();v->grab();}
static QJsonObject packet(int sequence,int count){
 const QJsonObject stats{{"count_total",QString::number(count)},{"statistics_target",29.0},{"at_or_above_target","0"},{"threshold_counts",QJsonObject{{"954",QString::number(count)}}},{"merit_band_counts",QJsonObject{{"22",QString::number(count)}}}};
 const QJsonObject row{{"lower_prime",QString("150000000000000000%1").arg(sequence,3,10,QChar('0'))},{"upper_prime","150000000000000001051"},{"gap",1050},{"merit","22.5"}};
 return {{"schema","PGN_HORIZON_MINING_VIEW_V1"},{"genesis",QString(64,'a')},{"stream_id","statistics-real-widget-fixture"},{"ticket_id",QString(64,QChar('b'+sequence))},{"request_id",QString(64,QChar('e'+sequence))},{"local_sequence",sequence},{"work_role","search"},{"phase","Searching"},{"positions","1000"},{"tests","100"},{"compute_time_ns","10000000"},{"compute_epoch","fixture-clock"},{"start","150000000000000000000"},{"end","150000200000000000000"},{"minimum_gap",954},{"bits",68},{"target_merit",29.0},{"scopes",QJsonObject{{"ticket",QJsonObject{{"statistics",stats},{"best",QJsonArray{row}},{"recent",QJsonArray{row}}}}}}};
}
int main(int argc,char** argv){
 QApplication app(argc,argv);QCoreApplication::setOrganizationName("PGN-QA");QCoreApplication::setApplicationName("statistics-scope155");QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;if(!dir.isValid())return 2;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,dir.path());
 try{
  auto view=std::unique_ptr<QWidget>(pgn::ui::createMiningResultsView(dir.path()));view->resize(1366,700);view->show();
  pgn::ui::presentMiningTicketData(view.get(),packet(1,3));pgn::ui::presentMiningTicketData(view.get(),packet(2,5));settle(view.get());
  auto* gaps=view->findChild<QTableWidget*>("pgn-results-gap-statistics");auto* best=view->findChild<QTableWidget*>("pgn-results-best");auto* summary=view->findChild<QWidget*>("pgn-session-statistics");need(gaps&&best&&summary,"Actual statistics widgets absent");
  auto* session=view->findChild<QPushButton*>("pgn-summary-session");need(session,"Session button absent");session->click();settle(view.get());
  need(gaps->rowCount()>0&&gaps->item(0,1)->text()=="8","Session selected but histogram still shows only the current ticket");
  need(summary->property("pgn_summary_statistics").toJsonObject().value("ticket_count").toString()=="2","Summary and histogram do not describe the same two tickets");
  auto* cell=best->item(0,0);const auto identity=cell->data(Qt::UserRole);best->setCurrentCell(0,0);auto* ticket=view->findChild<QPushButton*>("pgn-summary-ticket");auto* history=view->findChild<QPushButton*>("pgn-summary-history");need(ticket&&history,"Explicit ticket/history scopes absent");
  ticket->click();settle(view.get());need(gaps->item(0,1)->text()=="5","Ticket scope is not the current ticket");
  history->click();settle(view.get());need(gaps->item(0,1)->text()=="8","History scope lost an earlier ticket");
  need(best->item(0,0)==cell&&cell->data(Qt::UserRole)==identity&&best->currentItem()==cell,"Statistics scope changed the live Best model or selection");
  for(auto size:{QSize(800,320),QSize(1366,700),QSize(1920,900)}){view->resize(size);settle(view.get());for(auto* button:{ticket,session,history})need(button->isVisible()&&button->width()>20,"A statistics scope button disappeared");}
  view.reset();view.reset(pgn::ui::createMiningResultsView(dir.path()));view->resize(1366,700);view->show();pgn::ui::presentMiningTicketData(view.get(),packet(3,7));settle(view.get());
  gaps=view->findChild<QTableWidget*>("pgn-results-gap-statistics");need(gaps->item(0,1)->text()=="15","Persisted history scope does not restore all three ticket counts");
  session=view->findChild<QPushButton*>("pgn-summary-session");session->click();settle(view.get());need(gaps->item(0,1)->text()=="7","A fresh session included an earlier process session");
  if(argc>1)need(view->grab().save(QString::fromLocal8Bit(argv[1])),"Scope screenshot could not be saved");
  std::cout<<"PASS actual Qt: ticket/session/history distributions agree with summary; Best identity retained; history across restart and fresh-session separation; three sizes\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

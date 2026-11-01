#include "ui/native_archive_view.h"
#include "ui/panel_style.h"
#include "science/archive_reader.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QDateTime>
#include <QSignalBlocker>
#include <QDesktopServices>
#include <QUrl>
namespace pgn::ui {
namespace {
class NativeArchive final:public QWidget {
public:
 explicit NativeArchive(QString root,QWidget* parent):QWidget(parent),root_(std::move(root)){
  setObjectName("pgn-native-archive-view");setStyleSheet(dataPanelStyle());
  database_=root_+"/data/runtime/scientific-history.sqlite";
  auto* box=new QVBoxLayout(this);box->setContentsMargins(20,18,20,18);
  auto* heading=new QLabel("NATIVE SCIENTIFIC ARCHIVE · EXACT ENDPOINTS + ORIGINAL SOURCES");heading->setStyleSheet("font:700 20px 'Segoe UI';color:#e9fffb");box->addWidget(heading);
  auto* warning=new QLabel("SOURCE PRP OBSERVATIONS · independent primality proof and exhaustive interval coverage remain pending. A fixed snapshot excludes new arrivals until Refresh.");
  warning->setWordWrap(true);warning->setStyleSheet("color:#ffad4a");box->addWidget(warning);
  auto* controls=new QHBoxLayout;previous_=new QPushButton("Previous");next_=new QPushButton("Next");refresh_=new QPushButton("Refresh snapshot");
  search_=new QLineEdit;search_->setPlaceholderText("Exact left endpoint p (decimal) · empty = all");search_->setMaxLength(160);
  auto* searchButton=new QPushButton("Find exact p");auto* sources=new QPushButton("Open conserved journals");
  controls->addWidget(previous_);controls->addWidget(next_);controls->addWidget(refresh_);controls->addWidget(search_,1);controls->addWidget(searchButton);controls->addWidget(sources);box->addLayout(controls);
  status_=new QLabel;box->addWidget(status_);
  table_=new QTableWidget;table_->setObjectName("pgn-native-archive-records");table_->setColumnCount(6);
  table_->setHorizontalHeaderLabels({"Left endpoint p","Right endpoint q","Gap q − p","Original merit","Original UTC","Proof status"});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);table_->horizontalHeader()->setStretchLastSection(true);
  table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft|Qt::AlignVCenter);table_->verticalHeader()->hide();
  table_->setAlternatingRowColors(true);table_->setColumnWidth(0,230);table_->setColumnWidth(1,230);table_->setColumnWidth(2,110);table_->setColumnWidth(3,180);table_->setColumnWidth(4,185);table_->setColumnWidth(5,240);box->addWidget(table_,3);
  auto* originControls=new QHBoxLayout;originStatus_=new QLabel("Select an exact pair to inspect its source origins");originsNext_=new QPushButton("More origins");
  originControls->addWidget(originStatus_,1);originControls->addWidget(originsNext_);box->addLayout(originControls);
  origins_=new QTableWidget;origins_->setObjectName("pgn-native-archive-origins");origins_->setColumnCount(2);origins_->setHorizontalHeaderLabels({"Original run identity","Original source journal"});
  origins_->setEditTriggers(QAbstractItemView::NoEditTriggers);origins_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);box->addWidget(origins_,1);
  connect(refresh_,&QPushButton::clicked,this,[this]{reset();});connect(searchButton,&QPushButton::clicked,this,[this]{reset();});
  connect(search_,&QLineEdit::returnPressed,this,[this]{reset();});
  connect(previous_,&QPushButton::clicked,this,[this]{if(after_.size()>1){after_.removeLast();load();}});
  connect(next_,&QPushButton::clicked,this,[this]{if(current_.more&&!current_.rows.isEmpty()){after_.append(current_.rows.last().id);load();}});
  connect(sources,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(root_+"/data/runtime/source-journals"));});
  connect(table_,&QTableWidget::itemSelectionChanged,this,[this]{const int row=table_->currentRow();if(row<0||row>=current_.rows.size())return;
   selected_=current_.rows[row].id;originAfterRun_.clear();originAfterSource_.clear();loadOrigins();});
  connect(originsNext_,&QPushButton::clicked,this,[this]{if(!originPage_.more||originPage_.rows.isEmpty())return;
   originAfterRun_=originPage_.rows.last().run;originAfterSource_=originPage_.rows.last().source;loadOrigins();});
  connect(&pageWatcher_,&QFutureWatcher<pgn::science::ObservationPage>::finished,this,[this]{
   current_=pageWatcher_.result();if(!current_.error.isEmpty()){table_->setRowCount(0);origins_->setRowCount(0);selected_=0;status_->setText(current_.error);this->controls(true);return;}
   through_=current_.through;if(after_.size()==1)total_=current_.total;
   const QSignalBlocker block(table_);table_->setUpdatesEnabled(false);table_->setRowCount(current_.rows.size());
   for(int i=0;i<current_.rows.size();++i){const auto& row=current_.rows[i];bool ok=false;const auto time=row.originalUnixSeconds.toLongLong(&ok);
    const auto utc=ok?QDateTime::fromSecsSinceEpoch(time,Qt::UTC).toString(Qt::ISODate):QStringLiteral("UNKNOWN · no original date");
    const QStringList values{row.p,row.q,row.gap,row.merit,utc,"SOURCE PRP · PROOF PENDING"};
    for(int column=0;column<values.size();++column){auto* item=new QTableWidgetItem(values[column]);item->setToolTip(values[column]);table_->setItem(i,column,item);}}
   table_->setUpdatesEnabled(true);selected_=0;origins_->setRowCount(0);originsNext_->setEnabled(false);
   originStatus_->setText("Select an exact pair to inspect its source origins");
   status_->setText(QStringLiteral("%1 unique pairs in snapshot · page %2 · %3 exact rows · no sampling · watermark %4").arg(total_).arg(after_.size()).arg(current_.rows.size()).arg(through_));
   setProperty("pgn_archive_native_rows",current_.rows.size());setProperty("pgn_archive_native_total",total_);this->controls(true);
  });
  connect(&originWatcher_,&QFutureWatcher<pgn::science::OriginPage>::finished,this,[this]{
   // A changed record selection cannot publish the previous record's origins.
   if(originInFlight_!=selected_){loadOrigins();return;}
   originPage_=originWatcher_.result();origins_->setRowCount(originPage_.rows.size());
   for(int i=0;i<originPage_.rows.size();++i){origins_->setItem(i,0,new QTableWidgetItem(originPage_.rows[i].run));origins_->setItem(i,1,new QTableWidgetItem(originPage_.rows[i].source));}
   originStatus_->setText(originPage_.error.isEmpty()?QStringLiteral("Pair ID %1 · %2 origins on this page · complete keyset access").arg(selected_).arg(originPage_.rows.size()):originPage_.error);
   originsNext_->setEnabled(originPage_.error.isEmpty()&&originPage_.more);setProperty("pgn_archive_native_origins",originPage_.rows.size());
  });
  reset();
 }
private:
 void controls(bool enabled){previous_->setEnabled(enabled&&after_.size()>1);next_->setEnabled(enabled&&current_.more);refresh_->setEnabled(enabled);search_->setEnabled(enabled);}
 void reset(){if(pageWatcher_.isRunning())return;after_={0};through_=0;total_=0;load();}
 void load(){if(pageWatcher_.isRunning())return;controls(false);status_->setText("READ-ONLY ARCHIVE · loading bounded page off UI thread");
  const auto database=database_,left=search_->text().trimmed();const auto after=after_.last(),through=through_;
  pageWatcher_.setFuture(QtConcurrent::run([database,after,through,left]{return pgn::science::readObservations(database,after,through,left);}));}
 void loadOrigins(){if(originWatcher_.isRunning()||!selected_)return;originInFlight_=selected_;originsNext_->setEnabled(false);
  const auto database=database_,afterRun=originAfterRun_,afterSource=originAfterSource_;const auto selected=selected_;
  originWatcher_.setFuture(QtConcurrent::run([database,selected,afterRun,afterSource]{return pgn::science::readOrigins(database,selected,afterRun,afterSource);}));}
 QString root_,database_,originAfterRun_,originAfterSource_;QVector<qint64> after_;qint64 through_{},total_{},selected_{},originInFlight_{};
 QLabel *status_{},*originStatus_{};QPushButton *previous_{},*next_{},*refresh_{},*originsNext_{};QLineEdit* search_{};QTableWidget *table_{},*origins_{};
 QFutureWatcher<pgn::science::ObservationPage> pageWatcher_;QFutureWatcher<pgn::science::OriginPage> originWatcher_;
 pgn::science::ObservationPage current_;pgn::science::OriginPage originPage_;
};
}
QWidget* createNativeArchiveView(const QString& root,QWidget* parent){return new NativeArchive(root,parent);}
}

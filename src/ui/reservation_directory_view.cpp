#include "ui/reservation_directory_view.h"
#include "booking/booking_model.h"
#include "booking/decimal_range.h"
#include "booking/round_range_catalog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QCompleter>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
namespace pgn::ui {namespace {
struct Entry{QString owner,start,end,state,scope;bool processed{},local{};};
class ReservationDirectory final:public QWidget{
public:
 ReservationDirectory(pgn::booking::BookingModel* model,std::function<void(const QString&,const QString&)> navigate,QWidget* parent):QWidget(parent),model_(model),navigate_(std::move(navigate)){
  auto* box=new QVBoxLayout(this);auto* tools=new QHBoxLayout;
  search_=new QLineEdit;search_->setObjectName("pgn-owner-search");search_->setPlaceholderText("Find a miner · e.g. robert, Benxy, Wizz");
  state_=new QComboBox;state_->setObjectName("pgn-owner-state");state_->addItems({"Reservations + active work","Processed history","All states"});
  tools->addWidget(search_,1);tools->addWidget(state_);box->addLayout(tools);
  summary_=new QLabel;summary_->setObjectName("pgn-owner-summary");summary_->setWordWrap(true);box->addWidget(summary_);
  table_=new QTableWidget(0,6);table_->setObjectName("pgn-owner-results");table_->setHorizontalHeaderLabels({"Miner","Exact start (included)","Exact end (excluded)","State","Progress","Source"});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->verticalHeader()->hide();table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);table_->horizontalHeader()->setStretchLastSection(true);box->addWidget(table_,1);
  auto* bottom=new QHBoxLayout;notice_=new QLabel;notice_->setWordWrap(true);bottom->addWidget(notice_,1);
  go_=new QPushButton("Go to selected reservation");go_->setObjectName("pgn-owner-go");bottom->addWidget(go_);box->addLayout(bottom);
  connect(search_,&QLineEdit::textChanged,this,[this]{render();});connect(state_,&QComboBox::currentIndexChanged,this,[this]{render();});
  connect(table_,&QTableWidget::itemSelectionChanged,this,[this]{selectionChanged();});
  connect(table_,&QTableWidget::cellDoubleClicked,this,[this](int,int){go_->click();});connect(go_,&QPushButton::clicked,this,[this]{const int row=table_->currentRow();if(row>=0&&row<visible_.size()&&go_->isEnabled()){const auto& r=visible_[row];navigate_(r.start,r.end);}});
  model_->subscribe(this,[this]{refresh();});refresh();
 }
private:
 void refresh(){
  const auto key=model_->source().sha256+"/"+QString::number(model_->jobsRevision());
  if(key==revision_){updateProgress();return;}revision_=key;
  const auto source=pgn::booking::loadReservationDirectory(QFileInfo(model_->historyDatabase()).absolutePath(),model_->source());
  entries_.clear();QSet<QString> seen,owners;
  auto add=[&](Entry r){const auto k=r.owner+"/"+r.start+"/"+r.end+"/"+QString::number(r.processed)+"/"+r.scope;if(seen.contains(k))return;seen.insert(k);owners.insert(r.owner);entries_.append(std::move(r));};
  for(const auto& r:source.ranges)if(!r.owner.isEmpty())add({r.owner,r.start,r.end,r.status=="Complete"?"Declared complete (not proof)":"Reserved",r.row<0?"Development copy · manual exact interval":"Public historical snapshot",r.status=="Complete",false});
  const auto& runtime=model_->runtime();
  auto localJobs=model_->jobs();std::stable_sort(localJobs.begin(),localJobs.end(),[&](const auto& a,const auto& b){return (a.id==runtime.run)>(b.id==runtime.run);});
  for(const auto& r:localJobs){
   if(r.kind=="LOCAL_INTENT"&&r.start==runtime.start&&r.end==runtime.end&&!runtime.run.isEmpty())continue;
   add({model_->researcherName(),r.start,r.end,r.state=="ACTIVE"?"Processing":r.state=="PAUSED"?"Paused":"Reserved (local)","This miner · not a public lease",false,true});
  }
  for(const auto& r:model_->completedRanges())add({model_->researcherName(),r.start,r.end,"Source finished · proof pending","This miner · exact completion receipt",true,true});
  std::stable_sort(entries_.begin(),entries_.end(),[](const auto& a,const auto& b){const auto owner=a.owner.compare(b.owner,Qt::CaseInsensitive);return owner?owner<0:pgn::booking::compareUnsigned(a.start,b.start)<0;});
  QStringList names=owners.values();names.sort(Qt::CaseInsensitive);auto* completion=new QCompleter(names,search_);completion->setCaseSensitivity(Qt::CaseInsensitive);completion->setFilterMode(Qt::MatchContains);if(auto* old=search_->completer())old->deleteLater();search_->setCompleter(completion);
  directoryError_=source.error;render();
 }
 void render(){
  QString selected;const int prior=table_->currentRow();if(prior>=0&&prior<visible_.size())selected=visible_[prior].owner+"/"+visible_[prior].start+"/"+visible_[prior].end;
  visible_.clear();QSet<QString> owners;
  const auto query=search_->text().trimmed();for(const auto& r:entries_){if(!query.isEmpty()&&!r.owner.contains(query,Qt::CaseInsensitive))continue;if(state_->currentIndex()==0&&r.processed)continue;if(state_->currentIndex()==1&&!r.processed)continue;visible_.append(r);owners.insert(r.owner);}
  table_->setUpdatesEnabled(false);table_->setRowCount(visible_.size());int restore=-1;
  for(int i=0;i<visible_.size();++i){const auto& r=visible_[i];const QStringList columns{r.owner,r.start,r.end,r.state,"Not published",r.scope};for(int c=0;c<columns.size();++c)table_->setItem(i,c,new QTableWidgetItem(columns[c]));if(r.owner+"/"+r.start+"/"+r.end==selected)restore=i;}
  if(restore>=0)table_->selectRow(restore);table_->setUpdatesEnabled(true);
  summary_->setText(QString("%1 intervals · %2 miners · across all known ranges, independent of the current page.\nPublic declarations are snapshots; other miners' live progress is not yet published.%3").arg(visible_.size()).arg(owners.size()).arg(directoryError_.isEmpty()?QString{}:"\nDirectory warning: "+directoryError_));
  updateProgress();selectionChanged();
 }
 void updateProgress(){const auto& runtime=model_->runtime();for(int i=0;i<visible_.size();++i){const auto& r=visible_[i];QString value="Not published";
   if(r.processed)value=r.local?"Source complete · not independently certified":"Declared complete · not independently certified";
   else if(r.local&&r.start==runtime.start&&r.end==runtime.end&&!runtime.run.isEmpty())value=runtime.fraction?QString::number(*runtime.fraction*100,'f',5)+"% · "+runtime.observedUtc.toUTC().toString("HH:mm:ss.zzz 'UTC'"):"Awaiting progress";
   else if(r.local)value="Not started";
   if(auto* cell=table_->item(i,4);cell&&cell->text()!=value)cell->setText(value);
  }}
 void selectionChanged(){const int row=table_->currentRow();bool supported=false;if(row>=0&&row<visible_.size()){const auto& r=visible_[row];try{supported=pgn::booking::decimalZoneIndex(r.start)/1000<=pgn::booking::roundRangeMaximumPage;}catch(...){supported=false;}}
  go_->setEnabled(supported);notice_->setText(row<0?"Select an interval to navigate. This view never reserves or releases work.":supported?"Exact declared interval retained. Navigation does not change reservations.":"Outside the configured browsing/mining catalogue. Exact interval retained; no full block/zone or processed status is inferred.");
 }
 pgn::booking::BookingModel* model_;std::function<void(const QString&,const QString&)> navigate_;QLineEdit* search_;QComboBox* state_;QTableWidget* table_;QLabel *summary_,*notice_;QPushButton* go_;QVector<Entry> entries_,visible_;QString revision_,directoryError_;
};}
QWidget* createReservationDirectory(pgn::booking::BookingModel* model,std::function<void(const QString&,const QString&)> navigate,QWidget* parent){return new ReservationDirectory(model,std::move(navigate),parent);}
}

#include "ui/queue_view.h"
#include "booking/booking_model.h"
#include "booking/queue_zone.h"
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QSet>
#include <QCheckBox>

namespace pgn::ui {
namespace {
class QueueView final:public QWidget {
 pgn::booking::BookingModel* model_;QListWidget* list_;QLabel* message_;QCheckBox* automatic_;QString fingerprint_;QStringList queuedIds_;bool pending_{};
 void render(){
  const auto device=model_->runtime().device;
  const auto preference=model_->automaticAfterManual(device);
  {const QSignalBlocker blocker(automatic_);automatic_->setChecked(preference.value_or(false));}
  automatic_->setEnabled(preference.has_value());
  automatic_->setProperty("pgn_device",device);
  automatic_->setToolTip(preference.has_value()?
   "Only controls continuation after manual reservations. An explicit start without reservations is automatic. This switch does not start mining. Free blocks are claimed only when processing starts. Saved for this GPU: "+device:
   "Waiting for an identified GPU and readable local mining preferences. Automatic continuation is not enabled.");
  if(pending_)return;QString next;for(const auto& job:model_->jobs())if(job.kind=="LOCAL_INTENT"&&(job.state=="QUEUED"||job.state=="ARMED"))next+=job.id+job.start+job.end+job.state;
  if(next==fingerprint_&&list_->count())return;fingerprint_=next;const auto selected=list_->currentItem()?list_->currentItem()->data(Qt::UserRole).toString():QString{};
  const QSignalBlocker blocker(list_->model());list_->setUpdatesEnabled(false);list_->clear();queuedIds_.clear();QSet<QString> seen;
  for(const auto& job:model_->jobs())if(job.kind=="LOCAL_INTENT"&&(job.state=="QUEUED"||job.state=="ARMED")){
   queuedIds_.append(job.id);const auto zone=pgn::booking::queueZone(job);const auto key=zone.start+"/"+zone.end;if(seen.contains(key))continue;seen.insert(key);
   const auto number=zone.start.size()>17?zone.start.left(zone.start.size()-17):QString("0");bool ok=false;const auto index=number.toULongLong(&ok);
   auto* item=new QListWidgetItem(ok?QString("Range %1 · Zone %2").arg(index/1000).arg(index%1000+1):QString("Exact queued range"),list_);
   item->setData(Qt::UserRole,zone.start);item->setData(Qt::UserRole+1,zone.end);item->setToolTip("["+zone.start+", "+zone.end+")\nOnly reserved blocks are queued; blocks run in numeric order. Drag to change zone priority.");
   if(zone.start==selected)list_->setCurrentItem(item);
  }
  for(int i=0;i<list_->count();++i){auto* item=list_->item(i);int count=0;for(const auto& job:model_->jobs())if(job.kind=="LOCAL_INTENT"&&(job.state=="QUEUED"||job.state=="ARMED")&&pgn::booking::compareUnsigned(item->data(Qt::UserRole).toString(),job.start)<=0&&pgn::booking::compareUnsigned(job.end,item->data(Qt::UserRole+1).toString())<=0)++count;item->setText(QString("%1. %2 · %3 blocks").arg(i+1).arg(item->text()).arg(count));}
  if(!list_->count()){auto* item=new QListWidgetItem("No reserved zones queued",list_);item->setFlags(Qt::NoItemFlags);}
  list_->setUpdatesEnabled(true);list_->viewport()->update();
 }
public:
 QueueView(pgn::booking::BookingModel* model,std::function<void(const QString&,const QString&)> inspect,QWidget* parent):QWidget(parent),model_(model){
  setObjectName("pgn-queue-view");auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,2,0,2);layout->setSpacing(3);
  auto* heading=new QHBoxLayout;auto* title=new QLabel("QUEUE · RESERVED ZONES");title->setStyleSheet("font-weight:600;color:#69efc5;font-size:10px;");heading->addWidget(title,1);
  auto* numeric=new QPushButton("Numeric order");numeric->setObjectName("pgn-queue-numeric-order");numeric->setToolTip("Restore ascending zones and blocks. The current block keeps running.");numeric->setStyleSheet("padding:3px;min-height:12px;font-size:9px;");heading->addWidget(numeric);layout->addLayout(heading);
  list_=new QListWidget;list_->setObjectName("pgn-zone-queue");list_->setSelectionMode(QAbstractItemView::SingleSelection);list_->setDragDropMode(QAbstractItemView::InternalMove);list_->setDefaultDropAction(Qt::MoveAction);list_->setDragEnabled(true);list_->setAcceptDrops(true);list_->setDropIndicatorShown(true);list_->setMaximumHeight(145);list_->setMinimumHeight(44);list_->setStyleSheet("QListWidget{background:#061b23;border:1px solid #286d73;border-radius:6px;font-size:10px;}QListWidget::item{padding:5px;}QListWidget::item:selected{background:#0d353b;border:1px solid #e8fff8;}");layout->addWidget(list_);
  automatic_=new QCheckBox("Continue automatically after my reservations");
  automatic_->setObjectName("pgn-queue-automatic-after-manual");
  automatic_->setStyleSheet("QCheckBox{font-size:10px;color:#b8e4ec;spacing:6px;}QCheckBox::indicator{width:18px;height:14px;}");
  layout->addWidget(automatic_);
  // One stable status slot: drag feedback never grows the compact layout.
  message_=new QLabel("Drag zones to reorder · blocks stay numeric · current block stays running");message_->setObjectName("pgn-queue-order-message");message_->setWordWrap(true);message_->setFixedHeight(26);message_->setStyleSheet("font-size:9px;color:#8ebdc6;");layout->addWidget(message_);
  QObject::connect(automatic_,&QCheckBox::toggled,this,[this](bool enabled){
   const auto error=model_->setAutomaticAfterManual(automatic_->property("pgn_device").toString(),enabled);
   render();
   message_->setText(error.isEmpty()?(enabled?
    "Saved · automatic continuation after manual reservations: ON. This switch does not start mining.":
    "Saved · finish manual reservations, then stop. No automatic fallback."):error);
  });
  QObject::connect(list_->model(),&QAbstractItemModel::rowsMoved,this,[this]{
   if(pending_)return;QVector<pgn::booking::ExactBlockSelection> order;for(int i=0;i<list_->count();++i){auto* item=list_->item(i);const auto a=item->data(Qt::UserRole).toString(),b=item->data(Qt::UserRole+1).toString();if(!a.isEmpty())order.append({a,b,a,b});}
   const auto expected=queuedIds_;pending_=true;QTimer::singleShot(0,this,[this,order,expected]{const auto error=model_->reorderQueuedZones(order,expected);pending_=false;fingerprint_.clear();render();message_->setText(error.isEmpty()?"Zone priority saved. Current work unchanged.":error);message_->show();});
  });
  QObject::connect(numeric,&QPushButton::clicked,this,[this]{const auto error=model_->resetQueueOrder();fingerprint_.clear();render();message_->setText(error.isEmpty()?"Ascending numeric order restored. Current work unchanged.":error);message_->show();});
  QObject::connect(list_,&QListWidget::itemDoubleClicked,this,[inspect](QListWidgetItem* item){const auto a=item->data(Qt::UserRole).toString();if(!a.isEmpty()&&inspect)inspect(a,item->data(Qt::UserRole+1).toString());});
  model_->subscribe(this,[this]{render();});render();
 }
};
}
QWidget* createQueueView(pgn::booking::BookingModel* model,std::function<void(const QString&,const QString&)> inspect,QWidget* parent){return new QueueView(model,std::move(inspect),parent);}
}

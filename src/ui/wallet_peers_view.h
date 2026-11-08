#pragma once
#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QShowEvent>
#include <QTableWidget>
#include <QTimer>
#include <QTimeZone>
#include <QVBoxLayout>
#include <cmath>
#include <functional>

namespace pgn::ui {
class WalletPeersView final:public QWidget {
public:
 using Reader=std::function<void(std::function<void(QJsonArray,QString)>)>;
 explicit WalletPeersView(Reader read,QWidget* parent=nullptr):QWidget(parent),read_(std::move(read)){
  auto readable=font();readable.setPixelSize(13);setFont(readable);
  setStyleSheet("QTableWidget{font-size:13px;}QHeaderView::section{font-size:12px;font-weight:600;}QLabel{font-size:13px;}QPushButton{font-size:12px;min-height:28px;}");
  auto* layout=new QVBoxLayout(this);layout->setContentsMargins(8,8,8,8);
  auto* tools=new QHBoxLayout;status_=new QLabel("Peer information has not been read yet");status_->setObjectName("pgn-peers-status");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);tools->addWidget(status_,1);
  refresh_=new QPushButton("Refresh peers");refresh_->setObjectName("pgn-peers-refresh");tools->addWidget(refresh_);layout->addLayout(tools);
  table_=new QTableWidget(0,8);table_->setObjectName("pgn-wallet-peers");table_->setHorizontalHeaderLabels({"ID","Connection","Endpoint","Client","Connected since (UTC)","Latency (estimated)","Received (bytes)","Sent (bytes)"});
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionBehavior(QAbstractItemView::SelectRows);table_->setSelectionMode(QAbstractItemView::SingleSelection);table_->setAlternatingRowColors(true);table_->verticalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(26);table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);table_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);table_->setSortingEnabled(true);table_->sortItems(0);layout->addWidget(table_,1);
  connect(refresh_,&QPushButton::clicked,this,[this]{refresh();});timer_.setInterval(5000);connect(&timer_,&QTimer::timeout,this,[this]{if(isVisible())refresh();});timer_.start();
 }
 void refresh(){
  if(busy_)return;busy_=true;refresh_->setEnabled(false);QPointer<WalletPeersView> weak(this);
  read_([weak](QJsonArray peers,QString error){if(!weak)return;weak->busy_=false;weak->refresh_->setEnabled(true);weak->present(peers,error);});
 }
 void present(const QJsonArray& peers,QString error={}){
  QSet<qint64> ids;
  if(error.isEmpty()){
   if(peers.size()>256)error="Peer snapshot exceeds its display bound";
   for(const auto& value:peers){
    const auto p=value.toObject();const auto id=p.value("id").toInteger(-1);
    if(!value.isObject()||id<0||ids.contains(id)||!p.value("addr").isString()||p.value("addr").toString().size()>512||!p.value("subver").isString()||p.value("subver").toString().size()>512||!p.value("inbound").isBool()||p.value("conntime").toInteger(-1)<0||p.value("bytesrecv").toInteger(-1)<0||p.value("bytessent").toInteger(-1)<0){error="Invalid peer snapshot";break;}
    ids.insert(id);
   }
  }
  if(!error.isEmpty()){status_->setText("Refresh failed: "+error.left(240)+(haveSnapshot_?" · Showing the previous snapshot":""));setProperty("pgn_peers_stale",true);return;}
  const auto selected=table_->currentRow()>=0?table_->item(table_->currentRow(),0)->data(Qt::DisplayRole).toLongLong():-1;
  table_->setSortingEnabled(false);table_->setRowCount(peers.size());
  for(int i=0;i<peers.size();++i){
   const auto p=peers[i].toObject();
   const auto put=[&](int col,QVariant value){auto* item=new QTableWidgetItem;item->setData(Qt::DisplayRole,value);item->setTextAlignment(col==0||col>=5?Qt::AlignRight|Qt::AlignVCenter:Qt::AlignLeft|Qt::AlignVCenter);table_->setItem(i,col,item);};
   put(0,p.value("id").toInteger());put(1,p.value("inbound").toBool()?"Inbound":"Outbound");put(2,p.value("addr").toString());put(3,p.value("subver").toString());put(4,QDateTime::fromSecsSinceEpoch(p.value("conntime").toInteger(),QTimeZone("UTC")).toString("yyyy-MM-dd HH:mm:ss"));
   const auto ping=p.value("pingtime");put(5,ping.isDouble()&&std::isfinite(ping.toDouble())&&ping.toDouble()>=0?QString("≈ %1 ms").arg(ping.toDouble()*1000,0,'f',1):QString("—"));put(6,p.value("bytesrecv").toInteger());put(7,p.value("bytessent").toInteger());
  }
  table_->setSortingEnabled(true);
  for(int i=0;i<table_->rowCount();++i)if(table_->item(i,0)->data(Qt::DisplayRole).toLongLong()==selected){table_->setCurrentCell(i,0);table_->selectRow(i);break;}
  haveSnapshot_=true;const auto count=peers.size()==1?QString("1 connected peer"):QString("%1 connected peers").arg(peers.size());status_->setText(count+QString(" · Updated %1 UTC").arg(QDateTime::currentDateTimeUtc().toString("HH:mm:ss")));setProperty("pgn_peers_stale",false);setProperty("pgn_peer_count",peers.size());
 }
protected:
 void showEvent(QShowEvent* event)override{QWidget::showEvent(event);refresh();}
private:
 Reader read_;QTimer timer_;QLabel* status_{};QPushButton* refresh_{};QTableWidget* table_{};bool busy_{},haveSnapshot_{};
};
}

#pragma once
#include "wallet/core_runtime.h"
#include <QWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QApplication>
#include <QClipboard>
namespace pgn::ui {
class BlockExplorerView final:public QWidget {
    pgn::wallet::CoreRuntime* runtime_;QLabel *status_,*summary_;QLineEdit* search_;QTableWidget *blocks_,*transactions_;QTreeWidget *overview_,*outputs_;QPushButton *older_,*newer_;QJsonObject selected_,snapshot_;int pageTop_{-1};bool followLatest_{true};
    void transaction(int row){
        outputs_->clear();const auto txs=selected_.value("tx").toArray();if(row<0||row>=txs.size())return;const auto tx=txs[row].toObject();new QTreeWidgetItem(outputs_,{"Transaction",tx.value("txid").toString()});new QTreeWidgetItem(outputs_,{"Size",QString::number(tx.value("size").toInteger())+" bytes"});new QTreeWidgetItem(outputs_,{"Weight",QString::number(tx.value("weight").toInteger())});
        for(const auto& v:tx.value("vout").toArray()){const auto out=v.toObject(),script=out.value("scriptPubKey").toObject();auto* item=new QTreeWidgetItem(outputs_,{QString("Output %1").arg(out.value("n").toInt()),QString::number(out.value("value").toDouble(),'f',8)+" PGN"});new QTreeWidgetItem(item,{"Address",script.value("address").toString("Unspendable data output")});const auto fields=script.value("asm").toString().split(' ');if(fields.size()>1&&fields[0]=="OP_RETURN"){const auto tag=QString::fromLatin1(QByteArray::fromHex(fields[1].toLatin1()));new QTreeWidgetItem(item,{"PGN operation",tag.left(100)});}new QTreeWidgetItem(item,{"Script",script.value("hex").toString().left(1024)});item->setExpanded(true);}
    }
    void presentBlock(QJsonObject block,QString error){
        if(!error.isEmpty()){status_->setText(error);return;}selected_=block;setProperty("pgn_explorer_selected_block",block);overview_->clear();const auto field=[this](const QString& name,const QString& value){new QTreeWidgetItem(overview_,{name,value});};
        field("Block hash",block.value("hash").toString());field("Ledger height",QString::number(block.value("height").toInteger()));field("Status",block.value("confirmations").toInteger(-1)>=0?"In the current chain":"Outside the current chain");field("Confirmations",QString::number(block.value("confirmations").toInteger()));field("Time",QDateTime::fromSecsSinceEpoch(block.value("time").toInteger()).toLocalTime().toString("yyyy-MM-dd HH:mm:ss"));field("Proof",block.value("proof_type")=="prime_gap"?"Gold / prime-gap proof":"Ticket and audit coordination");field("Previous block",block.value("previousblockhash").toString());field("Merkle root",block.value("merkleroot").toString());field("Serialized size",QString::number(block.value("size").toInteger())+" bytes");field("Weight",QString::number(block.value("weight").toInteger()));
        const auto txs=block.value("tx").toArray();transactions_->setRowCount(std::min(4096,int(txs.size())));double issued=0;
        for(int i=0;i<transactions_->rowCount();++i){const auto tx=txs[i].toObject();double output=0;for(const auto& v:tx.value("vout").toArray())output+=v.toObject().value("value").toDouble();if(i==0)issued=output;const QStringList cells{QString::number(i),tx.value("txid").toString(),QString::number(tx.value("size").toInteger()),QString::number(output,'f',8)};for(int c=0;c<cells.size();++c)transactions_->setItem(i,c,new QTableWidgetItem(cells[c]));}
        field("Coinbase outputs",QString::number(issued,'f',8)+" PGN");status_->setText("Verified native block · "+block.value("hash").toString());if(!txs.isEmpty())transaction(0);
    }
    void page(int height){followLatest_=height<0;const QPointer<BlockExplorerView> guard(this);runtime_->readBlockPage(height,[guard](QJsonObject value,QString error){if(guard)guard->presentSnapshot(value,error);});}
public:
    explicit BlockExplorerView(pgn::wallet::CoreRuntime* runtime,QWidget* parent=nullptr):QWidget(parent),runtime_(runtime){
        setObjectName("pgn-block-explorer");auto* box=new QVBoxLayout(this);box->setContentsMargins(18,14,18,14);auto* bar=new QHBoxLayout;search_=new QLineEdit;search_->setPlaceholderText("Block height or block hash");auto* find=new QPushButton("Search");auto* latest=new QPushButton("Latest blocks");bar->addWidget(search_,1);bar->addWidget(find);bar->addWidget(latest);box->addLayout(bar);status_=new QLabel("Waiting for the verified native chain");summary_=new QLabel;summary_->setWordWrap(true);box->addWidget(status_);box->addWidget(summary_);
        blocks_=new QTableWidget(0,6);blocks_->setObjectName("pgn-native-network-blocks");blocks_->setHorizontalHeaderLabels({"Ledger height","Time","Proof / operation","Transactions","Confirmations","Block hash"});blocks_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);blocks_->horizontalHeader()->setStretchLastSection(true);blocks_->verticalHeader()->hide();blocks_->setSelectionBehavior(QAbstractItemView::SelectRows);blocks_->setEditTriggers(QAbstractItemView::NoEditTriggers);box->addWidget(blocks_,1);auto* paging=new QHBoxLayout;newer_=new QPushButton("Newer");older_=new QPushButton("Older");paging->addWidget(newer_);paging->addWidget(older_);paging->addStretch();auto* copy=new QPushButton("Copy block details");paging->addWidget(copy);box->addLayout(paging);
        auto* details=new QTabWidget;overview_=new QTreeWidget;overview_->setHeaderLabels({"Block field","Value"});overview_->setColumnWidth(0,230);details->addTab(overview_,"Overview");auto* txPage=new QWidget;auto* txBox=new QVBoxLayout(txPage);transactions_=new QTableWidget(0,4);transactions_->setHorizontalHeaderLabels({"Index","Transaction ID","Bytes","Output · PGN"});transactions_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);transactions_->verticalHeader()->hide();transactions_->setEditTriggers(QAbstractItemView::NoEditTriggers);outputs_=new QTreeWidget;outputs_->setHeaderLabels({"Transaction field","Value"});outputs_->setColumnWidth(0,230);txBox->addWidget(transactions_,1);txBox->addWidget(outputs_,1);details->addTab(txPage,"Transactions and PGN outputs");box->addWidget(details,2);
        connect(blocks_,&QTableWidget::cellClicked,this,[this](int row,int){if(auto* item=blocks_->item(row,5))inspectHash(item->text());});connect(transactions_,&QTableWidget::cellClicked,this,[this](int row,int){transaction(row);});connect(find,&QPushButton::clicked,this,[this]{inspect(search_->text());});connect(search_,&QLineEdit::returnPressed,this,[this]{inspect(search_->text());});connect(latest,&QPushButton::clicked,this,[this]{pageTop_=-1;page(-1);});connect(older_,&QPushButton::clicked,this,[this]{page(std::max(0,pageTop_-25));});connect(newer_,&QPushButton::clicked,this,[this]{page(pageTop_+25);});connect(copy,&QPushButton::clicked,this,[this]{if(!selected_.isEmpty())QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(selected_).toJson(QJsonDocument::Indented)));});
    }
    void inspectHash(const QString& hash){const QPointer<BlockExplorerView> guard(this);runtime_->readBlockDetails(hash,[guard](QJsonObject block,QString error){if(guard)guard->presentBlock(block,error);});}
    void inspect(const QString& query){const auto text=query.trimmed();bool ok=false;const auto number=text.toInt(&ok);if(ok&&number>=0){const QPointer<BlockExplorerView> guard(this);runtime_->readBlockAtHeight(number,[guard](QJsonObject block,QString error){if(guard)guard->presentBlock(block,error);});}else inspectHash(text);}
    void presentSnapshot(QJsonObject value,QString error={}){
        if(!error.isEmpty()){status_->setText(error);return;}if(!followLatest_&&!value.contains("tip_height"))return;snapshot_=value;const auto rows=value.value("blocks").toArray();blocks_->setRowCount(rows.size());if(rows.size())pageTop_=rows.first().toObject().value("height").toInt();
        for(int i=0;i<rows.size();++i){const auto row=rows[i].toObject();const QStringList cells{QString::number(row.value("height").toInteger()),QDateTime::fromSecsSinceEpoch(row.value("time").toInteger()).toLocalTime().toString("MM-dd HH:mm:ss"),row.value("proof_type")=="prime_gap"?"Gold proof":"Coordination",QString::number(row.value("nTx").toInteger()),QString::number(row.value("confirmations").toInteger()),row.value("hash").toString()};for(int c=0;c<cells.size();++c)blocks_->setItem(i,c,new QTableWidgetItem(cells[c]));}
        older_->setEnabled(pageTop_>=25);newer_->setEnabled(pageTop_<value.value("height").toInt());summary_->setText(QString("Native ledger record %1 · issued %2 atomic units · coordination records are not Gold discoveries").arg(value.value("height").toInteger()).arg(value.value("minted_atomic").toString()));status_->setText("Verified current-chain block page");
    }
};
}

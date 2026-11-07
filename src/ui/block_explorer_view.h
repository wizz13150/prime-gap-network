#pragma once
#include "wallet/core_runtime.h"
#include "wallet/currency_units.h"
#include "wallet/exact_rpc_money.h"
#include "wallet/explorer_projection.h"
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
#include <QSplitter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QResizeEvent>
#include <QFrame>
#include <QRegularExpression>
namespace pgn::ui {
struct BlockExplorerServices {
 using Reply=std::function<void(QJsonObject,QString)>;
 std::function<void(QString,Reply)> block;
 std::function<void(int,Reply)> height,page;
 std::function<void(QString,QString,Reply)> transaction;
 std::function<void(QString,int,Reply)> blockTransactions;
 std::function<void(QString,QString,int,Reply)> transactionPage;
};
inline QString explorerAmountUnit(const QString& amount,const QString& unit){return amount=="—"||unit.isEmpty()?QString("—"):amount+" "+unit;}
inline QString explorerAtomicAmount(QString value,const QString& unit="PGN"){bool okay{};const auto atomic=value.toLongLong(&okay);if(!okay||QString::number(atomic)!=value||atomic<0)return "—";const auto text=wallet::formatAtomicPgn(atomic,true);return explorerAmountUnit(text,unit);}

class BlockExplorerView final:public QWidget {
    BlockExplorerServices services_;QString unit_;quint64 detailGeneration_{},pageGeneration_{};bool detailBusy_{},splitInitialized_{};QSplitter* body_{};QLabel *status_,*summary_,*chainHeight_,*goldHeight_,*minted_,*detailTitle_;QPushButton *previous_,*next_;QLineEdit* search_;QTableWidget *blocks_,*transactions_;QTreeWidget *overview_,*outputs_;QTabWidget* details_;QLabel* detailPageLabel_;QPushButton *detailPrevious_,*detailNext_;QPushButton *older_,*newer_;QJsonObject selected_,selectedTransaction_,snapshot_;int pageTop_{-1};bool followLatest_{true};
    void transactionCells(int row,const QStringList& cells){
        for(int c=0;c<cells.size();++c){auto* item=new QTableWidgetItem(cells[c]);item->setToolTip(cells[c]);if(c!=1)item->setTextAlignment(c==0?Qt::AlignCenter:Qt::AlignRight|Qt::AlignVCenter);transactions_->setItem(row,c,item);}
    }
    void transaction(int row){
        if(!selectedTransaction_.isEmpty()){transactionDetails(selectedTransaction_);return;}
        outputs_->clear();const auto txs=selected_.value("tx").toArray();if(row<0||row>=txs.size())return;const auto tx=txs[row].toObject();if(!tx.contains("vout")){inspectTransaction(tx.value("txid").toString(),selected_.value("hash").toString());return;}transactionDetails(tx);
    }
    void transactionDetails(const QJsonObject& tx){
        outputs_->clear();new QTreeWidgetItem(outputs_,{"Transaction",tx.value("txid").toString()});new QTreeWidgetItem(outputs_,{"Size",QString::number(tx.value("size").toInteger())+" bytes"});new QTreeWidgetItem(outputs_,{"Weight",QString::number(tx.value("weight").toInteger())});
        const auto inputs=tx.value("vin").toArray();
        for(int i=0;i<inputs.size();++i){const auto in=inputs[i].toObject();
            auto* input=new QTreeWidgetItem(outputs_,{QString("Input %1").arg(in.contains("n")?in.value("n").toInteger():i),in.contains("coinbase")?QString("Coinbase"):in.value("txid").toString()+":"+QString::number(in.value("vout").toInt())});
            if(in.contains("txid")){input->setData(0,Qt::UserRole,in.value("txid"));input->setForeground(1,QColor("#57dfcf"));input->setToolTip(1,"Double-click to open this input transaction");}
        }
        for(const auto& v:tx.value("vout").toArray()){const auto out=v.toObject(),script=out.value("scriptPubKey").toObject();auto* item=new QTreeWidgetItem(outputs_,{QString("Output %1").arg(out.value("n").toInt()),(wallet::exactAtomicField(out,"value")?explorerAmountUnit(wallet::formatAtomicPgn(*wallet::exactAtomicField(out,"value")),unit_):QString("—"))});new QTreeWidgetItem(item,{"Address",script.value("address").toString("Unspendable data output")});const auto fields=script.value("asm").toString().split(' ');if(fields.size()>1&&fields[0]=="OP_RETURN"){const auto tag=QString::fromLatin1(QByteArray::fromHex(fields[1].toLatin1()));new QTreeWidgetItem(item,{"PGN operation",tag.left(100)});}if(script.contains("hex_preview")){new QTreeWidgetItem(item,{QString("Script preview · %1 bytes total").arg(script.value("bytes").toInteger()),script.value("hex_preview").toString()+(script.value("truncated").toBool()?"…":"")});}else new QTreeWidgetItem(item,{"Script",script.value("hex").toString().left(1024)});item->setExpanded(true);}
    }
    void presentBlock(QJsonObject block,QString error){
        if(!error.isEmpty()){status_->setText(error);return;}selectedTransaction_={};setProperty("pgn_explorer_selected_transaction",QJsonObject{});selected_=block;detailTitle_->setText(QString("Block %1 · %2").arg(block.value("height").toInteger()).arg(block.value("proof_type")=="prime_gap"?"Gold":"Coordination"));previous_->setEnabled(!block.value("previousblockhash").toString().isEmpty());next_->setEnabled(!block.value("nextblockhash").toString().isEmpty());setProperty("pgn_explorer_selected_block",block);overview_->clear();overview_->setHeaderLabels({"Block field","Value"});const auto field=[this](const QString& name,const QString& value){new QTreeWidgetItem(overview_,{name,value});};
        field("Block hash",block.value("hash").toString());field("Ledger height",QString::number(block.value("height").toInteger()));field("Status",block.value("confirmations").toInteger(-1)>=0?"In the current chain":"Outside the current chain");field("Confirmations",QString::number(block.value("confirmations").toInteger()));field("Time",QDateTime::fromSecsSinceEpoch(block.value("time").toInteger()).toLocalTime().toString("yyyy-MM-dd HH:mm:ss"));field("Proof",block.value("proof_type")=="prime_gap"?"Gold / prime-gap proof":"Ticket and audit coordination");field("Previous block",block.value("previousblockhash").toString());field("Merkle root",block.value("merkleroot").toString());field("Serialized size",QString::number(block.value("size").toInteger())+" bytes");field("Weight",QString::number(block.value("weight").toInteger()));
        const auto txs=block.value("tx").toArray();transactions_->setRowCount(std::min(4096,int(txs.size())));std::optional<qint64> issued;
        for(int i=0;i<transactions_->rowCount();++i){const auto tx=txs[i].toObject();const auto output=wallet::explorerOutputTotal(tx);if(i==0)issued=output;const QStringList cells{QString::number(i+block.value("tx_offset").toInt()),tx.value("txid").toString(),QString::number(tx.value("size").toInteger()),output?wallet::formatAtomicPgn(*output):QString("—")};transactionCells(i,cells);}
        if(block.contains("coinbase_output_atomic"))issued=wallet::explorerAtomicString(block.value("coinbase_output_atomic"));field("Coinbase outputs",issued?explorerAmountUnit(wallet::formatAtomicPgn(*issued),unit_):QString("—"));{const QSignalBlocker selection(blocks_);for(int row=0;row<blocks_->rowCount();++row)if(blocks_->item(row,5)&&blocks_->item(row,5)->text()==block.value("hash").toString()){blocks_->selectRow(row);break;}}status_->setText("Verified native block · "+block.value("hash").toString());updateDetailPager();if(!txs.isEmpty()&&txs.first().toObject().contains("vout"))transaction(0);else outputs_->clear();
    }
    void presentTransaction(QJsonObject tx,QString error){
        if(!error.isEmpty()){status_->setText(error);return;}
        const bool same=selectedTransaction_.value("txid")==tx.value("txid");selected_={};selectedTransaction_=tx;setProperty("pgn_explorer_selected_block",QJsonObject{});setProperty("pgn_explorer_selected_transaction",tx);
        {const QSignalBlocker selection(blocks_);blocks_->clearSelection();}
        detailTitle_->setText("Transaction");previous_->setEnabled(false);next_->setEnabled(false);overview_->clear();overview_->setHeaderLabels({"Transaction field","Value"});
        const auto field=[this](const QString& name,const QString& value){return new QTreeWidgetItem(overview_,{name,value});};
        field("Transaction ID",tx.value("txid").toString());
        const auto hash=tx.value("blockhash").toString();const auto confirmations=tx.value("confirmations").toInteger();
        field("Status",hash.isEmpty()?"Unconfirmed / in mempool":tx.value("in_active_chain")==false||confirmations<0?"Outside the current chain":"Confirmed in a block");
        field("Confirmations",QString::number(confirmations));
        if(!hash.isEmpty()){auto* item=field("Block hash",hash);item->setData(0,Qt::UserRole,hash);item->setForeground(1,QColor("#57dfcf"));item->setToolTip(1,"Double-click to open this block");}
        field("Serialized size",QString::number(tx.value("size").toInteger())+" bytes");
        field("Virtual size",QString::number(tx.value("vsize").toInteger())+" vbytes");field("Weight",QString::number(tx.value("weight").toInteger()));
        field("Version",QString::number(tx.value("version").toInteger()));field("Lock time",QString::number(tx.value("locktime").toInteger()));
        const auto amount=wallet::explorerOutputTotal(tx);field("Total outputs",amount?explorerAmountUnit(wallet::formatAtomicPgn(*amount),unit_):QString("—"));
        transactions_->setRowCount(1);const QStringList cells{"0",tx.value("txid").toString(),QString::number(tx.value("size").toInteger()),amount?wallet::formatAtomicPgn(*amount):QString("—")};
        transactionCells(0,cells);transactionDetails(tx);updateDetailPager();if(!same)details_->setCurrentIndex(0);
        status_->setText("Native transaction · "+tx.value("txid").toString());
    }
    void updateDetailPager(){
        const bool isTx=!selectedTransaction_.isEmpty();const auto value=isTx?selectedTransaction_:selected_;
        const int offset=value.value(isTx?"io_offset":"tx_offset").toInt();const int total=isTx?std::max(value.value("vin_total").toInt(value.value("vin").toArray().size()),value.value("vout_total").toInt(value.value("vout").toArray().size())):value.value("tx_total").toInt(value.value("tx").toArray().size());
        detailPageLabel_->setText(isTx?QString("Inputs %1 · outputs %2 · rows %3–%4").arg(value.value("vin_total").toInt(value.value("vin").toArray().size())).arg(value.value("vout_total").toInt(value.value("vout").toArray().size())).arg(total?offset+1:0).arg(std::min(total,offset+25)):QString("Transactions %1–%2 of %3").arg(total?offset+1:0).arg(std::min(total,offset+25)).arg(total));
        const bool paged=value.contains(isTx?"io_offset":"tx_offset");detailPrevious_->setEnabled(paged&&offset>0);detailNext_->setEnabled(paged&&offset+25<total);
    }
    void turnDetailPage(int direction){
        if(!selectedTransaction_.isEmpty()){const auto tx=selectedTransaction_;inspectTransaction(tx.value("txid").toString(),tx.value("blockhash").toString(),std::max(0,tx.value("io_offset").toInt()+direction*25));return;}
        if(!services_.blockTransactions||selected_.isEmpty())return;const auto hash=selected_.value("hash").toString();const int offset=std::max(0,selected_.value("tx_offset").toInt()+direction*25);const auto token=++detailGeneration_;detailBusy_=true;const QPointer<BlockExplorerView> guard(this);
        services_.blockTransactions(hash,offset,[guard,token](QJsonObject block,QString error){if(guard&&guard->detailGeneration_==token){guard->detailBusy_=false;guard->presentBlock(block,error);}});
    }
    void transactionRequest(const QString& txid,const QString& blockHash,quint64 token,int offset=0){
        if(!services_.transaction&&!services_.transactionPage){detailBusy_=false;status_->setText("Transaction lookup is unavailable");return;}
        status_->setText("Loading transaction…");const QPointer<BlockExplorerView> guard(this);
        auto consume=[guard,token](QJsonObject tx,QString error){if(guard&&guard->detailGeneration_==token){guard->detailBusy_=false;guard->presentTransaction(tx,error);}};
        if(services_.transactionPage)services_.transactionPage(txid,blockHash,offset,std::move(consume));else if(offset==0)services_.transaction(txid,blockHash,std::move(consume));else{detailBusy_=false;status_->setText("This Core cannot page transaction details");}
    }
    void page(int height){followLatest_=height<0;const auto token=++pageGeneration_;const QPointer<BlockExplorerView> guard(this);status_->setText("Loading block page…");services_.page(height,[guard,token](QJsonObject value,QString error){if(guard&&guard->pageGeneration_==token)guard->presentSnapshot(value,error);});}
protected:
    void resizeEvent(QResizeEvent* event)override{QWidget::resizeEvent(event);const auto orientation=width()<1120?Qt::Vertical:Qt::Horizontal;if(!splitInitialized_||body_->orientation()!=orientation){body_->setOrientation(orientation);body_->setSizes(orientation==Qt::Horizontal?QList<int>{width()*42/100,width()*58/100}:QList<int>{height()*42/100,height()*58/100});splitInitialized_=true;}}

public:
    explicit BlockExplorerView(pgn::wallet::CoreRuntime* runtime,QWidget* parent=nullptr):BlockExplorerView(BlockExplorerServices{
      [runtime](QString hash,auto done){runtime->readBlockDetails(hash,std::move(done));},
      [runtime](int height,auto done){runtime->readBlockAtHeight(height,std::move(done));},
      [runtime](int height,auto done){runtime->readBlockPage(height,std::move(done));},
      [runtime](QString txid,QString blockHash,auto done){runtime->readTransactionDetails(txid,blockHash,std::move(done));},
      [runtime](QString hash,int offset,auto done){runtime->readBlockTransactions(hash,offset,std::move(done));},
      [runtime](QString id,QString hash,int offset,auto done){runtime->readTransactionPage(id,hash,offset,std::move(done));}},parent,runtime->chainName()){}
    explicit BlockExplorerView(BlockExplorerServices services,QWidget* parent=nullptr,QString chain="main"):QWidget(parent),services_(std::move(services)),unit_(wallet::currencyTickerForChain(chain.toStdString())){
        setObjectName("pgn-block-explorer");auto* box=new QVBoxLayout(this);box->setContentsMargins(18,14,18,14);auto* heading=new QLabel("BLOCK EXPLORER");heading->setStyleSheet("font-size:18px;font-weight:600;color:#e5f5f5");box->addWidget(heading);auto* bar=new QHBoxLayout;search_=new QLineEdit;search_->setObjectName("pgn-explorer-search");search_->setPlaceholderText("Block height, block hash or transaction ID");auto* find=new QPushButton("Search");find->setObjectName("pgn-explorer-find");auto* latest=new QPushButton("Latest blocks");latest->setObjectName("pgn-explorer-latest");bar->addWidget(search_,1);bar->addWidget(find);bar->addWidget(latest);box->addLayout(bar);status_=new QLabel("Waiting for the verified native chain");status_->setObjectName("pgn-explorer-status");status_->setWordWrap(true);summary_=new QLabel;summary_->setWordWrap(true);box->addWidget(status_);auto* facts=new QHBoxLayout;const auto metric=[facts](const QString& caption,const char* name){auto* panel=new QFrame;panel->setObjectName("pgn-explorer-fact");panel->setStyleSheet("QFrame#pgn-explorer-fact{background:#071e25;border:1px solid #21515c;border-radius:9px;}");auto* layout=new QVBoxLayout(panel);layout->setContentsMargins(14,8,14,9);auto* title=new QLabel(caption);title->setStyleSheet("color:#88bbc6;font-size:11px");auto* value=new QLabel("—");value->setObjectName(name);value->setStyleSheet("color:#e6f7f7;font-size:20px;font-weight:600");layout->addWidget(title);layout->addWidget(value);facts->addWidget(panel,1);return value;};chainHeight_=metric("LEDGER HEIGHT","pgn-explorer-ledger-height");goldHeight_=metric("GOLD BLOCKS","pgn-explorer-gold-height");minted_=metric("COINS ISSUED","pgn-explorer-issued");box->addLayout(facts);summary_->setStyleSheet("color:#88bbc6");box->addWidget(summary_);body_=new QSplitter(Qt::Horizontal);body_->setChildrenCollapsible(false);body_->setHandleWidth(10);auto* listPage=new QWidget;listPage->setMinimumWidth(460);auto* listBox=new QVBoxLayout(listPage);listBox->setContentsMargins(0,0,0,0);
        blocks_=new QTableWidget(0,6);blocks_->setObjectName("pgn-native-network-blocks");blocks_->setHorizontalHeaderLabels({"Ledger height","Time","Proof / operation","Transactions","Confirmations","Block hash"});blocks_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);blocks_->horizontalHeader()->setStretchLastSection(true);blocks_->verticalHeader()->hide();blocks_->setSelectionBehavior(QAbstractItemView::SelectRows);blocks_->setEditTriggers(QAbstractItemView::NoEditTriggers);blocks_->setColumnHidden(5,true);blocks_->setAlternatingRowColors(true);blocks_->setSelectionMode(QAbstractItemView::SingleSelection);blocks_->verticalHeader()->setDefaultSectionSize(30);blocks_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);listBox->addWidget(blocks_,1);auto* paging=new QHBoxLayout;newer_=new QPushButton("Newer");older_=new QPushButton("Older");older_->setObjectName("pgn-explorer-older");newer_->setObjectName("pgn-explorer-newer");paging->addWidget(newer_);paging->addWidget(older_);paging->addStretch();auto* copy=new QPushButton("Copy details");paging->addWidget(copy);listBox->addLayout(paging);
        auto* detailPage=new QWidget;auto* detailBox=new QVBoxLayout(detailPage);detailBox->setContentsMargins(0,0,0,0);auto* detailBar=new QHBoxLayout;detailTitle_=new QLabel("Select a block");detailTitle_->setObjectName("pgn-explorer-detail-title");detailTitle_->setStyleSheet("font-size:15px;font-weight:600");previous_=new QPushButton("← Previous");next_=new QPushButton("Next →");previous_->setObjectName("pgn-explorer-previous-block");next_->setObjectName("pgn-explorer-next-block");previous_->setEnabled(false);next_->setEnabled(false);detailBar->addWidget(detailTitle_,1);detailBar->addWidget(previous_);detailBar->addWidget(next_);detailBox->addLayout(detailBar);auto* details=details_=new QTabWidget;details->setObjectName("pgn-explorer-detail-tabs");overview_=new QTreeWidget;overview_->setObjectName("pgn-explorer-overview");overview_->setHeaderLabels({"Block field","Value"});overview_->setColumnWidth(0,230);details->addTab(overview_,"Overview");auto* txPage=new QWidget;auto* txBox=new QVBoxLayout(txPage);auto* pager=new QHBoxLayout;detailPageLabel_=new QLabel;detailPageLabel_->setObjectName("pgn-explorer-detail-page-label");detailPrevious_=new QPushButton("Previous page");detailPrevious_->setObjectName("pgn-explorer-detail-page-previous");detailNext_=new QPushButton("Next page");detailNext_->setObjectName("pgn-explorer-detail-page-next");detailPrevious_->setEnabled(false);detailNext_->setEnabled(false);pager->addWidget(detailPageLabel_,1);pager->addWidget(detailPrevious_);pager->addWidget(detailNext_);txBox->addLayout(pager);connect(detailPrevious_,&QPushButton::clicked,this,[this]{turnDetailPage(-1);});connect(detailNext_,&QPushButton::clicked,this,[this]{turnDetailPage(1);});transactions_=new QTableWidget(0,4);transactions_->setObjectName("pgn-explorer-transactions");transactions_->setHorizontalHeaderLabels({"Index","Transaction ID","Bytes","Output · "+(unit_.isEmpty()?QString("—"):unit_)});transactions_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);transactions_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);transactions_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);transactions_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::ResizeToContents);transactions_->verticalHeader()->hide();transactions_->setEditTriggers(QAbstractItemView::NoEditTriggers);outputs_=new QTreeWidget;outputs_->setObjectName("pgn-explorer-outputs");outputs_->setHeaderLabels({"Transaction field","Value"});outputs_->setColumnWidth(0,230);txBox->addWidget(transactions_,1);txBox->addWidget(outputs_,1);details->addTab(txPage,"Transactions and "+(unit_.isEmpty()?QString("—"):unit_)+" outputs");detailBox->addWidget(details,1);body_->addWidget(listPage);body_->addWidget(detailPage);body_->setStretchFactor(0,2);body_->setStretchFactor(1,3);box->addWidget(body_,1);
        connect(previous_,&QPushButton::clicked,this,[this]{inspectHash(selected_.value("previousblockhash").toString());});connect(next_,&QPushButton::clicked,this,[this]{inspectHash(selected_.value("nextblockhash").toString());});connect(overview_,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem* item,int){const auto hash=item->data(0,Qt::UserRole).toString();if(!hash.isEmpty())inspectHash(hash);else QApplication::clipboard()->setText(item->text(1));});connect(blocks_,&QTableWidget::cellClicked,this,[this](int row,int){if(auto* item=blocks_->item(row,5))inspectHash(item->text());});connect(transactions_,&QTableWidget::cellClicked,this,[this](int row,int){transaction(row);});connect(outputs_,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem* item,int){const auto id=item->data(0,Qt::UserRole).toString();if(!id.isEmpty())inspectTransaction(id);});connect(find,&QPushButton::clicked,this,[this]{inspect(search_->text());});connect(search_,&QLineEdit::returnPressed,this,[this]{inspect(search_->text());});connect(latest,&QPushButton::clicked,this,[this]{pageTop_=-1;page(-1);});connect(older_,&QPushButton::clicked,this,[this]{if(auto* last=blocks_->item(blocks_->rowCount()-1,0))page(std::max(0,last->text().toInt()-1));});connect(newer_,&QPushButton::clicked,this,[this]{page(pageTop_+25);});connect(copy,&QPushButton::clicked,this,[this]{const auto value=selectedTransaction_.isEmpty()?selected_:selectedTransaction_;if(!value.isEmpty())QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Indented)));});
    }
    void inspectHash(const QString& hash){const auto token=++detailGeneration_;detailBusy_=true;status_->setText("Loading block details…");const QPointer<BlockExplorerView> guard(this);services_.block(hash.trimmed().toLower(),[guard,token](QJsonObject block,QString error){if(guard&&guard->detailGeneration_==token){guard->detailBusy_=false;guard->presentBlock(block,error);}});}
    void inspectTransaction(const QString& txid,const QString& blockHash={},int offset=0){const auto token=++detailGeneration_;detailBusy_=true;transactionRequest(txid.trimmed().toLower(),blockHash.toLower(),token,offset);}
    void inspect(const QString& query){const auto text=query.trimmed();bool ok=false;const auto number=text.toInt(&ok);if(ok&&number>=0){const auto token=++detailGeneration_;detailBusy_=true;status_->setText("Loading block details…");const QPointer<BlockExplorerView> guard(this);services_.height(number,[guard,token](QJsonObject block,QString error){if(guard&&guard->detailGeneration_==token){guard->detailBusy_=false;guard->presentBlock(block,error);}});}else{
        const auto hash=text.toLower();const auto token=++detailGeneration_;
        static const QRegularExpression id("^[0-9a-f]{64}$");if(!id.match(hash).hasMatch()){detailBusy_=false;status_->setText("Enter a block height or a 64-character block / transaction ID");return;}
        detailBusy_=true;status_->setText("Looking up block or transaction…");const QPointer<BlockExplorerView> guard(this);
        services_.block(hash,[guard,token,hash](QJsonObject block,QString error){if(!guard||guard->detailGeneration_!=token)return;
            if(error.startsWith("Core RPC error (-5):")){guard->transactionRequest(hash,{},token);return;}
            guard->detailBusy_=false;guard->presentBlock(block,error);
        });
    }}
    void presentSnapshot(QJsonObject value,QString error={}){
        if(!error.isEmpty()){status_->setText(error);return;}if(!followLatest_&&!value.contains("tip_height"))return;
        const auto rows=value.value("blocks").toArray();if(rows.size()>25){status_->setText("Block page exceeds its verified bound");return;}
        snapshot_=value;const auto selectedHash=selected_.value("hash").toString();const QSignalBlocker blockSignals(blocks_);const int scroll=blocks_->verticalScrollBar()->value();blocks_->setUpdatesEnabled(false);blocks_->setRowCount(rows.size());if(!rows.isEmpty())pageTop_=rows.first().toObject().value("height").toInt();int selectedRow=-1;
        for(int i=0;i<rows.size();++i){const auto row=rows[i].toObject();const QString hash=row.value("hash").toString();const QStringList cells{QString::number(row.value("height").toInteger()),QDateTime::fromSecsSinceEpoch(row.value("time").toInteger()).toLocalTime().toString("MM-dd HH:mm:ss"),row.value("proof_type")=="prime_gap"?"Gold proof":"Coordination",QString::number(row.value("nTx").toInteger()),QString::number(row.value("confirmations").toInteger()),hash};for(int c=0;c<cells.size();++c){auto* item=blocks_->item(i,c);if(!item){item=new QTableWidgetItem;blocks_->setItem(i,c,item);}if(item->text()!=cells[c])item->setText(cells[c]);if(c==2)item->setForeground(row.value("proof_type")=="prime_gap"?QColor("#ffd35c"):QColor("#9fc8d3"));}if(hash==selectedHash)selectedRow=i;}
        if(selectedRow>=0)blocks_->selectRow(selectedRow);else blocks_->clearSelection();blocks_->verticalScrollBar()->setValue(scroll);blocks_->setUpdatesEnabled(true);
        const int bottom=rows.isEmpty()?0:rows.last().toObject().value("height").toInt();older_->setEnabled(!rows.isEmpty()&&bottom>0);newer_->setEnabled(pageTop_<value.value("height").toInt());chainHeight_->setText(QString::number(value.value("height").toInteger()));goldHeight_->setText(value.contains("gold_blocks")?QString::number(value.value("gold_blocks").toInteger()):"—");minted_->setText(explorerAtomicAmount(value.value("minted_atomic").toString(),unit_));summary_->setText("Gold closes a mining epoch. Coordination records carry tickets and audits.");if(selectedTransaction_.isEmpty()&&!detailBusy_)status_->setText("Verified chain snapshot · "+value.value("observed_utc").toString());
        if(selected_.isEmpty()&&selectedTransaction_.isEmpty()&&!detailBusy_&&!rows.isEmpty())inspectHash(rows.first().toObject().value("hash").toString());
    }
};
}

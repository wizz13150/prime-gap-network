#include "ui/wallet_core_view.h"
#include "ui/wallet_setup_wizard.h"
#include "wallet/setup_deployment.h"
#include "wallet/core_bundle.h"
#include "wallet/runtime_paths.h"
#include "ui/audit_explorer_view.h"
#include "ui/block_explorer_view.h"
#include "ui/vanity_view.h"
#include "wallet/core_runtime.h"
#include "wallet/difficulty_history.h"
#include "ui/difficulty_history_view.h"
#include "ui/render/horizon_coin_navigation.h"
#include <QWidget>
#include <QWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QFrame>
#include <QTabWidget>
#include <QDateTime>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QFormLayout>
#include <QGridLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QClipboard>
#include <QScrollArea>
#include <QLocale>
#include <QTimer>
#include <QPointer>
#include <QStackedWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QLibrary>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace pgn::ui {
namespace {
#ifdef Q_OS_WIN
void hideOwnedCoreWindows(qint64 pid){
 EnumWindows([](HWND window,LPARAM value)->BOOL{
  DWORD owner=0;GetWindowThreadProcessId(window,&owner);
  if(owner==static_cast<DWORD>(value)&&GetWindow(window,GW_OWNER)==nullptr&&IsWindowVisible(window))ShowWindowAsync(window,SW_HIDE);
  return TRUE;
 },static_cast<LPARAM>(pid));
}
#endif
// The wallet is a native Horizon widget tree. The separately owned Core remains
// the authority for balances, keys, addresses and transactions; no foreign HWND
// is used as a substitute for an integrated interface.
class WalletAccountPanel final:public QWidget {
public:
 explicit WalletAccountPanel(pgn::wallet::CoreRuntime* runtime,QWidget* parent):QWidget(parent),runtime_(runtime){
  setObjectName("pgn-wallet-account");auto* box=new QVBoxLayout(this);box->setContentsMargins(20,12,20,16);box->setSpacing(12);
  account_=new QLabel("Reading the verified wallet…");account_->setObjectName("pgn-wallet-account-status");account_->setWordWrap(true);box->addWidget(account_);
  tabs_=new QTabWidget;auto* overview=new QWidget;auto* overviewBox=new QVBoxLayout(overview);
  auto* cards=new QGridLayout;
  for(int i=0;i<3;++i){auto* frame=new QFrame;frame->setStyleSheet("QFrame{background:#08262d;border:1px solid #256573;border-radius:9px;}QLabel{border:0;background:transparent;}");auto* card=new QVBoxLayout(frame);
   card->addWidget(new QLabel(QStringList{"AVAILABLE","PENDING","IMMATURE"}[i]));balances_[i]=new QLabel("— PGN");balances_[i]->setStyleSheet("font:700 23px monospace;color:#5ff2ce");card->addWidget(balances_[i]);cards->addWidget(frame,0,i);}
  overviewBox->addLayout(cards);auto* controls=new QHBoxLayout;auto* refresh=new QPushButton("Refresh wallet");auto* unlock=new QPushButton("Unlock…");auto* lock=new QPushButton("Lock wallet");controls->addWidget(refresh);controls->addWidget(unlock);controls->addWidget(lock);controls->addStretch();overviewBox->addLayout(controls);overviewBox->addStretch();tabs_->addTab(overview,"Balances");
  auto* sendPage=new QWidget;auto* sendBox=new QFormLayout(sendPage);recipient_=new QLineEdit;recipient_->setPlaceholderText("Recipient PGN address");amount_=new QLineEdit;amount_->setPlaceholderText("Amount in PGN");send_=new QPushButton("Review payment…");send_->setEnabled(false);sendBox->addRow("Recipient",recipient_);sendBox->addRow("Amount",amount_);sendBox->addRow(send_);tabs_->addTab(sendPage,"Send");
  auto* receivePage=new QWidget;auto* receiveBox=new QVBoxLayout(receivePage);auto* receiveRow=new QHBoxLayout;label_=new QLineEdit;label_->setPlaceholderText("Optional address label");auto* generate=new QPushButton("Create receiving address");receiveRow->addWidget(label_,1);receiveRow->addWidget(generate);receiveBox->addLayout(receiveRow);auto* addressRow=new QHBoxLayout;address_=new QLineEdit;address_->setReadOnly(true);address_->setPlaceholderText("Select an address below, or create one");auto* copy=new QPushButton("Copy address");addressRow->addWidget(address_,1);addressRow->addWidget(copy);receiveBox->addLayout(addressRow);
  addresses_=table({"Label","Receiving address","Received · PGN"});receiveBox->addWidget(addresses_,1);tabs_->addTab(receivePage,"Receive");
  transactions_=table({"Date","Type","Address","Amount · PGN","Confirmations"});tabs_->addTab(transactions_,"History");box->addWidget(tabs_,1);
  connect(refresh,&QPushButton::clicked,this,[this]{refreshWallet();});connect(copy,&QPushButton::clicked,this,[this]{if(!address_->text().isEmpty())QGuiApplication::clipboard()->setText(address_->text());});
  connect(addresses_,&QTableWidget::cellClicked,this,[this](int row,int){if(auto* item=addresses_->item(row,1))address_->setText(item->text());});
  connect(generate,&QPushButton::clicked,this,[this,generate]{generate->setEnabled(false);const QPointer<WalletAccountPanel> weak(this);runtime_->walletRequest("getnewaddress",{label_->text(),"bech32"},[weak,generate](QJsonValue value,QString error){if(!weak)return;generate->setEnabled(true);if(!error.isEmpty())return weak->showError(error);if(!value.isString()||value.toString().isEmpty())return weak->showError("Core did not return an address");weak->address_->setText(value.toString());weak->refreshWallet();});});
  connect(unlock,&QPushButton::clicked,this,[this]{bool okay=false;QString secret=QInputDialog::getText(this,"Unlock PGN wallet","Wallet passphrase",QLineEdit::Password,{},&okay);if(!okay||secret.isEmpty())return;const QPointer<WalletAccountPanel> weak(this);runtime_->walletRequest("walletpassphrase",{secret,60},[weak](QJsonValue,QString error){if(!weak)return;if(!error.isEmpty())weak->showError(error);weak->refreshWallet();});secret.fill(QChar(0));});
  connect(lock,&QPushButton::clicked,this,[this]{const QPointer<WalletAccountPanel> weak(this);runtime_->walletRequest("walletlock",{},[weak](QJsonValue,QString error){if(!weak)return;if(!error.isEmpty())weak->showError(error);weak->refreshWallet();});});
  connect(send_,&QPushButton::clicked,this,[this]{reviewPayment();});clock_.setInterval(5000);connect(&clock_,&QTimer::timeout,this,[this]{if(isVisible())refreshWallet();});clock_.start();
 }
 int pageCount()const{return tabs_->count();}
 void refreshWallet(){
  if(pending_||!runtime_->connected())return;pending_=4;const QPointer<WalletAccountPanel> weak(this);
  runtime_->walletRequest("getwalletinfo",{},[weak](QJsonValue value,QString error){if(!weak)return;--weak->pending_;if(!error.isEmpty()){weak->account_->setText(error);weak->send_->setEnabled(false);return;}const auto data=value.toObject();const bool encrypted=data.contains("unlocked_until");const bool locked=encrypted&&data.value("unlocked_until").toInteger()==0;
   weak->account_->setText(QString("Wallet %1 · %2 · %3 transactions").arg(data.value("walletname").toString(),locked?"Locked":encrypted?"Unlocked":"Not encrypted").arg(data.value("txcount").toInteger()));weak->setProperty("pgn_wallet_account_observed",true);
  });
  runtime_->walletRequest("getbalances",{},[weak](QJsonValue value,QString error){if(!weak)return;--weak->pending_;if(!error.isEmpty()){weak->account_->setText(error);weak->send_->setEnabled(false);return;}const auto mine=value.toObject().value("mine").toObject();const QStringList keys{"trusted","untrusted_pending","immature"};
   for(int i=0;i<3;++i)weak->balances_[i]->setText(mine.value(keys[i]).isDouble()?QString::number(mine.value(keys[i]).toDouble(),'f',10)+" PGN":"— PGN");
   weak->available_=mine.value("trusted").isDouble()?mine.value("trusted").toDouble():0;weak->send_->setEnabled(weak->available_>0);
  });
  runtime_->walletRequest("listtransactions",{"*",50,0,true},[weak](QJsonValue value,QString error){if(!weak)return;--weak->pending_;if(!error.isEmpty())return;const auto rows=value.toArray();weak->transactions_->setRowCount(rows.size());for(int i=0;i<rows.size();++i){const auto v=rows[rows.size()-1-i].toObject();const QStringList columns{QDateTime::fromSecsSinceEpoch(v.value("time").toInteger()).toLocalTime().toString("yyyy-MM-dd HH:mm"),v.value("category").toString(),v.value("address").toString(),QString::number(v.value("amount").toDouble(),'f',10),QString::number(v.value("confirmations").toInteger())};for(int c=0;c<columns.size();++c)weak->transactions_->setItem(i,c,new QTableWidgetItem(columns[c]));}});
  runtime_->walletRequest("listreceivedbyaddress",{0,true,true},[weak](QJsonValue value,QString error){if(!weak)return;--weak->pending_;if(!error.isEmpty())return;const auto rows=value.toArray();weak->addresses_->setRowCount(rows.size());for(int i=0;i<rows.size();++i){const auto v=rows[i].toObject();const QStringList columns{v.value("label").toString(),v.value("address").toString(),QString::number(v.value("amount").toDouble(),'f',10)};for(int c=0;c<columns.size();++c)weak->addresses_->setItem(i,c,new QTableWidgetItem(columns[c]));}});
 }
private:
 static QTableWidget* table(const QStringList& labels){auto* out=new QTableWidget(0,labels.size());out->setHorizontalHeaderLabels(labels);out->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);out->horizontalHeader()->setStretchLastSection(true);out->verticalHeader()->hide();out->setEditTriggers(QAbstractItemView::NoEditTriggers);out->setSelectionBehavior(QAbstractItemView::SelectRows);return out;}
 void showError(const QString& error){QMessageBox::warning(this,"PGN wallet",error.left(500));}
 void reviewPayment(){
  const auto recipient=recipient_->text().trimmed(),amount=amount_->text().trimmed();static const QRegularExpression decimal("^(0|[1-9][0-9]*)(\\.[0-9]{1,10})?$");bool okay=false;const double number=QLocale::c().toDouble(amount,&okay);
  if(recipient.isEmpty()||!decimal.match(amount).hasMatch()||!okay||number<=0||number>available_)return showError("Enter a recipient and a positive amount within the available balance.");
  send_->setEnabled(false);const QPointer<WalletAccountPanel> weak(this);runtime_->walletRequest("validateaddress",{recipient},[weak,recipient,amount](QJsonValue value,QString error){if(!weak)return;weak->send_->setEnabled(weak->available_>0);if(!error.isEmpty())return weak->showError(error);if(!value.toObject().value("isvalid").toBool())return weak->showError("Core rejected this PGN address.");
   if(QMessageBox::question(weak,"Confirm PGN payment","Send "+amount+" PGN to\n"+recipient+"?\nThe native wallet determines the transaction fee.",QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
   weak->send_->setEnabled(false);weak->runtime_->walletRequest("sendtoaddress",{recipient,amount},[weak](QJsonValue result,QString failure){if(!weak)return;weak->send_->setEnabled(weak->available_>0);if(!failure.isEmpty())return weak->showError(failure);QMessageBox::information(weak,"Payment submitted","Transaction: "+result.toString());weak->refreshWallet();});
  });
 }
 pgn::wallet::CoreRuntime* runtime_{};QLabel* account_{};QLabel* balances_[3]{};QTabWidget* tabs_{};QLineEdit *recipient_{},*amount_{},*label_{},*address_{};QPushButton* send_{};QTableWidget *addresses_{},*transactions_{};QTimer clock_;int pending_{};double available_{};
};
class WalletCoreView final : public QWidget {
public:
 pgn::wallet::CoreRuntime* networkRuntime()const{return runtime_;}
 void inspectBlock(const QString& hash){networkExplorer_->inspectHash(hash); }
 explicit WalletCoreView(const QString& root,WalletObservationSink observe,bool testnet){
  setObjectName("pgn-wallet-core-view");setProperty("pgn_wallet_frontend_loaded",false);
  auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
  auto* header=new QWidget(this);auto* headerBox=new QVBoxLayout(header);headerBox->setContentsMargins(20,18,20,8);layout->addWidget(header);
  auto* heading=new QLabel("PGN WALLET",this);
  heading->setStyleSheet("font:700 22px monospace;color:#e9fffb");headerBox->addWidget(heading);
  status_=new QLabel("CORE DEPLOYMENT PENDING · no fabricated balances, node or chain state",this);
  status_->setStyleSheet("color:#ffad4a;font:10px monospace");status_->setWordWrap(true);headerBox->addWidget(status_);
  const auto configuration=pgn::wallet::coreRuntimeDirectory(root,testnet);
  const bool legacyDeployment=QFileInfo::exists(root+(testnet?"/data/testnet/ui-runtime/wallet-core.json":"/data/runtime/wallet-core.json"));
  historyFile_=legacyDeployment?root+"/data/runtime/wallet-difficulty.sqlite":configuration+"/wallet-difficulty.sqlite";
  auto* workspace=new QTabWidget;workspace->setObjectName("pgn-wallet-subtabs");auto* walletPage=new QWidget;viewport_=new QVBoxLayout(walletPage);viewport_->setContentsMargins(0,0,0,0);workspace->addTab(walletPage,"Wallet");
  runtime_=new pgn::wallet::CoreRuntime(root,this,testnet,pgn::wallet::coreRuntimeDirectory(root,testnet));networkExplorer_=new BlockExplorerView(runtime_);workspace->addTab(networkExplorer_,"Explorer");
  auto* setup=new QPushButton("Set up wallet…",header);setup->setObjectName("pgn-wallet-setup");headerBox->addWidget(setup,0,Qt::AlignLeft);
  setup->setVisible(!QFileInfo::exists(runtime_->runtimeDirectory()+"/wallet-core.json"));
  connect(setup,&QPushButton::clicked,this,[this,root,testnet,setup]{
   if(runtime_->processId()>0){status_->setText("Stop the current Core before changing wallet setup.");return;}
   setup->setEnabled(false);status_->setText("Checking PGN Core files…");
   auto* verification=new QFutureWatcher<pgn::wallet::CoreBundle>(this);
   connect(verification,&QFutureWatcher<pgn::wallet::CoreBundle>::finished,this,[this,root,testnet,setup,verification]{
    const auto bundle=verification->result();verification->deleteLater();setup->setEnabled(true);
    if(!bundle.error.isEmpty()){status_->setText(bundle.error);return;}
    const auto deployment=bundle.deployment;
    auto* wizard=new WalletSetupWizard(root,testnet?QString("PGN testnet"):QString("PGN"),[this,root,deployment,setup](const auto& choice){
     const auto selection=pgn::wallet::selectedWalletDeployment(deployment,choice,root);if(!selection.error.isEmpty())return selection.error;
     const auto error=pgn::wallet::publishWalletSetup(runtime_->runtimeDirectory(),selection);if(!error.isEmpty())return error;
     setup->hide();runtime_->launch();return QString{};
    },this);wizard->setAttribute(Qt::WA_DeleteOnClose);wizard->open();
   });
   verification->setFuture(QtConcurrent::run([root,testnet]{return pgn::wallet::readCoreBundle(root,testnet?QString("test"):QString("main"));}));
  });
  historyView_=createDifficultyHistoryView(historyFile_);layout->addWidget(workspace,1);
  auto* historyControls=new QHBoxLayout;auto* reloadHistory=new QPushButton("Retry verified history load");reloadHistory->setObjectName("pgn-wallet-difficulty-reload");
  historyControls->addWidget(reloadHistory);historyControls->addStretch();headerBox->addLayout(historyControls);
  connect(reloadHistory,&QPushButton::clicked,this,[this]{historyFailed_.clear();if(latestHistory_&&!historyBusy_)beginHistory(*latestHistory_);});
  placeholder_=new QLabel("Connecting the verified native wallet…",this);
  qobject_cast<QLabel*>(placeholder_)->setWordWrap(true);
  viewport_->addWidget(placeholder_,1);
  workspace->addTab(createVanityView(root),"Vanity");workspace->addTab(historyView_,"Difficulty history");
  workspace->addTab(new AuditExplorerView(runtime_),"Audits");
  auto* navigation=HorizonCoinNavigation(workspace,{
   {"01 WALLET","BALANCES + PAYMENTS"},{"02 NETWORK","BLOCKS + ACTIVITY"},
   {"03 VANITY","PERSONAL ADDRESS"},{"04 DIFFICULTY","CHAIN HISTORY"},{"05 AUDITS","REPORTS + VERIFICATION"}},this);
  navigation->setProperty("pgn_navigation_role","wallet");layout->insertWidget(0,navigation);

  accountPanel_=new WalletAccountPanel(runtime_,walletPage);accountPanel_->setEnabled(false);viewport_->addWidget(accountPanel_,1);placeholder_->hide();
  setProperty("pgn_core_link_state","starting");
  runtime_->onLinkState=[this](pgn::wallet::CoreLinkState state,const QString& detail){
   const auto key=pgn::wallet::coreLinkKey(state);
   setProperty("pgn_core_link_state",key);setProperty("pgn_core_link_detail",detail);
   auto history=property("pgn_core_link_transitions").toJsonArray();
   history.append(QJsonObject{{"state",key},{"detail",detail},{"observed_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
   while(history.size()>32)history.removeFirst();
   setProperty("pgn_core_link_transitions",history);
   if(state!=pgn::wallet::CoreLinkState::Ready)accountPanel_->setEnabled(false);
   else accountPanel_->setEnabled(runtime_->walletReady());
  };
  runtime_->onObservation=[this,observe=std::move(observe)](const auto& anchor){
   if(observe)observe(anchor);latestHistory_=anchor;
   if(anchor&&anchor->tipHash!=networkDone_&&!networkBusy_)refreshNetwork();
   if(anchor&&!anchor->tipHash.isEmpty()&&!historyBusy_&&historyFingerprint(*anchor)!=historyFailed_&&anchor->tipHash!=historyDone_)beginHistory(*anchor);
  };
  runtime_->onStatus=[this,setup](const QString& text){status_->setText(!setup->isHidden()?QString("Choose Set up wallet to create or open your PGN wallet."):text);status_->setStyleSheet(runtime_->connected()?"color:#69efc5;font:11px monospace":"color:#ffbd69;font:11px monospace");};
  runtime_->onReady=[this](qint64){attachClock_.start();attach();accountPanel_->setEnabled(true);accountPanel_->refreshWallet();};
  runtime_->onStopped=[this]{attachClock_.stop();accountPanel_->setEnabled(false);setProperty("pgn_wallet_embedded",false);setProperty("pgn_wallet_frontend_loaded",false);};
  attachClock_.setInterval(500);connect(&attachClock_,&QTimer::timeout,this,[this]{attach();});
  QTimer::singleShot(0,runtime_,[this]{runtime_->launch();});
 }
 ~WalletCoreView() override = default;
private:
 void refreshNetwork(){
  networkBusy_=true;runtime_->readNetworkSnapshot([this](QJsonObject state,QString error){networkBusy_=false;networkExplorer_->presentSnapshot(state,error);if(!error.isEmpty())return;networkDone_=state.value("bestblockhash").toString();setProperty("pgn_network_block_rows",state.value("blocks"));setProperty("pgn_network_leases",state.value("leases"));setProperty("pgn_network_height",state.value("height"));setProperty("pgn_network_gold_blocks",state.value("gold_blocks"));setProperty("pgn_network_ticket_width",state.value("ticket_width").toString());});
 }
 QString historyFingerprint(const pgn::wallet::ChainObservation& anchor)const{return anchor.genesis+":"+anchor.tipHash+":"+QString::number(anchor.owner.pid)+":"+anchor.owner.birth+":"+QString::number(anchor.owner.generation);}
 void failHistory(const QString& error){historyBusy_=false;if(historyAnchor_)historyFailed_=historyFingerprint(*historyAnchor_);presentDifficultyHistoryError(historyView_,error);setProperty("pgn_wallet_difficulty_source_error",error);}
 void beginHistory(const pgn::wallet::ChainObservation& anchor){if(!historyStore_){try{historyStore_=std::make_shared<pgn::wallet::DifficultyHistoryStore>(historyFile_);}catch(const std::exception& error){failHistory(QString::fromUtf8(error.what()));return;}}historyAnchor_=anchor;historyCursor_=anchor.tipHash;historyHeight_=anchor.height;historyBusy_=true;historyStep();}
 void historyStep(){
  if(!historyAnchor_)return;const auto anchor=*historyAnchor_;const auto cursor=historyCursor_;
  try{if(const auto known=historyStore_->find(anchor.genesis,cursor)){
   if(known->height!=historyHeight_){failHistory("Stored header height does not match the observed chain path");return;}
   if(known->ancestryComplete){publishHistory(anchor);return;}
   historyCursor_=known->previous;--historyHeight_;QTimer::singleShot(0,this,[this]{historyStep();});return;
  }}catch(const std::exception& error){failHistory(QString::fromUtf8(error.what()));return;}
  runtime_->readDifficultyHeader(anchor,cursor,[this,anchor,cursor](QJsonObject header,QString error,QDateTime observedUtc){
   if(!error.isEmpty()){failHistory(error);return;}
   if(header.value("height").toInteger(-1)!=historyHeight_){failHistory("Core historical header height differs from the expected parent path");return;}
   auto* write=new QFutureWatcher<QString>(this);connect(write,&QFutureWatcher<QString>::finished,this,[this,write]{const auto error=write->result();write->deleteLater();if(!error.isEmpty()){failHistory(error);return;}historyStep();});
   const auto store=historyStore_;write->setFuture(QtConcurrent::run([store,anchor,cursor,header,observedUtc]{return store->record(anchor.owner,anchor.genesis,cursor,header,observedUtc);}));
  });
 }
 void publishHistory(const pgn::wallet::ChainObservation& anchor){
  auto* publish=new QFutureWatcher<QString>(this);connect(publish,&QFutureWatcher<QString>::finished,this,[this,publish,anchor]{
   const auto error=publish->result();publish->deleteLater();if(!error.isEmpty()){failHistory(error);return;}
   historyBusy_=false;historyDone_=anchor.tipHash;historyFailed_.clear();presentDifficultyHistory(historyView_,historyStore_->summary(anchor.genesis));setProperty("pgn_wallet_difficulty_source_error",QVariant());
   if(latestHistory_&&latestHistory_->tipHash!=anchor.tipHash)beginHistory(*latestHistory_);
  });const auto store=historyStore_;publish->setFuture(QtConcurrent::run([store,anchor]{return store->activate(anchor,anchor.tipHash);}));
 }
 void attach(){
  if(!runtime_->connected())return;
#ifdef Q_OS_WIN
  hideOwnedCoreWindows(runtime_->processId());
#endif
  setProperty("pgn_wallet_embedded",accountPanel_->window()==window());setProperty("pgn_wallet_embedded_pid",runtime_->processId());setProperty("pgn_wallet_frontend_loaded",true);setProperty("pgn_wallet_genuine_page_count",accountPanel_->pageCount());
 }
 pgn::wallet::CoreRuntime* runtime_{};QLabel* status_{};QWidget* placeholder_{};
 BlockExplorerView* networkExplorer_{};bool networkBusy_{};QString networkDone_;
 QVBoxLayout* viewport_{};WalletAccountPanel* accountPanel_{};
 QTimer attachClock_;int attempts_{};

 QString historyFile_;std::shared_ptr<pgn::wallet::DifficultyHistoryStore> historyStore_;QWidget* historyView_{};
 std::optional<pgn::wallet::ChainObservation> latestHistory_,historyAnchor_;QString historyCursor_,historyDone_,historyFailed_;qint64 historyHeight_{};bool historyBusy_{};
};
}
QWidget* createWalletCoreView(const QString& root,WalletObservationSink observe,bool testnet){return new WalletCoreView(root,std::move(observe),testnet);}
void readWalletTicketPage(QWidget* widget,int offset,std::function<void(QJsonObject,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->readTicketPage(offset,std::move(consume));else consume({},"Wallet network view unavailable");}
void readWalletTicketDetails(QWidget* widget,const QString& id,std::function<void(QJsonObject,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->readTicketDetails(id,std::move(consume));else consume({},"Wallet network view unavailable");}
void readWalletAuditDetails(QWidget* widget,const QString& id,std::function<void(QJsonObject,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->readAuditDetails(id,std::move(consume));else consume({},"Wallet audit view unavailable");}
void readWalletLocalConnection(QWidget* widget,std::function<void(QJsonObject,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->readLocalConnection(std::move(consume));else consume({},"Local wallet connection unavailable");}
void readWalletSigningIdentity(QWidget* widget,const QString& address,std::function<void(QJsonValue,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->walletRequest("getaddressinfo",{address},std::move(consume));else consume({},"Wallet signing identity unavailable");}
void requestWalletAudit(QWidget* widget,const QString& method,const QJsonArray& params,std::function<void(QJsonValue,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->auditRequest(method,params,std::move(consume));else consume({},"Wallet audit view unavailable");}
void signWalletAudit(QWidget* widget,const QString& address,const QString& challenge,std::function<void(QJsonValue,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->signAuditChallenge(address,challenge,std::move(consume));else consume({},"Wallet audit view unavailable");}
void inspectWalletBlock(QWidget* widget,const QString& hash){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->inspectBlock(hash);}

}

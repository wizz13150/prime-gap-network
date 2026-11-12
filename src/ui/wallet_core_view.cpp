#include "ui/wallet_console_view.h"
#include "wallet/exact_rpc_money.h"
#include "ui/wallet_core_view.h"
#include "ui/wallet_peers_view.h"
#include "ui/wallet_management_panel.h"
#include "ui/wallet_history_view.h"
#include "ui/component_versions_dialog.h"
#include "wallet/setup_deployment.h"
#include "wallet/core_bundle.h"
#include "wallet/runtime_paths.h"
#include "ui/audit_explorer_view.h"
#include "ui/block_explorer_view.h"
#include "ui/vanity_view.h"
#include "wallet/core_runtime.h"
#include "ui/network_metrics_view.h"
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
 explicit WalletAccountPanel(pgn::wallet::CoreRuntime* runtime,NetworkMetricsHub* metrics,bool testnet,QWidget* parent):QWidget(parent),runtime_(runtime),unit_(pgn::wallet::currencyTicker(testnet)){
  setObjectName("pgn-wallet-account");auto* box=new QVBoxLayout(this);box->setContentsMargins(20,12,20,16);box->setSpacing(12);
  account_=new QLabel("Reading the verified wallet…");account_->setObjectName("pgn-wallet-account-status");account_->setWordWrap(true);box->addWidget(account_);
  tabs_=new QTabWidget;auto* overview=new QWidget;auto* overviewBox=new QVBoxLayout(overview);
  auto* cards=new QGridLayout;
  for(int i=0;i<3;++i){auto* frame=new QFrame;frame->setStyleSheet("QFrame{background:#08262d;border:1px solid #256573;border-radius:9px;}QLabel{border:0;background:transparent;}");auto* card=new QVBoxLayout(frame);
   card->addWidget(new QLabel(QStringList{"AVAILABLE","PENDING","IMMATURE"}[i]));balances_[i]=new QLabel("— "+unit_);balances_[i]->setStyleSheet("font:700 23px monospace;color:#5ff2ce");card->addWidget(balances_[i]);cards->addWidget(frame,0,i);}
  overviewBox->addLayout(cards);auto* controls=new QHBoxLayout;auto* refresh=new QPushButton("Refresh wallet");auto* unlock=new QPushButton("Unlock…");auto* lock=new QPushButton("Lock wallet");controls->addWidget(refresh);controls->addWidget(unlock);controls->addWidget(lock);controls->addStretch();overviewBox->addLayout(controls);overviewBox->addWidget(createNetworkMetricsView(metrics,true),1);tabs_->addTab(overview,"Balances");
  auto* sendPage=new QWidget;auto* sendBox=new QFormLayout(sendPage);recipient_=new QLineEdit;recipient_->setPlaceholderText("Recipient "+unit_+" address");amount_=new QLineEdit;amount_->setPlaceholderText("Amount in "+unit_);send_=new QPushButton("Review payment…");send_->setEnabled(false);sendBox->addRow("Recipient",recipient_);sendBox->addRow("Amount",amount_);sendBox->addRow(send_);tabs_->addTab(sendPage,"Send");
  auto* receivePage=new QWidget;auto* receiveBox=new QVBoxLayout(receivePage);auto* receiveRow=new QHBoxLayout;label_=new QLineEdit;label_->setPlaceholderText("Optional address label");auto* generate=new QPushButton("Create receiving address");receiveRow->addWidget(label_,1);receiveRow->addWidget(generate);receiveBox->addLayout(receiveRow);auto* addressRow=new QHBoxLayout;address_=new QLineEdit;address_->setReadOnly(true);address_->setPlaceholderText("Select an address below, or create one");auto* copy=new QPushButton("Copy address");addressRow->addWidget(address_,1);addressRow->addWidget(copy);receiveBox->addLayout(addressRow);
  addresses_=table({"Label","Receiving address","Received · "+unit_});receiveBox->addWidget(addresses_,1);tabs_->addTab(receivePage,"Receive");
  auto* historyPage=new QWidget;auto* historyBox=new QVBoxLayout(historyPage);historyBox->setContentsMargins(0,0,0,0);historyStatus_=new QLabel("Latest wallet entries · newest first · dates in local time");historyStatus_->setObjectName("pgn-wallet-history-status");historyBox->addWidget(historyStatus_);transactions_=table({"Date · local","Type","Address","Amount · "+unit_,"Confirmations"});transactions_->setObjectName("pgn-wallet-transactions");historyBox->addWidget(transactions_,1);tabs_->addTab(historyPage,"History");box->addWidget(tabs_,1);
  connect(tabs_,&QTabWidget::currentChanged,this,[this](int){refreshWallet();});
  connect(refresh,&QPushButton::clicked,this,[this]{refreshWallet();});connect(copy,&QPushButton::clicked,this,[this]{if(!address_->text().isEmpty())QGuiApplication::clipboard()->setText(address_->text());});
  connect(addresses_,&QTableWidget::cellClicked,this,[this](int row,int){if(auto* item=addresses_->item(row,1))address_->setText(item->text());});
  connect(generate,&QPushButton::clicked,this,[this,generate]{generate->setEnabled(false);const QPointer<WalletAccountPanel> weak(this);runtime_->walletRequest("getnewaddress",{label_->text(),"bech32"},[weak,generate](QJsonValue value,QString error){if(!weak)return;generate->setEnabled(true);if(!error.isEmpty())return weak->showError(error);if(!value.isString()||value.toString().isEmpty())return weak->showError("Core did not return an address");weak->address_->setText(value.toString());weak->refreshWallet();});});
  connect(unlock,&QPushButton::clicked,this,[this]{bool okay=false;QString secret=QInputDialog::getText(this,"Unlock PGN wallet","Wallet passphrase",QLineEdit::Password,{},&okay);if(!okay||secret.isEmpty())return;const QPointer<WalletAccountPanel> weak(this);runtime_->walletRequest("walletpassphrase",{secret,60},[weak](QJsonValue,QString error){if(!weak)return;if(!error.isEmpty())weak->showError(error);weak->refreshWallet();});secret.fill(QChar(0));});
  connect(lock,&QPushButton::clicked,this,[this]{const QPointer<WalletAccountPanel> weak(this);runtime_->walletRequest("walletlock",{},[weak](QJsonValue,QString error){if(!weak)return;if(!error.isEmpty())weak->showError(error);weak->refreshWallet();});});
  connect(send_,&QPushButton::clicked,this,[this]{reviewPayment();});clock_.setInterval(5000);connect(&clock_,&QTimer::timeout,this,[this]{if(isVisible())refreshWallet();});clock_.start();
 }
 int pageCount()const{return tabs_->count();}
  void resetDisplayedWallet(){
   ++refreshGeneration_;pending_=0;available_=0;send_->setEnabled(false);address_->clear();label_->clear();recipient_->clear();amount_->clear();addresses_->setRowCount(0);transactions_->setRowCount(0);for(auto* balance:balances_)balance->setText("— "+unit_);account_->setText("Reading the selected wallet…");refreshWallet();
  }
 void refreshWallet(){
  if(pending_||!runtime_->connected()||runtime_->walletManager()->busy())return;pending_=4;const auto generation=++refreshGeneration_;const QPointer<WalletAccountPanel> weak(this);
  runtime_->walletRequest("getwalletinfo",{},[weak,generation](QJsonValue value,QString error){if(!weak||weak->refreshGeneration_!=generation)return;--weak->pending_;if(!error.isEmpty()){weak->account_->setText(error);weak->send_->setEnabled(false);return;}const auto data=value.toObject();const bool encrypted=data.contains("unlocked_until");const bool locked=encrypted&&data.value("unlocked_until").toInteger()==0;
   weak->account_->setText(QString("Wallet %1 · %2 · %3 transactions").arg(data.value("walletname").toString(),locked?"Locked":encrypted?"Unlocked":"Not encrypted").arg(data.value("txcount").toInteger()));weak->setProperty("pgn_wallet_account_observed",true);
  });
  runtime_->walletRequest("getbalances",{},[weak,generation](QJsonValue value,QString error){if(!weak||weak->refreshGeneration_!=generation)return;--weak->pending_;if(!error.isEmpty()){weak->account_->setText(error);weak->send_->setEnabled(false);return;}const auto mine=value.toObject().value("mine").toObject();const QStringList keys{"trusted","untrusted_pending","immature"};
   bool complete=true;for(int i=0;i<3;++i){const auto amount=pgn::wallet::exactAtomicField(mine,keys[i]);complete&=amount&&*amount>=0;weak->balances_[i]->setText(amount?pgn::wallet::formatAtomicPgn(*amount)+" "+weak->unit_:"— "+weak->unit_);}
   weak->available_=complete?pgn::wallet::exactAtomicField(mine,"trusted").value_or(0):0;weak->send_->setEnabled(weak->available_>0);if(!complete)weak->account_->setText("Exact native wallet balances are unavailable");
  });
  runtime_->walletRequest("listtransactions",walletHistoryParameters(),[weak,generation](QJsonValue value,QString error){if(!weak||weak->refreshGeneration_!=generation)return;--weak->pending_;if(!error.isEmpty()){weak->historyStatus_->setText("History could not refresh: "+error);return;}if(!value.isArray()||!presentWalletHistory(weak->transactions_,value.toArray())){weak->historyStatus_->setText("History response is invalid; the last verified entries are retained.");return;}weak->historyStatus_->setText(QString("%1 latest entries · newest first · dates in local time").arg(value.toArray().size()));weak->setProperty("pgn_wallet_history_rows",value.toArray().size());});
  runtime_->walletRequest("listreceivedbyaddress",{0,true},[weak,generation](QJsonValue value,QString error){if(!weak||weak->refreshGeneration_!=generation)return;--weak->pending_;if(!error.isEmpty())return;const auto rows=value.toArray();weak->addresses_->setRowCount(rows.size());for(int i=0;i<rows.size();++i){const auto v=rows[i].toObject();const QStringList columns{v.value("label").toString(),v.value("address").toString(),pgn::wallet::exactAtomicField(v,"amount").has_value()?pgn::wallet::formatAtomicPgn(*pgn::wallet::exactAtomicField(v,"amount")):QString("—")};for(int c=0;c<columns.size();++c)weak->addresses_->setItem(i,c,new QTableWidgetItem(columns[c]));}});
 }
private:
 static QTableWidget* table(const QStringList& labels){auto* out=new QTableWidget(0,labels.size());out->setHorizontalHeaderLabels(labels);out->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);out->horizontalHeader()->setStretchLastSection(true);out->verticalHeader()->hide();out->setEditTriggers(QAbstractItemView::NoEditTriggers);out->setSelectionBehavior(QAbstractItemView::SelectRows);return out;}
 void showError(const QString& error){QMessageBox::warning(this,"PGN wallet",error.left(500));}
 void reviewPayment(){
  const auto recipient=recipient_->text().trimmed(),amount=amount_->text().trimmed();const auto number=pgn::wallet::parsePgnTransfer(amount);
  if(recipient.isEmpty()||!number||*number<=0||*number>available_)return showError("Enter a recipient and a positive amount within the available balance.");
  send_->setEnabled(false);const QPointer<WalletAccountPanel> weak(this);runtime_->walletRequest("validateaddress",{recipient},[weak,recipient,amount](QJsonValue value,QString error){if(!weak)return;weak->send_->setEnabled(weak->available_>0);if(!error.isEmpty())return weak->showError(error);if(!value.toObject().value("isvalid").toBool())return weak->showError("Core rejected this "+weak->unit_+" address.");
   if(QMessageBox::question(weak,"Confirm "+weak->unit_+" payment","Send "+amount+" "+weak->unit_+" to\n"+recipient+"?\nThe native wallet determines the transaction fee.",QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
   weak->send_->setEnabled(false);weak->runtime_->walletRequest("sendtoaddress",{recipient,amount},[weak](QJsonValue result,QString failure){if(!weak)return;weak->send_->setEnabled(weak->available_>0);if(!failure.isEmpty())return weak->showError(failure);QMessageBox::information(weak,"Payment submitted","Transaction: "+result.toString());weak->refreshWallet();});
  });
 }
 pgn::wallet::CoreRuntime* runtime_{};const QString unit_;QLabel *account_{},*historyStatus_{};QLabel* balances_[3]{};QTabWidget* tabs_{};QLineEdit *recipient_{},*amount_{},*label_{},*address_{};QPushButton* send_{};QTableWidget *addresses_{},*transactions_{};QTimer clock_;int pending_{};quint64 refreshGeneration_{};qint64 available_{};
};
class WalletCoreView final : public QWidget {
public:
 pgn::wallet::CoreRuntime* networkRuntime()const{return runtime_;}
 void observeNetworkMetrics(QWidget* owner,std::function<void(const QJsonObject&,const QString&)> receive){metrics_->subscribe(owner,std::move(receive));}
 void inspectBlock(const QString& hash){if(networkPages_)networkPages_->setCurrentWidget(networkExplorer_);networkExplorer_->inspectHash(hash); }
 explicit WalletCoreView(const QString& root,WalletObservationSink observe,bool testnet){
  setObjectName("pgn-wallet-core-view");setProperty("pgn_wallet_frontend_loaded",false);
  auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
  auto* header=new QWidget(this);auto* headerBox=new QVBoxLayout(header);headerBox->setContentsMargins(20,18,20,8);layout->addWidget(header);
  auto* heading=new QLabel("PGN WALLET",this);
  heading->setStyleSheet("font:700 22px monospace;color:#e9fffb");auto* headingRow=new QHBoxLayout;headingRow->addWidget(heading);headingRow->addStretch();auto* manageWallets=new QPushButton("Manage wallets…");manageWallets->setObjectName("pgn-wallet-manage");headingRow->addWidget(manageWallets);headerBox->addLayout(headingRow);
  status_=new QLabel("Create a wallet or open an existing Core data folder below.",this);
  status_->setStyleSheet("color:#ffad4a;font:10px monospace");status_->setWordWrap(true);headerBox->addWidget(status_);
  auto* workspace=new QTabWidget;workspace->setObjectName("pgn-wallet-subtabs");auto* walletPage=new QWidget;auto* walletPageBox=new QVBoxLayout(walletPage);walletPageBox->setContentsMargins(0,0,0,0);auto* walletScroll=new QScrollArea;walletScroll->setObjectName("pgn-wallet-content-scroll");walletScroll->setWidgetResizable(true);walletScroll->setFrameShape(QFrame::NoFrame);auto* walletContent=new QWidget;viewport_=new QVBoxLayout(walletContent);viewport_->setContentsMargins(0,0,0,0);walletScroll->setWidget(walletContent);auto* walletPages=new QStackedWidget(walletPage);walletPages->setObjectName("pgn-wallet-pages");walletPageBox->addWidget(walletPages);walletPages->addWidget(walletScroll);auto* managementScroll=new QScrollArea;managementScroll->setObjectName("pgn-wallet-management-scroll");managementScroll->setWidgetResizable(true);managementScroll->setFrameShape(QFrame::NoFrame);managementScroll->setAlignment(Qt::AlignTop|Qt::AlignHCenter);auto* managementContent=new QWidget;managementContent->setMaximumWidth(1100);auto* managementBox=new QVBoxLayout(managementContent);managementBox->setContentsMargins(0,8,0,16);auto* backToOverview=new QPushButton("← Back to overview");backToOverview->setObjectName("pgn-wallet-overview");managementBox->addWidget(backToOverview,0,Qt::AlignLeft);managementScroll->setWidget(managementContent);walletPages->addWidget(managementScroll);walletPages->setCurrentIndex(0);connect(manageWallets,&QPushButton::clicked,this,[walletPages]{walletPages->setCurrentIndex(1);});connect(backToOverview,&QPushButton::clicked,this,[walletPages]{walletPages->setCurrentIndex(0);});workspace->addTab(walletPage,"Wallet");
  runtime_=new pgn::wallet::CoreRuntime(root,this,testnet,pgn::wallet::coreRuntimeDirectory(root,testnet));metrics_=new NetworkMetricsHub([this](int span,auto consume){runtime_->readNetworkMetrics(span,std::move(consume));},this);networkExplorer_=new BlockExplorerView(runtime_);
  networkPages_=new QTabWidget;networkPages_->setObjectName("pgn-wallet-network-tabs");networkPages_->addTab(networkExplorer_,"Blocks");networkPages_->addTab(new WalletPeersView([runtime=QPointer(runtime_)](auto done){if(runtime)runtime->readPeerSnapshot(std::move(done));else done({},"Core is unavailable");}),"Peers");networkPages_->addTab(new WalletConsoleView([runtime=QPointer(runtime_)](QString method,QByteArray params,bool wallet,auto done){if(runtime)runtime->consoleRequest(method,params,wallet,std::move(done));else done({},"Core is unavailable");}),"Console");workspace->addTab(networkPages_,"Explorer");
   management_=new WalletManagementPanel(runtime_->walletManager(),managementContent);managementBox->addWidget(management_);managementBox->addStretch();
   management_->onComponents=[this,root,testnet]{
    if(componentDialog_){componentDialog_->raise();componentDialog_->activateWindow();return;}
    componentDialog_=new ComponentVersionsDialog(walletComponentVersionsContext(root,runtime_->runtimeDirectory(),testnet),this);
    componentDialog_->setAttribute(Qt::WA_DeleteOnClose);componentDialog_->open();
   };
   const auto presentSetup=[this]{
    QFile file(runtime_->runtimeDirectory()+"/wallet-core.json");QJsonObject config;
    if(file.open(QIODevice::ReadOnly)&&file.size()<=32768)config=QJsonDocument::fromJson(file.readAll()).object();
    QFile pending(runtime_->runtimeDirectory()+"/wallet-pending-setup.json");QString scheduled;
    if(pending.open(QIODevice::ReadOnly)&&pending.size()<=40000)scheduled=QJsonDocument::fromJson(pending.readAll()).object().value("profile").toObject().value("data_directory").toString();
    management_->present(config.value("chain").toString(),config.value("data_directory").toString(),!config.isEmpty(),QFileInfo::exists(pending.fileName()),scheduled);
   };presentSetup();
   management_->onCancelChange=[this,presentSetup]{
    const auto path=runtime_->runtimeDirectory()+"/wallet-pending-setup.json";
    if(QFileInfo::exists(path)&&!QFile::remove(path)){status_->setText("Pending folder change could not be cancelled. Core and wallet data remain in their current folder.");return;}
    presentSetup();status_->setText("Pending folder change cancelled. The current Core folder is unchanged.");
   };
   management_->onApplySetup=[this,root,testnet,presentSetup](const pgn::wallet::WalletSetupSelection& choice){
    if(setupOpen_)return;setupOpen_=true;status_->setText("Checking PGN Core files…");
    auto* verification=new QFutureWatcher<pgn::wallet::CoreBundle>(this);
    connect(verification,&QFutureWatcher<pgn::wallet::CoreBundle>::finished,this,[this,root,testnet,presentSetup,verification,choice]{
     const auto bundle=verification->result();verification->deleteLater();
     if(!bundle.error.isEmpty()){setupOpen_=false;management_->finishSetup(bundle.error);status_->setText(bundle.error);return;}
     QFile current(runtime_->runtimeDirectory()+"/wallet-core.json");QByteArray base;
     if(current.exists()){
      if(!current.open(QIODevice::ReadOnly)||current.size()>32768){setupOpen_=false;management_->finishSetup("Current Core settings could not be read.");return;}base=current.readAll();
     }
     const auto selection=pgn::wallet::selectedWalletDeployment(bundle.deployment,choice,root);
     const auto error=!selection.error.isEmpty()?selection.error:base.isEmpty()?pgn::wallet::publishWalletSetup(runtime_->runtimeDirectory(),selection):pgn::wallet::stageWalletSetupChange(runtime_->runtimeDirectory(),selection,base);
     setupOpen_=false;management_->finishSetup(error);if(!error.isEmpty()){status_->setText(error);return;}presentSetup();
     setProperty("pgn_wallet_setup_required",false);
     if(base.isEmpty())runtime_->launch();
     else status_->setText("Folder change saved. Pause mining and audits, then shut down PGN Core normally. The selection applies only after the owned Core has stopped and Horizon starts again.");
    });
    verification->setFuture(QtConcurrent::run([root,testnet]{return pgn::wallet::readCoreBundle(root,testnet?QString("test"):QString("main"));}));
   };
  auto* historyView=createNetworkMetricsView(metrics_);layout->addWidget(workspace,1);
  auto* reloadHistory=new QPushButton("Refresh network graphs");reloadHistory->setObjectName("pgn-wallet-difficulty-reload");
  headingRow->addWidget(reloadHistory);
  connect(reloadHistory,&QPushButton::clicked,this,[this]{metrics_->refresh();});
  placeholder_=new QLabel("Connecting the verified native wallet…",this);
  qobject_cast<QLabel*>(placeholder_)->setWordWrap(true);
  viewport_->addWidget(placeholder_,1);
  workspace->addTab(createVanityView(root,runtime_,{},testnet),"Vanity");workspace->addTab(historyView,"Difficulty history");
  workspace->addTab(new AuditExplorerView(runtime_),"Audits");
  const auto contextualHeader=[heading,reloadHistory,manageWallets,workspace,walletPages](int){const int page=workspace->currentIndex();const bool managing=page==0&&walletPages->currentIndex()==1;heading->setText(managing?"Wallet management":"PGN WALLET");heading->setVisible(page==0);manageWallets->setVisible(page==0&&!managing);reloadHistory->setVisible((page==0&&!managing)||page==3);};connect(workspace,&QTabWidget::currentChanged,this,contextualHeader);connect(walletPages,&QStackedWidget::currentChanged,this,contextualHeader);contextualHeader(workspace->currentIndex());
  auto* navigation=HorizonCoinNavigation(workspace,{
   {"01 WALLET","BALANCES + PAYMENTS"},{"02 NETWORK","BLOCKS + ACTIVITY"},
   {"03 VANITY","PERSONAL ADDRESS"},{"04 DIFFICULTY","CHAIN HISTORY"},{"05 AUDITS","REPORTS + VERIFICATION"}},this);
  navigation->setProperty("pgn_navigation_role","wallet");layout->insertWidget(0,navigation);

  accountPanel_=new WalletAccountPanel(runtime_,metrics_,testnet,walletPage);accountPanel_->setEnabled(false);viewport_->addWidget(accountPanel_,1);placeholder_->hide();
   displayGeneration_=runtime_->walletManager()->displayGeneration();
   runtime_->walletManager()->onChanged=[this]{
    management_->refresh();const auto generation=runtime_->walletManager()->displayGeneration();
    if(generation!=displayGeneration_){displayGeneration_=generation;accountPanel_->resetDisplayedWallet();}
     accountPanel_->setEnabled(runtime_->displayWalletReady()&&!runtime_->walletManager()->busy());
     if(accountPanel_->isEnabled())accountPanel_->refreshWallet();
   };
  setProperty("pgn_core_link_state","starting");
  runtime_->onLinkState=[this](pgn::wallet::CoreLinkState state,const QString& detail){
   const auto key=pgn::wallet::coreLinkKey(state);
   setProperty("pgn_core_link_state",key);setProperty("pgn_core_link_detail",detail);
   auto history=property("pgn_core_link_transitions").toJsonArray();
   history.append(QJsonObject{{"state",key},{"detail",detail},{"observed_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}});
   while(history.size()>32)history.removeFirst();
   setProperty("pgn_core_link_transitions",history);
   if(state!=pgn::wallet::CoreLinkState::Ready)accountPanel_->setEnabled(false);
   else accountPanel_->setEnabled(runtime_->displayWalletReady()&&!runtime_->walletManager()->busy());
  };
  runtime_->onObservation=[this,observe=std::move(observe)](const auto& anchor){
   if(observe)observe(anchor);
   if(anchor&&anchor->tipHash!=networkDone_&&!networkBusy_)refreshNetwork();
   if(anchor&&anchor->tipHash!=metricsTip_){metricsTip_=anchor->tipHash;metrics_->refresh();}
  };
  runtime_->onStatus=[this](const QString& text){status_->setText(!QFileInfo::exists(runtime_->runtimeDirectory()+"/wallet-core.json")?QString("Create a wallet or open an existing Core data folder below."):text);status_->setStyleSheet(runtime_->connected()?"color:#69efc5;font:11px monospace":"color:#ffbd69;font:11px monospace");};
   runtime_->onReady=[this,presentSetup](qint64){presentSetup();attachClock_.start();attach();accountPanel_->setEnabled(!runtime_->walletManager()->busy());accountPanel_->refreshWallet();metrics_->refresh();};
  runtime_->onStopped=[this]{attachClock_.stop();accountPanel_->setEnabled(false);setProperty("pgn_wallet_embedded",false);setProperty("pgn_wallet_frontend_loaded",false);};
  attachClock_.setInterval(500);connect(&attachClock_,&QTimer::timeout,this,[this]{attach();});
  const bool needsSetup=!QFileInfo::exists(runtime_->runtimeDirectory()+"/wallet-core.json");
  setProperty("pgn_wallet_setup_required",needsSetup);
  if(needsSetup){workspace->setCurrentIndex(0);walletPages->setCurrentIndex(1);}
  else QTimer::singleShot(0,runtime_,[this]{runtime_->launch();});
 }
 ~WalletCoreView() override = default;
private:
 void refreshNetwork(){
  networkBusy_=true;runtime_->readNetworkSnapshot([this](QJsonObject state,QString error){networkBusy_=false;networkExplorer_->presentSnapshot(state,error);if(!error.isEmpty())return;networkDone_=state.value("bestblockhash").toString();setProperty("pgn_network_block_rows",state.value("blocks"));setProperty("pgn_network_leases",state.value("leases"));setProperty("pgn_network_height",state.value("height"));setProperty("pgn_network_gold_blocks",state.value("gold_blocks"));setProperty("pgn_network_ticket_width",state.value("ticket_width").toString());});
 }
 void attach(){
  if(!runtime_->connected())return;
#ifdef Q_OS_WIN
  hideOwnedCoreWindows(runtime_->processId());
#endif
  setProperty("pgn_wallet_embedded",accountPanel_->window()==window());setProperty("pgn_wallet_embedded_pid",runtime_->processId());setProperty("pgn_wallet_frontend_loaded",true);setProperty("pgn_wallet_genuine_page_count",accountPanel_->pageCount());
 }
 pgn::wallet::CoreRuntime* runtime_{};QLabel* status_{};QWidget* placeholder_{};
 BlockExplorerView* networkExplorer_{};QTabWidget* networkPages_{};bool networkBusy_{};QString networkDone_;
 QVBoxLayout* viewport_{};WalletAccountPanel* accountPanel_{};WalletManagementPanel* management_{};QPointer<ComponentVersionsDialog> componentDialog_;bool setupOpen_{};quint64 displayGeneration_{};
 QTimer attachClock_;int attempts_{};

 NetworkMetricsHub* metrics_{};QString metricsTip_;
};
}
QWidget* createWalletCoreView(const QString& root,WalletObservationSink observe,bool testnet){return new WalletCoreView(root,std::move(observe),testnet);}
void readWalletTicketPage(QWidget* widget,int offset,std::function<void(QJsonObject,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->readTicketPage(offset,std::move(consume));else consume({},"Wallet network view unavailable");}
void readWalletTicketDetails(QWidget* widget,const QString& id,std::function<void(QJsonObject,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->readTicketDetails(id,std::move(consume));else consume({},"Wallet network view unavailable");}
void readWalletTicketAtlas(QWidget* widget,const QString& start,const QString& end,int bins,std::function<void(QJsonObject,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->readTicketAtlas(start,end,bins,std::move(consume));else consume({},"Wallet network atlas unavailable");}
void readWalletAuditActivity(QWidget* widget,qint64 height,const QString& hash,const QJsonArray& watched,std::function<void(QJsonObject,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->readAuditActivity(height,hash,watched,std::move(consume));else consume({},"Wallet network activity unavailable");}
void readWalletAuditDetails(QWidget* widget,const QString& id,std::function<void(QJsonObject,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->readAuditDetails(id,std::move(consume));else consume({},"Wallet audit view unavailable");}
void readWalletLocalConnection(QWidget* widget,std::function<void(QJsonObject,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->readLocalConnection(std::move(consume));else consume({},"Local wallet connection unavailable");}
void stopWalletCoreForUpdate(QWidget* widget,std::function<void(QString)> done){
 auto* view=dynamic_cast<WalletCoreView*>(widget);if(!view){done("Wallet runtime is unavailable");return;}
 const QPointer<pgn::wallet::CoreRuntime> core(view->networkRuntime());
 core->suspendRecoveryForMaintenance();
 if(core->startPending()){done("Core startup or ownership recovery is still in progress");return;}
 if(!core->processId()){done({});return;}
 if(!core->connected()){done("Core is not verified and connected; no shutdown was attempted");return;}
 auto settled=std::make_shared<bool>(false);auto* timer=new QTimer(core);timer->setSingleShot(true);const auto previous=core->onStopped;
 core->onStopped=[core,timer,settled,previous,done]{
  if(*settled)return;*settled=true;timer->stop();timer->deleteLater();
  // Restore after this std::function returns, so its captures stay alive.
  QTimer::singleShot(0,core.data(),[core,previous,done]{if(core)core->onStopped=previous;if(previous)previous();done({});});
 };
 QObject::connect(timer,&QTimer::timeout,core.data(),[core,timer,settled,previous,done]{
  if(*settled)return;*settled=true;timer->deleteLater();if(core)core->onStopped=previous;done("Core did not confirm a graceful shutdown; update was held");
 });
 timer->start(30000);core->stop();
}
void readWalletSigningIdentity(QWidget* widget,const QString& address,std::function<void(QJsonValue,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->serviceWalletRequest("getaddressinfo",{address},std::move(consume));else consume({},"Wallet signing identity unavailable");}
void requestWalletAudit(QWidget* widget,const QString& method,const QJsonArray& params,std::function<void(QJsonValue,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->auditRequest(method,params,std::move(consume));else consume({},"Wallet audit view unavailable");}
void signWalletAudit(QWidget* widget,const QString& address,const QString& challenge,std::function<void(QJsonValue,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->signAuditChallenge(address,challenge,std::move(consume));else consume({},"Wallet audit view unavailable");}
QString walletSocialBinding(QWidget* widget){auto* view=dynamic_cast<WalletCoreView*>(widget);return view&&view->networkRuntime()->connected()?view->networkRuntime()->walletBinding():QString{};}
void readWalletSocialContext(QWidget* widget,const QString& address,std::function<void(QJsonObject,QString)> consume){
 auto* view=dynamic_cast<WalletCoreView*>(widget);if(!view){consume({},"Wallet Social view unavailable");return;}
 const QPointer<pgn::wallet::CoreRuntime> core(view->networkRuntime());const auto binding=core->walletBinding();
 if(address.isEmpty()){core->readSocialIdentity(std::move(consume));return;}
 core->readLocalConnection([core,binding,address,consume](QJsonObject local,QString error){
  if(!core||binding!=core->walletBinding()||!error.isEmpty()){consume({},error.isEmpty()?QString("Wallet Social binding changed"):error);return;}
  const auto genesis=local.value("genesis").toString();core->serviceWalletRequest("getaddressinfo",{address},[core,binding,address,genesis,consume](QJsonValue value,QString error){
   if(!core||binding!=core->walletBinding()||!error.isEmpty()){consume({},error.isEmpty()?QString("Wallet Social binding changed"):error);return;}
   const auto info=value.toObject();const auto script=info.value("scriptPubKey").toString();
   if(!info.value("ismine").toBool()||!QRegularExpression("^0014[0-9a-f]{40}$").match(script).hasMatch()||genesis!=core->networkGenesis()){consume({},"Social address is not owned by this verified wallet");return;}
   consume(QJsonObject{{"genesis",genesis},{"address",address},{"script",script},{"binding",binding}},{});
  });
 });
}
void requestWalletSocial(QWidget* widget,const QString& method,const QJsonArray& params,std::function<void(QJsonValue,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->socialRequest(method,params,std::move(consume));else consume({},"Wallet Social connection unavailable");}
void signWalletSocial(QWidget* widget,const QString& address,const QString& genesis,const QString& script,const QByteArray& message,std::function<void(QJsonValue,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->signSocialEnvelope(address,genesis,script,message,std::move(consume));else consume({},"Wallet Social signer unavailable");}
void verifyWalletSocial(QWidget* widget,const QString& genesis,const QString& script,const QByteArray& message,const QString& signature,std::function<void(bool,QString)> consume){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->networkRuntime()->verifySocialEnvelope(genesis,script,message,signature,std::move(consume));else consume(false,"Wallet Social verifier unavailable");}
void inspectWalletBlock(QWidget* widget,const QString& hash){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->inspectBlock(hash);}
void bindWalletNetworkMetrics(QWidget* widget,QWidget* owner,std::function<void(const QJsonObject&,const QString&)> receive){if(auto* view=dynamic_cast<WalletCoreView*>(widget))view->observeNetworkMetrics(owner,std::move(receive));else receive({},"Wallet network view unavailable");}

}

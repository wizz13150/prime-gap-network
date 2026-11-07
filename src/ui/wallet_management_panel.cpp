#include "ui/wallet_management_panel.h"
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFontDatabase>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
namespace pgn::ui {
WalletManagementPanel::WalletManagementPanel(pgn::wallet::WalletManager* manager,QWidget* parent):QWidget(parent),manager_(manager){
 setObjectName("pgn-wallet-management");auto textFont=QFontDatabase::systemFont(QFontDatabase::GeneralFont);
#ifdef Q_OS_WIN
 textFont.setFamily("Segoe UI");
#endif
 textFont.setPointSize(10);setFont(textFont);
 setStyleSheet("QLabel,QComboBox,QGroupBox{font-size:10pt;font-weight:400;font-family:'"+textFont.family()+"';}QGroupBox{border:1px solid #285662;border-radius:8px;margin-top:12px;padding:10px;}QGroupBox::title{subcontrol-origin:margin;left:12px;}QPushButton,QToolButton{font-size:10pt;font-weight:400;font-family:'"+textFont.family()+"';min-height:32px;padding:0 10px;}QToolButton{background:#0b3939;color:#69efc5;border:1px solid #4bc9b0;border-radius:7px;}QToolButton:disabled{color:#52757d;border-color:#23424a;background:#06171e;}");
 auto* outer=new QVBoxLayout(this);outer->setContentsMargins(20,8,20,4);
 auto* group=new QGroupBox("Core setup and wallets");outer->addWidget(group);auto* box=new QVBoxLayout(group);box->setSpacing(8);
 auto* note=new QLabel("One PGN Core node shares the blockchain across your wallets. Choose which wallet to display below.");note->setWordWrap(true);box->addWidget(note);
 location_=new QLabel;location_->setObjectName("pgn-wallet-core-location");location_->setTextFormat(Qt::PlainText);location_->setWordWrap(true);location_->setTextInteractionFlags(Qt::TextSelectableByMouse);box->addWidget(location_);
 auto* configuration=new QHBoxLayout;configure_=new QPushButton("Set up Core…");configure_->setObjectName("pgn-wallet-setup");configuration->addWidget(configure_);cancelChange_=new QPushButton("Cancel folder change");cancelChange_->setObjectName("pgn-wallet-cancel-folder-change");cancelChange_->hide();configuration->addWidget(cancelChange_);reload_=new QPushButton("Refresh wallets");reload_->setObjectName("pgn-wallet-list-refresh");configuration->addWidget(reload_);configuration->addStretch();box->addLayout(configuration);
 auto* choice=new QHBoxLayout;auto* label=new QLabel("Displayed wallet");wallets_=new QComboBox;wallets_->setObjectName("pgn-wallet-selector");wallets_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);wallets_->setMinimumContentsLength(14);wallets_->setMinimumWidth(80);label->setBuddy(wallets_);choice->addWidget(label);choice->addWidget(wallets_,1);select_=new QPushButton("Select");select_->setObjectName("pgn-wallet-select");choice->addWidget(select_);box->addLayout(choice);
 auto* actions=new QHBoxLayout;create_=new QPushButton("Create wallet…");create_->setObjectName("pgn-wallet-create");open_=new QToolButton;open_->setText("Open wallet…");open_->setObjectName("pgn-wallet-open");open_->setPopupMode(QToolButton::InstantPopup);auto* menu=new QMenu(open_);auto* folder=menu->addAction("Open wallet folder…");auto* file=menu->addAction("Open wallet .dat file…");open_->setMenu(menu);load_=new QPushButton("Load");load_->setObjectName("pgn-wallet-load");close_=new QPushButton("Close wallet");close_->setObjectName("pgn-wallet-close");for(auto* button:{create_,load_,close_})actions->addWidget(button);actions->insertWidget(1,open_);actions->addStretch();box->addLayout(actions);
 service_=new QLabel;service_->setObjectName("pgn-wallet-service-binding");service_->setTextFormat(Qt::PlainText);service_->setWordWrap(true);box->addWidget(service_);
 auto* software=new QHBoxLayout;auto* components=new QPushButton("Components…");components->setObjectName("pgn-wallet-components");software->addWidget(components);software->addStretch();box->addLayout(software);connect(components,&QPushButton::clicked,this,[this]{if(onComponents)onComponents();});
 message_=new QLabel;message_->setObjectName("pgn-wallet-management-status");message_->setTextFormat(Qt::PlainText);message_->setWordWrap(true);message_->setAccessibleName("Wallet management status");box->addWidget(message_);
 connect(configure_,&QPushButton::clicked,this,[this]{if(onConfigure)onConfigure();});connect(reload_,&QPushButton::clicked,this,[this]{manager_->refresh();});
 connect(cancelChange_,&QPushButton::clicked,this,[this]{if(onCancelChange)onCancelChange();});
 connect(create_,&QPushButton::clicked,this,[this]{bool okay=false;const auto name=QInputDialog::getText(this,"Create PGN wallet","Wallet name (letters, digits, _ or -)",QLineEdit::Normal,{},&okay);if(okay)manager_->create(name.trimmed());});
 connect(folder,&QAction::triggered,this,[this]{const auto path=QFileDialog::getExistingDirectory(this,"Open existing PGN wallet folder",manager_->walletDirectory());if(!path.isEmpty())manager_->open(path);});
 connect(file,&QAction::triggered,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"Open existing PGN wallet file",manager_->walletDirectory(),"Core wallet (*.dat)");if(!path.isEmpty())manager_->open(path);});
 connect(load_,&QPushButton::clicked,this,[this]{manager_->load(wallets_->currentData().toString());});connect(select_,&QPushButton::clicked,this,[this]{manager_->select(wallets_->currentData().toString());});connect(close_,&QPushButton::clicked,this,[this]{manager_->unload(wallets_->currentData().toString());});connect(wallets_,&QComboBox::currentIndexChanged,this,[this]{refresh();});refresh();
}
void WalletManagementPanel::present(const QString& network,const QString& directory,bool configured,bool pending,const QString& pendingDirectory){
 location_->setText(configured?QString("Network: %1\nCurrent Core data folder: %2\nDefault wallet folder: %3").arg(network,directory,QDir(directory).filePath(network=="test"?"testnet/wallets":"wallets")):"Choose a Core data folder to install a new PGN wallet or open your existing Core data.");
 configure_->setText(configured?"Change Core folder…":"Set up Core…");configure_->setToolTip("Choose a Core data folder. Existing files are never moved or deleted.");
 configure_->setEnabled(!pending);cancelChange_->setVisible(pending);
 if(pending)location_->setText(location_->text()+"\nScheduled Core data folder: "+pendingDirectory+"\nWaiting for the current Core to stop cleanly. Reopening Horizon while Core is still running keeps the current folder.");
}
void WalletManagementPanel::refresh(){
 const auto rows=manager_->wallets();const auto previous=wallets_->currentData().toString();wallets_->blockSignals(true);wallets_->clear();
 for(const auto& value:rows){const auto row=value.toObject();const auto name=row.value("name").toString();const auto shown=name.isEmpty()?QString("(default wallet)"):name;wallets_->addItem(shown+(row.value("loaded").toBool()?" · Loaded":" · Available")+(row.value("service").toBool()?" · Service":"")+(row.value("displayed").toBool()?" · Displayed":""),name);}
 auto index=wallets_->findData(previous);if(index<0)index=wallets_->findData(manager_->displayWallet());wallets_->setCurrentIndex(index);wallets_->blockSignals(false);
 const auto selected=wallets_->currentData().toString();bool loaded=false,known=wallets_->currentIndex()>=0;for(const auto& row:rows)if(row.toObject().value("name")==selected)loaded=row.toObject().value("loaded").toBool();
 const bool available=manager_->ready()&&!manager_->busy();for(auto* button:{reload_,create_})button->setEnabled(available);open_->setEnabled(available);wallets_->setEnabled(available);select_->setEnabled(available&&known&&loaded&&selected!=manager_->displayWallet());load_->setEnabled(available&&known&&!loaded);close_->setEnabled(available&&known&&loaded&&selected!=manager_->serviceWallet()&&selected!=manager_->displayWallet());
 close_->setToolTip(selected==manager_->serviceWallet()?"The mining and audit service wallet stays loaded to preserve signed work.":selected==manager_->displayWallet()?"Select another wallet before closing this wallet.":"Unload this wallet from Core; its files remain intact.");
 service_->setText(manager_->ready()?QString("Mining / audit service wallet: %1. %2").arg(manager_->serviceWallet().isEmpty()?"(default wallet)":manager_->serviceWallet(),manager_->isLoaded(manager_->serviceWallet())?"Display selection leaves this binding unchanged.":"Not loaded. Services wait for this wallet; displaying another wallet keeps the original binding."):"The service wallet is established only after Core verifies the network and genesis.");
 message_->setText(manager_->message());
}
}

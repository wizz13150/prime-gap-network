#include "wallet/wallet_manager.h"
#include "ui/wallet_management_panel.h"
#include "ui/render/horizon_workspace_style.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <iostream>
#include <stdexcept>
using namespace pgn::wallet;
int main(int argc,char** argv){
 QApplication app(argc,argv);QTemporaryDir temp;if(!temp.isValid())return 2;int checks=0,calls=0,creates=0;auto require=[&](bool ok){++checks;if(!ok)throw std::runtime_error("wallet management check "+std::to_string(checks));};
 QSet<QString> loaded{"service","Savings / # % é"},available=loaded;available.insert("cold");QString fail;QString lastWallet;QJsonArray lastArgs;
 WalletTransport transport=[&](const QString& method,const QJsonArray& params,const QString& wallet,WalletReply done){
  ++calls;lastWallet=wallet;lastArgs=params;if(method=="createwallet")++creates;
  QTimer::singleShot(0,&app,[&,method,params,wallet,done=std::move(done)]{
   if(!fail.isEmpty()){auto error=fail;fail.clear();done({},error);return;}
   if(method=="listwallets"){QJsonArray rows;for(const auto& name:loaded)rows.append(name);done(rows,{});}
   else if(method=="listwalletdir"){QJsonArray rows;for(const auto& name:available)rows.append(QJsonObject{{"name",name}});done(QJsonObject{{"wallets",rows}},{});}
   else if(method=="getwalletinfo")done(QJsonObject{{"walletname",wallet}},{});
   else if(method=="loadwallet"||method=="createwallet"){
    auto name=params[0].toString();if(loaded.contains(name)){done({},"Core RPC error (-35): Wallet is already loaded");return;}
    if(method=="createwallet"&&available.contains(name)){done({},"Wallet already exists");return;}
    available.insert(name);loaded.insert(name);done(QJsonObject{{"name",name}},{});
   }else if(method=="unloadwallet"){loaded.remove(params[0].toString());done(QJsonObject{},{});}
   else done({},"Unexpected RPC");
  });
 };
 auto wait=[&](WalletManager& manager){QElapsedTimer timer;timer.start();while(manager.busy()&&timer.elapsed()<3000){app.processEvents();QThread::msleep(1);}require(!manager.busy());};
 const auto directory=temp.filePath("PGNCore"),state=temp.filePath("view.json");QDir().mkpath(directory);
 WalletManager manager(transport);pgn::ui::WalletManagementPanel panel(&manager);manager.onChanged=[&]{panel.refresh();};panel.present("test",directory,true);manager.activate(QString(64,'b'),"test",directory,"service",state);wait(manager);require(manager.wallets().size()==3&&manager.displayWallet()=="service");
 require(walletRpcPath("Savings / # % é")=="/wallet/Savings%20%2F%20%23%20%25%20%C3%A9");
 require(QUrl::fromEncoded(("http://127.0.0.1:12345"+walletRpcPath("x/#%?é")).toUtf8()).toEncoded().endsWith("/wallet/x%2F%23%25%3F%C3%A9"));
 const auto originalCalls=calls;manager.unload("service");require(calls==originalCalls&&!manager.message().isEmpty());
 const auto displayGeneration=manager.displayGeneration();require(manager.holdDisplayOperation());manager.select("Savings / # % é");require(calls==originalCalls&&manager.displayWallet()=="service"&&manager.busy());manager.finishDisplayOperation(displayGeneration);require(!manager.busy());
 manager.select("Savings / # % é");manager.select("cold");wait(manager);require(manager.displayWallet()=="Savings / # % é"&&manager.serviceWallet()=="service");require(QFileInfo::exists(state));
 auto* combo=panel.findChild<QComboBox*>("pgn-wallet-selector");require(combo&&combo->count()==3);combo->setCurrentIndex(combo->findData("service"));require(!panel.findChild<QPushButton*>("pgn-wallet-close")->isEnabled());
 const auto count=calls;manager.create("../bad");manager.open("relative");manager.open(temp.filePath("missing"));require(calls==count);
 manager.create("Daily");manager.create("Daily");require(creates==1);require(lastArgs.size()==7&&lastArgs[5].toBool()&&!lastArgs[6].toBool());wait(manager);require(loaded.contains("Daily"));
 manager.load("Daily");require(manager.message().contains("already loaded"));
 manager.load("cold");wait(manager);require(loaded.contains("cold"));
 fail="Backend unavailable";manager.select("cold");wait(manager);require(manager.displayWallet()=="Savings / # % é"&&manager.message()=="Backend unavailable");
 WalletManager reopened(transport);reopened.activate(QString(64,'b'),"test",directory,"service",state);wait(reopened);require(reopened.displayWallet()=="Savings / # % é");
 WalletManager wrongNetwork(transport);wrongNetwork.activate(QString(64,'c'),"main",directory,"service",state);wait(wrongNetwork);require(wrongNetwork.displayWallet()=="service");
 WalletManager missingService(transport);missingService.activate(QString(64,'b'),"test",directory,"missing-service",temp.filePath("missing-service.json"));wait(missingService);require(!missingService.isLoaded(missingService.serviceWallet()));missingService.select("Savings / # % é");wait(missingService);require(missingService.isLoaded(missingService.displayWallet())&&missingService.serviceWallet()=="missing-service");
 const auto external=temp.filePath("External wallet #1");QDir().mkpath(external);manager.open(external);wait(manager);require(loaded.contains(QDir::fromNativeSeparators(QFileInfo(external).canonicalFilePath())));
 manager.unload(manager.displayWallet());require(manager.message().contains("Select another"));manager.select("service");wait(manager);manager.unload("Savings / # % é");require(!lastArgs[1].toBool());wait(manager);require(!loaded.contains("Savings / # % é")&&available.contains("Savings / # % é"));
 manager.refresh();manager.invalidate();app.processEvents();require(!manager.ready()&&manager.wallets().isEmpty());manager.activate(QString(64,'b'),"test",directory,"service",state);wait(manager);
 int cancelled=0;panel.onCancelChange=[&]{++cancelled;};panel.present("test",directory,true,true,temp.filePath("ScheduledCore"));require(panel.findChild<QLabel*>("pgn-wallet-core-location")->text().contains("ScheduledCore"));require(!panel.findChild<QPushButton*>("pgn-wallet-setup")->isEnabled()&&!panel.findChild<QPushButton*>("pgn-wallet-cancel-folder-change")->isHidden());panel.findChild<QPushButton*>("pgn-wallet-cancel-folder-change")->click();require(cancelled==1);panel.present("test",directory,true);require(panel.findChild<QPushButton*>("pgn-wallet-setup")->isEnabled()&&panel.findChild<QPushButton*>("pgn-wallet-cancel-folder-change")->isHidden());
 if(argc>1){QDir().mkpath(QString::fromLocal8Bit(argv[1]));app.setStyleSheet("QMainWindow,QWidget{background:#010b10;color:#c9eee7;}"+HorizonWorkspaceControlStyle());for(const auto size:{QSize(580,440),QSize(1100,460)}){panel.resize(size);panel.show();app.processEvents();require(panel.rect().contains(QRect(panel.findChild<QPushButton*>("pgn-wallet-close")->mapTo(&panel,QPoint{}),panel.findChild<QPushButton*>("pgn-wallet-close")->size())));QPixmap image(panel.size());panel.render(&image);require(image.save(QDir(QString::fromLocal8Bit(argv[1])).filePath(QString("wallet-management-%1.png").arg(size.width()))));}}
 std::cout<<"PASS "<<checks<<" multiwallet checks: real UI/fake RPC, encoded paths, persistent display, network isolation, create/load/open/unload, busy/double click, backend errors, service binding retained\n";
}

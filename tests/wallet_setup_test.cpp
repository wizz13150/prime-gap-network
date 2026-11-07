#include "ui/wallet_setup_wizard.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QPushButton>
#include <QLabel>
#include <QPixmap>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
 QApplication app(argc,argv);QTemporaryDir temp;if(!temp.isValid())return 2;int checks=0;auto require=[&](bool okay){++checks;if(!okay)throw std::runtime_error("wallet setup check "+std::to_string(checks));};
 const auto application=temp.filePath("application");QDir().mkpath(application);const auto target=temp.filePath("user-data/PGNCore");using pgn::wallet::WalletSetupSelection;
 WalletSetupSelection choice{target,"PGN",false};require(pgn::wallet::validateWalletSetup(choice,application).isEmpty());require(!QFileInfo::exists(target));
 for(const auto& bad:QList<WalletSetupSelection>{{"relative","PGN",false},{QDir::rootPath(),"PGN",false},{target,"../wallet",false},{application+"/data","PGN",false}})require(!pgn::wallet::validateWalletSetup(bad,application).isEmpty());
 int commits=0;WalletSetupSelection committed;
 {
  pgn::ui::WalletSetupWizard wizard(application,"PGN testnet",[&](const auto& value){++commits;committed=value;return QString{};});wizard.setSelection(choice);wizard.show();app.processEvents();require(commits==0&&!QFileInfo::exists(target));require(wizard.button(QWizard::NextButton)->isVisible()&&wizard.rect().contains(QRect(wizard.button(QWizard::NextButton)->mapTo(&wizard,QPoint{}),wizard.button(QWizard::NextButton)->size())));require(wizard.button(QWizard::CancelButton)->isVisible());wizard.reject();require(commits==0&&!QFileInfo::exists(target));
 }
 {
  pgn::ui::WalletSetupWizard wizard(application,"PGN testnet",[&](const auto& value){++commits;committed=value;return QString{};});wizard.setSelection(choice);wizard.show();wizard.next();wizard.next();app.processEvents();require(wizard.currentId()==2&&commits==0&&!QFileInfo::exists(target));require(wizard.button(QWizard::FinishButton)->isVisible()&&wizard.rect().contains(QRect(wizard.button(QWizard::FinishButton)->mapTo(&wizard,QPoint{}),wizard.button(QWizard::FinishButton)->size())));wizard.button(QWizard::FinishButton)->click();require(commits==1&&wizard.result()==QDialog::Accepted);require(committed.dataDirectory==target&&!committed.existing&&committed.walletName=="PGN");
 }
 {
  pgn::ui::WalletSetupWizard wizard(application,"PGN testnet",[](const auto&){return QString("Deployment write refused");});wizard.setSelection(choice);wizard.show();wizard.next();wizard.next();wizard.button(QWizard::FinishButton)->click();require(wizard.result()!=QDialog::Accepted&&wizard.findChild<QLabel*>("pgn-wallet-setup-error")->text()=="Deployment write refused");
 }
 const auto existing=temp.filePath("existing");QDir().mkpath(existing+"/wallets");require(pgn::wallet::validateWalletSetup({existing,"saved_wallet",true},application).isEmpty());require(!pgn::wallet::validateWalletSetup({existing,"saved_wallet",false},application).isEmpty());require(!QFileInfo::exists(target));
  {pgn::ui::WalletSetupWizard wizard(application,"PGN testnet",[&](const auto&){++commits;return QString{};});wizard.setDeferredChange(true);wizard.setSelection(choice);wizard.show();wizard.next();wizard.next();app.processEvents();require(wizard.button(QWizard::FinishButton)->text()=="Save folder change");require(wizard.findChild<QLabel*>("pgn-wallet-setup-review")->text().contains("new keys"));if(argc>1){QDir().mkpath(QString::fromLocal8Bit(argv[1]));QPixmap image(wizard.size());wizard.render(&image);require(image.save(QDir(QString::fromLocal8Bit(argv[1])).filePath("wallet-folder-change-review.png")));}wizard.reject();require(commits==1&&!QFileInfo::exists(target));}
  std::cout<<"PASS "<<checks<<" wallet setup checks: OS-data default, explicit custom/existing selection, no installation or filesystem writes before Finish, cancel and failed commit preserved; deferred change and new keys explicit\n";
}

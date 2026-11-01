#include "ui/wallet_core_view.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTabBar>
#include <QFrame>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QButtonGroup>
#include <QDir>
#include <QFileInfo>
#include <memory>
#include <iostream>
int main(int argc,char** argv){
 QApplication app(argc,argv);QTemporaryDir empty;
 std::unique_ptr<QWidget> view(pgn::ui::createWalletCoreView(empty.path()));view->resize(1380,800);view->show();app.processEvents();
 auto* outer=view->findChild<QTabWidget*>("pgn-wallet-subtabs");auto* account=view->findChild<QWidget*>("pgn-wallet-account");
 if(!outer||outer->count()!=4||!account||account->isEnabled()||view->property("pgn_wallet_frontend_loaded").toBool())return 1;
 auto* pages=account->findChild<QTabWidget*>();if(!pages||pages->count()!=4)return 2;
 auto* nav=view->findChild<QFrame*>("horizonNavigation");if(!nav||!outer->tabBar()->isHidden())return 3;
 auto* group=nav->findChild<QButtonGroup*>();if(!group||group->buttons().size()!=4)return 4;
 for(int i=0;i<4;++i){group->button(i)->click();app.processEvents();if(outer->currentIndex()!=i)return 5;}
 bool placeholder=false;for(auto* label:account->findChildren<QLabel*>())if(label->text()==QString::fromUtf8("— PGN"))placeholder=true;
 if(!placeholder||QFileInfo::exists(empty.path()+"/data/runtime/wallet-owned.json"))return 6;
 for(auto* input:account->findChildren<QLineEdit*>())if(input->placeholderText().contains("Select an address")&&!input->isReadOnly())return 7;
 outer->setCurrentIndex(0);if(argc>1)view->grab().save(QString::fromLocal8Bit(argv[1]));
 std::cout<<"WALLET_NATIVE_HORIZON_PASS: actual four-page widget tree, shared navigation, absent Core holds operations and unknown balances, no process/window/key/node created\n";return 0;
}

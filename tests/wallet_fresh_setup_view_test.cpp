#include "ui/wallet_core_view.h"
#include <QStackedWidget>
#include <QLineEdit>
#include <QLabel>
#include <QWizard>
#include "wallet/runtime_paths.h"
#include "ui/render/horizon_workspace_style.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QPushButton>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QUuid>
#include <QPixmap>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
int main(int argc,char** argv){
 QApplication app(argc,argv);QCoreApplication::setOrganizationName("PGN-TEST");QCoreApplication::setApplicationName("wallet-fresh-"+QUuid::createUuid().toString(QUuid::Id128));QTemporaryDir temp;if(!temp.isValid())return 2;
 const auto root=temp.filePath("application"),data=temp.filePath("user-data/PGNCore");QDir().mkpath(root+"/Core");QFile executable(root+"/Core/pgn-core.exe");if(!executable.open(QIODevice::WriteOnly))return 3;const QByteArray fixture("INERT TEST FIXTURE NEVER EXECUTED");executable.write(fixture);executable.close();
 const QJsonObject deployment{{"product","PGN Core"},{"chain","test"},{"frontend","daemon"},{"executable","pgn-core.exe"},{"executable_sha256",QString::fromLatin1(QCryptographicHash::hash(fixture,QCryptographicHash::Sha256).toHex())},{"genesis_hash",QString(64,'b')},{"rpc_port",30101},{"peer_port",30102}};
 QFile manifest(root+"/Core/core-template.json");if(!manifest.open(QIODevice::WriteOnly))return 4;manifest.write(QJsonDocument(QJsonObject{{"schema","PGN_CORE_BUNDLE_V1"},{"deployment",deployment},{"files",QJsonArray{QJsonObject{{"path","pgn-core.exe"},{"bytes",fixture.size()},{"sha256",deployment.value("executable_sha256")}}}}}).toJson());manifest.close();
 const auto configuration=pgn::wallet::coreRuntimeDirectory(root,true);if(QFileInfo::exists(configuration))return 5;
 auto* view=pgn::ui::createWalletCoreView(root,{},true);view->setStyleSheet("QWidget{background:#010b10;color:#c9eee7;}"+HorizonWorkspaceControlStyle());view->resize(850,650);view->show();app.processEvents();
 auto* pages=view->findChild<QStackedWidget*>("pgn-wallet-pages");auto* create=view->findChild<QPushButton*>("pgn-wallet-create-initial");auto* open=view->findChild<QPushButton*>("pgn-wallet-open-core");
 if(!view->property("pgn_wallet_setup_required").toBool()||!pages||pages->currentIndex()!=1||!create||!open||!create->isVisible()||!create->isEnabled()||!open->isVisible()||!open->isEnabled()||!view->findChildren<QWizard*>().isEmpty())return 6;
 if(QFileInfo::exists(data)||QFileInfo::exists(root+"/data")||QFileInfo::exists(configuration))return 7;
 if(argc>1){QDir().mkpath(QString::fromLocal8Bit(argv[1]));QPixmap image(view->size());view->render(&image);if(!image.save(QDir(QString::fromLocal8Bit(argv[1])).filePath("wallet-first-run-management.png")))return 9;}
 // An invalid request returns to the same usable management page; verification
 // must not launch the inert Core or create data/configuration as a side effect.
 view->findChild<QLineEdit*>("pgn-wallet-data-directory")->setText(data);view->findChild<QLineEdit*>("pgn-wallet-name")->setText("invalid/name");create->click();
 QElapsedTimer wait;wait.start();while(!create->isEnabled()&&wait.elapsed()<4000){app.processEvents();QThread::msleep(5);}
 auto* message=view->findChild<QLabel*>("pgn-wallet-management-status");if(!create->isEnabled()||!message||!message->text().contains("letters")||!view->findChildren<QWizard*>().isEmpty())return 10;
 const bool untouched=!QFileInfo::exists(data)&&!QFileInfo::exists(root+"/data")&&!QFileInfo::exists(configuration);delete view;
 if(!untouched)return 8;std::cout<<"PASS fresh startup opens actual Manage wallets with enabled create/open, no wizard; closing and invalid setup create no wallet/config/data; inert Core never launched\n";
}

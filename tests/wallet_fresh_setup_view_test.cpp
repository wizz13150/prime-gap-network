#include "ui/wallet_core_view.h"
#include "ui/wallet_setup_wizard.h"
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
 auto* view=pgn::ui::createWalletCoreView(root,{},true);view->setStyleSheet("QWidget{background:#010b10;color:#c9eee7;}"+HorizonWorkspaceControlStyle());view->resize(800,650);view->show();app.processEvents();auto* setup=view->findChild<QPushButton*>("pgn-wallet-setup");
 if(!setup||!setup->isVisible()||QFileInfo::exists(data)||QFileInfo::exists(root+"/data")||QFileInfo::exists(configuration)){delete view;return 6;}
 if(argc>1){QDir().mkpath(QString::fromLocal8Bit(argv[1]));QPixmap image(view->size());view->render(&image);if(!image.save(QDir(QString::fromLocal8Bit(argv[1])).filePath("wallet-before-setup.png")))return 9;}
 setup->click();app.processEvents();pgn::ui::WalletSetupWizard* wizard=nullptr;QElapsedTimer wait;wait.start();while(!wizard&&wait.elapsed()<2000){for(auto* dialog:view->findChildren<QWizard*>())if(auto* candidate=dynamic_cast<pgn::ui::WalletSetupWizard*>(dialog))wizard=candidate;if(!wizard){app.processEvents();QThread::msleep(5);}}
 if(!wizard){delete view;return 7;}wizard->setSelection({data,"PGN",false});
 if(argc>1){app.processEvents();QPixmap image(wizard->size());wizard->render(&image);if(!image.save(QDir(QString::fromLocal8Bit(argv[1])).filePath("wallet-setup-welcome.png")))return 10;wizard->next();app.processEvents();image=QPixmap(wizard->size());wizard->render(&image);if(!image.save(QDir(QString::fromLocal8Bit(argv[1])).filePath("wallet-setup-location.png")))return 11;wizard->next();app.processEvents();image=QPixmap(wizard->size());wizard->render(&image);if(!image.save(QDir(QString::fromLocal8Bit(argv[1])).filePath("wallet-setup-review.png")))return 12;}
 wizard->reject();app.processEvents();
 const bool untouched=!QFileInfo::exists(data)&&!QFileInfo::exists(root+"/data")&&!QFileInfo::exists(configuration);delete view;
 if(!untouched)return 8;std::cout<<"PASS fresh wallet page: explicit setup button, real wizard, cancel creates no wallet/config/history data or miner-folder wallet files; inert Core never launched\n";
}

#include "ui/component_versions_dialog.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>
#include "update/update_paths.h"
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
 QApplication app(argc,argv);int checks=0;const auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Update dialog check "+std::to_string(checks));};
 QStandardPaths::setTestModeEnabled(true);app.setApplicationName("PGN-Recovery-QA-"+QUuid::createUuid().toString(QUuid::Id128));
 pgn::ui::ComponentVersionsContext context;context.testnet=true;context.fetchOnOpen=false;
 pgn::ui::ComponentVersionsDialog dialog(context);dialog.show();
 QEventLoop settle;QTimer::singleShot(100,&settle,&QEventLoop::quit);settle.exec();
 auto* table=dialog.findChild<QTableWidget*>("pgn-components-table");auto* download=dialog.findChild<QPushButton*>("pgn-components-prepare");auto* install=dialog.findChild<QPushButton*>("pgn-components-install");
 check(table&&download&&install);check(!download->isEnabled()&&!install->isVisible());
 QJsonArray installed,available;for(const auto* id:{"ui","core","gpu","cpu"}){installed.append(QJsonObject{{"id",id},{"state","matched_local_manifest"},{"version","1.2"}});available.append(QJsonObject{{"id",id},{"version",QString(id)=="core"?"1.2.0":"1.3"}});}
 dialog.presentLocal({{"manifest_state","valid"},{"components",installed}});dialog.presentPublished({{"schema","PGN_AUTHENTICATED_COMPONENT_VIEW_V1"},{"publisher_authenticated",true},{"components",available}});
 check(table->rowCount()==4&&table->columnCount()==4);check(download->isEnabled());
 check(table->item(0,0)->checkState()==Qt::Checked);check(!(table->item(1,0)->flags()&Qt::ItemIsUserCheckable));check(table->item(1,3)->text()=="Up to date");
 for(int row=0;row<4;++row)if(table->item(row,0)->flags()&Qt::ItemIsUserCheckable)table->item(row,0)->setCheckState(Qt::Unchecked);
 check(!download->isEnabled());dialog.findChild<QPushButton*>("pgn-components-select-all")->click();check(download->isEnabled());
 auto unknown=installed[3].toObject();unknown.insert("state","unidentified");installed[3]=unknown;dialog.presentLocal({{"manifest_state","valid"},{"components",installed}});check(!(table->item(3,0)->flags()&Qt::ItemIsUserCheckable));
 dialog.resize(850,440);app.processEvents();check(table->viewport()->width()>700&&table->viewport()->height()>=136);
 if(argc>1){QDir().mkpath(QString::fromLocal8Bit(argv[1]));check(dialog.grab().save(QString::fromLocal8Bit(argv[1])+"/component-updates.png"));}
 // An interrupted installation stays visible even when no update catalogue
 // can be fetched. The fixture uses a unique test-mode application directory.
 context.genesisHash=QString(64,'5');const auto store=pgn::update::updateStateDirectory(context.genesisHash)+"/installation";check(QDir().mkpath(store));
 QFile journal(store+"/transaction.json");check(journal.open(QIODevice::WriteOnly));journal.write(QJsonDocument(QJsonObject{{"schema","PGN_COHORT_ACTIVATION_V1"},{"state","starting_candidate"},{"restoration_completed",false}}).toJson());journal.close();
 pgn::ui::ComponentVersionsDialog recovery(context);recovery.show();recovery.presentLocal({{"manifest_state","valid"},{"components",installed}});recovery.presentPublished({{"schema","PGN_AUTHENTICATED_COMPONENT_VIEW_V1"},{"publisher_authenticated",true},{"components",available}});app.processEvents();
 auto* restore=recovery.findChild<QPushButton*>("pgn-components-recover");auto* blockedDownload=recovery.findChild<QPushButton*>("pgn-components-prepare");check(restore&&restore->isVisible()&&restore->isEnabled());check(!blockedDownload->isEnabled());
 auto* choices=recovery.findChild<QTableWidget*>("pgn-components-table");choices->item(0,0)->setCheckState(Qt::Unchecked);check(!blockedDownload->isEnabled());
 check(journal.remove());recovery.presentLocal({{"manifest_state","valid"},{"components",installed}});check(!restore->isVisible());
 QDir().rmdir(store);QDir().rmdir(QFileInfo(store).absolutePath());
 std::cout<<"PASS "<<checks<<" optional component selection/layout checks; no download or process control\n";
}

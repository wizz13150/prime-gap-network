#include "ui/component_status_strip.h"
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QDir>
#include <QJsonDocument>
#include <iostream>
int main(int argc,char** argv){
 QApplication app(argc,argv);pgn::ui::ComponentVersionsContext context;context.fetchOnOpen=false;
 if(argc>=3&&QString::fromLocal8Bit(argv[1])=="--inspect"){
  const auto root=QString::fromLocal8Bit(argv[2]);context.manifestPath=root+"/components.json";
  context.codeRoots={{"ui",root},{"core",argc>3?QString::fromLocal8Bit(argv[3]):root+"/Core"},{"gpu",root+"/EngineRuntime"}};
  context.primaryPrograms={{"ui",root+"/PGN-Horizon.exe"},{"core",context.codeRoots.value("core")+"/pgn-core.exe"}};
  const auto value=pgn::ui::inspectLocalComponentVersions(context);std::cout<<QJsonDocument(value).toJson(QJsonDocument::Compact).constData()<<"\n";
  for(const auto& entry:value.value("components").toArray()){const auto row=entry.toObject();if(row.value("id")!="cpu"&&row.value("state")!="matched_local_manifest")return 6;}
  return 0;
 }
 QWidget window;auto* box=new QHBoxLayout(&window);auto* network=new QLabel("TESTNET · Gold blocks 0 · 1 peer");network->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);box->addWidget(network,1);
 auto* status=new pgn::ui::ComponentStatusStrip(context,&window);box->addWidget(status);
 QEventLoop loop;QTimer::singleShot(80,&loop,&QEventLoop::quit);loop.exec();
 status->presentLocal({{"components",QJsonArray{QJsonObject{{"id","ui"},{"state","matched_local_manifest"},{"version","1.2.3"},{"code_root","verified-code"}},QJsonObject{{"id","core"},{"state","unidentified"},{"code_root","existing-code"}},QJsonObject{{"id","cpu"},{"state","unidentified"},{"code_root",""}}}}});
 const auto ui=status->findChild<QToolButton*>("pgn-component-status-ui");const auto core=status->findChild<QToolButton*>("pgn-component-status-core");const auto cpu=status->findChild<QToolButton*>("pgn-component-status-cpu");
 if(!ui||ui->text()!="UI 1.2.3"||!core||core->text()!="CORE ?"||!cpu||cpu->text()!="CPU —")return 1;
 status->presentPublished({{"component_versions_available",false},{"packages",QJsonArray{QJsonObject{{"version","99.0"}}}}});
 const auto updates=status->findChild<QToolButton*>("pgn-component-update-status");if(!updates||updates->text()!="Updates · versions unavailable")return 2;
 QJsonObject signedCatalog{{"schema","PGN_AUTHENTICATED_COMPONENT_VIEW_V1"},{"publisher_authenticated",true},{"components",QJsonArray{QJsonObject{{"id","ui"},{"version","1.2.4"}}}}};
 status->presentPublished(signedCatalog);if(updates->text()!="Updates · 1 available"||!ui->text().endsWith(" ↑"))return 7;
 signedCatalog.insert("components",QJsonArray{QJsonObject{{"id","ui"},{"version","1.2.3"}}});status->presentPublished(signedCatalog);
 if(updates->text()!="Updates · partially verified"||ui->text().endsWith(" ↑"))return 8;
 status->presentPublished({{"component_versions_available",false}});if(updates->text()!="Updates · partially verified")return 9;
 QJsonArray installed,available;
 for(const auto& pair:QList<QPair<QString,QString>>{{"ui","2026.11.6.25"},{"core","2026.11.6.2"},{"gpu","2026.11.6.6"},{"cpu","2026.11.6.389"}}){installed.append(QJsonObject{{"id",pair.first},{"state","matched_local_manifest"},{"version",pair.second},{"code_root","verified-code"}});available.append(QJsonObject{{"id",pair.first},{"version",pair.second}});}
 status->presentLocal({{"components",installed}});signedCatalog.insert("components",available);status->presentPublished(signedCatalog);if(updates->text()!="Updates · up to date")return 10;
 for(auto* button:status->findChildren<QToolButton*>())if(button!=updates&&(button->property("state")!="current"||!button->text().endsWith(" ✓")))return 11;
 status->presentUpdateChecking();if(updates->text()!="Updates · checking"||ui->property("state")!="checking"||ui->text().contains("✓"))return 12;
 status->presentUpdateFailure("Controlled offline check");if(updates->text()!="Updates · check failed"||ui->property("state")!="stale"||!ui->text().endsWith(" ?"))return 13;
 status->presentUpdateFailure("GitHub update service is temporarily unavailable (HTTP 504)");if(updates->text()!="Updates · GitHub unavailable"||ui->property("state")!="stale")return 17;
 status->presentPublished(signedCatalog);if(ui->property("state")!="current")return 14;
 // A local inventory refresh must keep a fresh signed comparison, while a
 // later failure must visibly withdraw it without changing installed versions.
 status->presentLocal({{"components",installed}});if(ui->property("state")!="current"||!ui->text().endsWith(" ✓"))return 15;
 auto newer=installed;auto newerUI=newer[0].toObject();newerUI.insert("version","2026.11.6.26");newer[0]=newerUI;
 status->presentLocal({{"components",newer}});if(ui->property("state")!="ahead"||!ui->text().endsWith(" ?")||updates->text()!="Updates · partially verified")return 16;
 status->presentLocal({{"components",installed}});status->presentPublished(signedCatalog);
 for(int width:{1280,1920}){
  window.resize(width,60);window.show();app.processEvents();
  for(auto* button:status->findChildren<QToolButton*>())if(!status->rect().contains(button->geometry()))return 3;
  if(status->geometry().right()>=window.width()||network->width()<180)return 4;
 }
 if(argc>1){QDir().mkpath(QString::fromLocal8Bit(argv[1]));window.resize(1280,60);app.processEvents();if(!window.grab().save(QString::fromLocal8Bit(argv[1])+"/component-footer.png"))return 5;}
 std::cout<<"PASS: verified/unknown/absent states, no false current claim, 1280/1920 layout.\n";
}

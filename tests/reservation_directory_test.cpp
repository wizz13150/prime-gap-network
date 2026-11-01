#include "booking/booking_model.h"
#include "booking/source_registry.h"
#include "ui/reservation_directory_view.h"
#include "ui/community_view.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QLineEdit>
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <memory>
#include <iostream>
static void write(const QString& path,const QByteArray& data){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())throw std::runtime_error("Fixture write failed");}
static QByteArray read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Fixture read failed");return f.readAll();}
static void click(QApplication& app,QPushButton* button){const QPointF p=button->rect().center();QMouseEvent down(QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier),up(QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);app.sendEvent(button,&down);app.sendEvent(button,&up);app.processEvents();}
static void type(QApplication& app,QLineEdit* field,const QString& value){field->setFocus();field->selectAll();for(const auto c:value){QKeyEvent key(QEvent::KeyPress,0,Qt::NoModifier,QString(c));app.sendEvent(field,&key);}app.processEvents();}
int main(int argc,char** argv){QApplication app(argc,argv);try{
 if(argc!=2)return 2;const auto product=QString::fromLocal8Bit(argv[1]);QTemporaryDir temporary;const auto root=temporary.path(),runtime=root+"/data/runtime",community=root+"/data/community";QDir().mkpath(runtime);QDir().mkpath(community);
 auto bytes=read(product+"/data/runtime/community-source.csv");if(!bytes.endsWith('\n'))bytes+='\n';bytes+="510000,510100,Reserved,robert44444uk,,,,,,,,,,\n910000,910100,Reserved,robert44444uk,,,,,,,,,,\n";
 auto binding=QJsonDocument::fromJson(read(product+"/data/runtime/binding.json")).object();auto metadata=binding["community"].toObject();metadata["source_sha256"]=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());binding["community"]=metadata;
 write(runtime+"/community-source.csv",bytes);write(runtime+"/binding.json",QJsonDocument(binding).toJson());write(runtime+"/community-profile.json","{\"display_name\":\"Wizz\"}");
 for(const auto& name:{"registry.json","historical-original.csv","community-reservations.csv"})if(!QFile::copy(product+"/data/community/"+name,community+"/"+name))return 3;
 pgn::booking::BookingModel model(root);if(!model.source().valid())return 4;const auto sourceCount=model.source().ranges.size();model.setRoundRange(91);
 const QString device="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac",run(32,'a');const auto now=QDateTime::currentDateTimeUtc();
 model.observeRuntime(run,binding["start"].toString(),binding["end"].toString(),device,"RTX 3090","500000000000000",now,true);
 QString navigatedStart,navigatedEnd;std::unique_ptr<QWidget> view(pgn::ui::createReservationDirectory(&model,[&](const QString& a,const QString& b){navigatedStart=a;navigatedEnd=b;}));view->resize(1220,650);view->show();app.processEvents();
 auto* search=view->findChild<QLineEdit*>("pgn-owner-search");auto* table=view->findChild<QTableWidget*>("pgn-owner-results");auto* go=view->findChild<QPushButton*>("pgn-owner-go");auto* state=view->findChild<QComboBox*>("pgn-owner-state");if(!search||!table||!go||!state)return 5;
 const auto originalPage=model.viewPage();type(app,search,"RoBeRt");if(table->rowCount()<3)return 6;bool page5=false,page9=false;for(int i=0;i<table->rowCount();++i){page5|=table->item(i,1)->text()=="510000000000000000000";page9|=table->item(i,1)->text()=="910000000000000000000";if(table->item(i,3)->text().contains("complete",Qt::CaseInsensitive))return 7;}if(!page5||!page9||model.viewPage()!=originalPage){std::cerr<<"directory navigation invariant: page5="<<page5<<" page9="<<page9<<" before="<<originalPage<<" after="<<model.viewPage()<<'\n';return 8;}
 table->selectRow(0);click(app,go);if(navigatedStart!=table->item(0,1)->text()||navigatedEnd!=table->item(0,2)->text())return 9;
 type(app,search,"Benxy");if(table->rowCount()!=1||table->item(0,1)->text()!="79000000000000100000000000000"||table->item(0,2)->text()!="79000000000002100000000000000")return 10;table->selectRow(0);app.processEvents();if(go->isEnabled()||table->item(0,3)->text()!="Reserved")return 11;
 type(app,search,"Wizz");if(table->rowCount()!=1||!table->item(0,4)->text().startsWith("50.00000%"))return 12;table->selectRow(0);
 model.observeRuntime(run,binding["start"].toString(),binding["end"].toString(),device,"RTX 3090","600000000000000",now.addMSecs(100),true);app.processEvents();if(search->text()!="Wizz"||table->currentRow()!=0||!table->item(0,4)->text().startsWith("60.00000%"))return 13;
 type(app,search,"robert");state->setCurrentIndex(1);app.processEvents();if(table->rowCount()==0)return 14;for(int i=0;i<table->rowCount();++i)if(!table->item(i,3)->text().contains("complete",Qt::CaseInsensitive))return 15;
 type(app,search,"miner not found");if(table->rowCount()!=0||go->isEnabled())return 16;
 if(model.source().ranges.size()!=sourceCount)return 17; // directory is not booking authority
 auto archive=read(community+"/historical-original.csv");archive[0]='X';write(community+"/historical-original.csv",archive);const auto refused=pgn::booking::loadReservationDirectory(runtime,model.source());if(refused.error.isEmpty()||refused.ranges.size()!=sourceCount)return 18;
 std::unique_ptr<QWidget> page(pgn::ui::createCommunityView(&model,nullptr,[](const QString&){return QColor("#4facfe");}));page->resize(1380,900);page->show();app.processEvents();
 auto* open=page->findChild<QPushButton*>("pgn-owner-directory-open");if(!open)return 19;click(app,open);auto* pageSearch=page->findChild<QLineEdit*>("pgn-owner-search");auto* pageResults=page->findChild<QTableWidget*>("pgn-owner-results");auto* pageGo=page->findChild<QPushButton*>("pgn-owner-go");if(!pageSearch||!pageResults||!pageGo)return 20;
 type(app,pageSearch,"robert");int remote=-1;for(int i=0;i<pageResults->rowCount();++i)if(pageResults->item(i,1)->text()=="510000000000000000000")remote=i;if(remote<0)return 21;pageResults->selectRow(remote);click(app,pageGo);if(model.viewPage()!=5)return 22;
 std::cout<<"PASS owner search across fragmented ranges, exact manual endpoints, keyboard and navigation clicks, unavailable remote progress, stable local live progress/selection, separate processed history, no authority mutation, corrupt prefix refusal; actual Community button and navigation callback\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 100;}}

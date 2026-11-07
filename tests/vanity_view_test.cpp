#include "ui/vanity_view.h"
#include "ui/vanity_device_picker.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QPushButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QTableWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QImage>
#include <QThread>
#include <QFrame>
#include <QScrollBar>
#include <iostream>
#include <stdexcept>
static void require(bool okay,const char* error){if(!okay)throw std::runtime_error(error);}
int main(int argc,char** argv){
 QApplication app(argc,argv);require(argc>=2,"Qualified native helper fixture required");QTemporaryDir fixture;require(fixture.isValid(),"Fixture directory unavailable");
 const QString root=fixture.path(),vault=root+"/vault",native=root+"/wallet/native-runtime/qualified/pgn-vanity-session.exe";
 require(QDir().mkpath(QFileInfo(native).absolutePath())&&QDir().mkpath(root+"/data"),"Fixture runtime directory unavailable");require(QFile::copy(QString::fromLocal8Bit(argv[1]),native),"Native helper copy failed");
 QFile image(native);require(image.open(QIODevice::ReadOnly),"Native helper fixture unreadable");const auto hash=QString::fromLatin1(QCryptographicHash::hash(image.readAll(),QCryptographicHash::Sha256).toHex());image.close();
 QFile spec(root+"/data/vanity-runtime.json");require(spec.open(QIODevice::WriteOnly),"Fixture profile unavailable");spec.write(QJsonDocument(QJsonObject{{"schema","pgn-vanity-runtime-1"},{"executable",native},{"sha256",hash}}).toJson());spec.close();
 {pgn::ui::VanityDevicePicker picker;picker.resize(280,32);picker.show();for(int i=0;i<2;++i)picker.addDevice("gpu"+QString::number(i),"GPU "+QString::number(i),pgn::ui::VanityDevicePicker::Kind::Gpu,true,true);for(int i=0;i<80;++i)picker.addDevice("cpu"+QString::number(i),"CPU "+QString::number(i),pgn::ui::VanityDevicePicker::Kind::Cpu,true,i<3);require(picker.deviceCount()==82&&picker.selectedCount()==5&&picker.text().contains("2 GPUs")&&picker.text().contains("3 CPUs"),"Device summary does not track multiple selections");picker.addDevice("cpu0","CPU renamed",pgn::ui::VanityDevicePicker::Kind::Cpu);require(picker.deviceCount()==82&&picker.selectedCount()==5,"Inventory refresh duplicated or cleared a device");picker.menu()->popup(QPoint(20,20));app.processEvents();require(picker.popupViewportHeight()<=320&&picker.menu()->findChild<QScrollArea*>()->verticalScrollBar()->maximum()>0,"Large device inventory is not scrollable and bounded");picker.menu()->hide();picker.hide();}
 const auto wait=[&](auto condition,int timeout=15000){QElapsedTimer clock;clock.start();while(!condition()&&clock.elapsed()<timeout){app.processEvents(QEventLoop::AllEvents,15);QThread::msleep(5);}require(condition(),"Vanity UI did not reach its bounded expected state");};
 auto* view=pgn::ui::createVanityView(root,nullptr,vault);view->resize(1100,760);view->show();
 wait([&]{return view->findChild<QCheckBox*>("pgn-vanity-cpu-local-0")!=nullptr;});
 auto* cpu=view->findChild<QCheckBox*>("pgn-vanity-cpu-local-0");cpu->setChecked(true);view->findChild<QLineEdit*>("pgn-vanity-prefix")->setText("");
 require(!view->findChild<QPushButton*>("pgn-vanity-check"),"Manual prefix check is still exposed");auto* beginning=view->findChild<QLineEdit*>("pgn-vanity-prefix");auto* ending=view->findChild<QLineEdit*>("pgn-vanity-suffix");require(ending&&beginning->maxLength()==32,"Pattern inputs unavailable");require(beginning->placeholderText()=="Prefix"&&ending->placeholderText()=="Suffix"&&beginning->palette().color(QPalette::PlaceholderText).alpha()<180,"Pattern placeholders are not distinct and subdued");beginning->setText("O0!");require(!view->property("pgn_vanity_pattern_ready").toBool()&&!view->findChild<QPushButton*>("pgn-vanity-generate")->isEnabled(),"Invalid input did not immediately block generation");beginning->clear();view->findChild<QPushButton*>("pgn-vanity-mode-suffix")->click();ending->setText("a");wait([&]{return view->property("pgn_vanity_pattern_ready").toBool();});require(!beginning->isVisible()&&ending->isVisible(),"Suffix mode did not switch fields");view->findChild<QPushButton*>("pgn-vanity-mode-both")->click();beginning->setText("a");wait([&]{return view->property("pgn_vanity_pattern_ready").toBool();});require(beginning->isVisible()&&ending->isVisible(),"Both mode did not show both inputs");beginning->clear();ending->clear();view->findChild<QPushButton*>("pgn-vanity-mode-prefix")->click();
 auto* count=view->findChild<QSpinBox*>("pgn-vanity-count");require(count&&count->minimum()==1&&count->maximum()==1000,"Custom quantity is not bounded1..1000");count->setValue(1001);require(count->value()==1000,"Quantity upper bound failed");
 auto* five=view->findChild<QPushButton*>("pgn-vanity-count-5");five->click();require(count->value()==5&&five->isChecked(),"Quantity preset did not select visibly");
 auto* generate=view->findChild<QPushButton*>("pgn-vanity-generate");wait([&]{return view->property("pgn_vanity_pattern_ready").toBool();});generate->click();wait([&]{return !view->property("pgn_vanity_busy").toBool();});
 auto* results=view->findChild<QTableWidget*>("pgn-vanity-results");require(results&&results->rowCount()==5&&view->property("pgn_vanity_saved_results").toInt()==5,"Finite run did not persist exactly5verified addresses");
 require(view->findChild<QPushButton*>("pgn-vanity-export-selected")->isEnabled(),"Generated results are not selectable for export");
 require(!view->findChild<QPushButton*>("pgn-vanity-import-selected")->isEnabled(),"Import was offered without a verified wallet");
 for(int row=0;row<results->rowCount();++row)require(results->item(row,0)->checkState()==Qt::Checked&&results->item(row,1)->text().startsWith("PGN"),"Result checkbox or native prefix wrong");
 auto* continuous=view->findChild<QPushButton*>("pgn-vanity-count-0");continuous->click();require(continuous->isChecked()&&!count->isEnabled(),"Continuous mode is not distinct from a finite count");wait([&]{return view->property("pgn_vanity_pattern_ready").toBool();});generate->click();
 bool resetCount=false;for(auto* label:view->findChildren<QLabel*>())resetCount|=label->text()=="0 found";require(resetCount,"A new batch displayed the previous batch count");
 wait([&]{return view->property("pgn_vanity_saved_results").toInt()>=7;});const int atStop=view->property("pgn_vanity_saved_results").toInt();view->findChild<QPushButton*>("pgn-vanity-cancel")->click();wait([&]{return !view->property("pgn_vanity_busy").toBool();});
 const int saved=view->property("pgn_vanity_saved_results").toInt();require(saved>=atStop&&saved<=atStop+1,"Cancellation lost a result or admitted further searches");
 delete view;app.processEvents();view=pgn::ui::createVanityView(root,nullptr,vault);view->resize(1100,760);view->show();app.processEvents();
 require(view->property("pgn_vanity_saved_results").toInt()==saved,"Restart did not retain generated addresses");
 wait([&]{return view->findChild<QCheckBox*>("pgn-vanity-cpu-local-0")!=nullptr;});
 wait([&]{return view->property("pgn_vanity_pattern_ready").toBool();});
 for(int width:{640,820,1100,1920}){
  view->resize(width,width<800?900:760);app.processEvents();auto* pattern=view->findChild<QFrame*>("pgn-vanity-pattern-card");auto* quantity=view->findChild<QFrame*>("pgn-vanity-quantity-card");
  require(pattern&&quantity,"Vanity composer cards are missing");
  require(view->width()==width,"Vanity layout forces a wider window");require(width<800?pattern->geometry().bottom()<quantity->geometry().top():pattern->geometry().right()<quantity->geometry().left(),"Vanity composer is not responsive or cards overlap");
  for(auto* control:{static_cast<QWidget*>(view->findChild<QLineEdit*>("pgn-vanity-prefix")),static_cast<QWidget*>(view->findChild<QSpinBox*>("pgn-vanity-count")),static_cast<QWidget*>(view->findChild<QPushButton*>("pgn-vanity-generate"))}){
   const QRect bounds(control->mapTo(view,QPoint{}),control->size());require(view->rect().contains(bounds),"A primary vanity control is clipped");
  }
  require(view->findChild<QTableWidget*>("pgn-vanity-results")->height()>=100,"Generated addresses lost usable space");
  auto* prefixField=view->findChild<QLineEdit*>("pgn-vanity-prefix");require(prefixField->height()>=prefixField->fontMetrics().height()+20,"Address prefix text is compressed by panel styles");
  if(argc>2){QDir().mkpath(QString::fromLocal8Bit(argv[2]));QImage frame(view->size(),QImage::Format_ARGB32);view->render(&frame);require(frame.save(QString::fromLocal8Bit(argv[2])+QString("/vanity-%1.png").arg(width)),"Vanity fixture render failed");}
 }
 view->findChild<QCheckBox*>("pgn-vanity-cpu-local-0")->setChecked(true);view->findChild<QLineEdit*>("pgn-vanity-prefix")->setText("");wait([&]{return view->property("pgn_vanity_pattern_ready").toBool();});view->findChild<QPushButton*>("pgn-vanity-generate")->click();delete view;app.processEvents();
 view=pgn::ui::createVanityView(root,nullptr,vault+"-testnet",true);view->show();wait([&]{return view->findChild<QCheckBox*>("pgn-vanity-cpu-local-0")!=nullptr;});
 require(view->findChild<QLabel*>("pgn-vanity-fixed-namespace")->text()=="tpgn"&&view->findChild<QLabel*>("pgn-vanity-fixed-codec")==nullptr&&view->findChild<QFrame*>("pgn-vanity-fixed-prefix")->property("addressPrefix").toString()=="tpgn"&&view->findChild<QLineEdit*>("pgn-vanity-prefix")->text().isEmpty(),"Testnet UI selected the mainnet prefix");view->findChild<QCheckBox*>("pgn-vanity-cpu-local-0")->setChecked(true);wait([&]{return view->property("pgn_vanity_pattern_ready").toBool();});view->findChild<QPushButton*>("pgn-vanity-generate")->click();wait([&]{return !view->property("pgn_vanity_busy").toBool();});
 results=view->findChild<QTableWidget*>("pgn-vanity-results");if(results->rowCount()!=1)std::cerr<<"Public testnet diagnostic: rows="<<results->rowCount()<<" status="<<view->findChild<QLabel*>("pgn-vanity-status")->text().toStdString()<<'\n';require(results->rowCount()==1&&results->item(0,1)->text().startsWith("tpgn"),"Testnet generator did not use the canonical wallet network format");
 view->findChild<QPushButton*>("pgn-vanity-mode-suffix")->click();view->findChild<QLineEdit*>("pgn-vanity-suffix")->setText("q");wait([&]{return view->property("pgn_vanity_pattern_ready").toBool();});view->findChild<QPushButton*>("pgn-vanity-generate")->click();wait([&]{return !view->property("pgn_vanity_busy").toBool();});require(results->rowCount()==2&&results->item(0,1)->text().endsWith("q"),"Real CPU suffix search failed");
 view->findChild<QPushButton*>("pgn-vanity-mode-both")->click();const auto reachablePrefix=results->item(0,1)->text().mid(4,1);view->findChild<QLineEdit*>("pgn-vanity-prefix")->setText(reachablePrefix);wait([&]{return view->property("pgn_vanity_pattern_ready").toBool();});view->findChild<QPushButton*>("pgn-vanity-generate")->click();wait([&]{return !view->property("pgn_vanity_busy").toBool();});require(results->rowCount()==3&&results->item(0,1)->text().startsWith("tpgn"+reachablePrefix)&&results->item(0,1)->text().endsWith("q"),"Real CPU combined search failed");require(results->item(0,2)->text().contains("…q"),"Combined pattern metadata is not visible");
  if(argc>2){for(const int width:{640,820,1100,1920}){view->resize(width,720);QCoreApplication::processEvents();QImage frame(view->size(),QImage::Format_ARGB32);view->render(&frame);require(frame.save(QString::fromLocal8Bit(argv[2])+QString("/vanity-testnet-%1.png").arg(width)),"Testnet vanity fixture render failed");}}

 delete view;app.processEvents();std::cout<<"PASS actual native CPU vanity UI:5finite, continuous/cancel, selection, bounded quantity, protected history restart; no GPU, real wallet or real vault touched\n";
}

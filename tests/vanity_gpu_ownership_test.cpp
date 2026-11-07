#include "ui/vanity_view.h"
#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileInfo>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <iostream>
#include <stdexcept>
#include <vector>

// Inert child protocol fixture. It never generates keys or loads a GPU module.
static int child(int argc,char** argv){
 const QString command=QString::fromLocal8Bit(argv[1]);QJsonObject reply;
 if(command=="--plan"&&argc>=5)reply={{"status","pattern_ready"},{"prefix",QString::fromLocal8Bit(argv[2])},{"suffix",QString::fromLocal8Bit(argv[4])},{"pattern_modes",QJsonArray{"prefix","suffix","both"}}};
 else if(command=="--prepare"&&argc>=5){const auto path=QString::fromLocal8Bit(argv[2])+"/inert-ownership-fixture";QDir().mkpath(path);reply={{"status","prepared"},{"directory",path},{"job",QJsonObject{}}};}
 else if(command=="--cpu-inventory"||command=="--inventory")reply={{"status","inventory"},{"packages",QJsonArray{}}};
  else{const auto marker=qEnvironmentVariable("PGN_OWNERSHIP_TEST_MARKER");if(!marker.isEmpty()){QFile file(marker);if(file.open(QIODevice::WriteOnly))file.write("Unexpected worker dispatch");}reply={{"status","error"},{"error","Inert fixture refuses computation"}};}
 std::cout<<QJsonDocument(reply).toJson(QJsonDocument::Compact).constData()<<"\n";return 0;
}
int main(int argc,char** argv){
 if(argc>1){QCoreApplication childApp(argc,argv);return child(argc,argv);}
 QApplication app(argc,argv);int checks=0;auto require=[&](bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);};
 QString stage;QWidget* diagnosticView=nullptr;
 auto wait=[&](auto predicate){QElapsedTimer clock;clock.start();while(!predicate()&&clock.elapsed()<5000){app.processEvents(QEventLoop::AllEvents,10);QThread::msleep(5);}if(!predicate()){std::cerr<<"stage="<<stage.toStdString();if(diagnosticView){auto* label=diagnosticView->findChild<QLabel*>("pgn-vanity-status");std::cerr<<" status="<<(label?label->text().toStdString():"missing");}std::cerr<<"\n";}require(predicate(),"Ownership fixture timed out");};
 for(int scenario=0;scenario<6;++scenario){
  QTemporaryDir fixture;require(fixture.isValid(),"Temporary fixture missing");const auto root=fixture.path(),marker=root+"/unexpected-worker";qputenv("PGN_OWNERSHIP_TEST_MARKER",marker.toUtf8());
  const auto native=root+"/wallet/native-runtime/pgn-vanity-session.exe";require(QDir().mkpath(QFileInfo(native).absolutePath())&&QDir().mkpath(root+"/data"),"Fixture paths unavailable");require(QFile::copy(QCoreApplication::applicationFilePath(),native),"Inert helper copy failed");QFile binary(native);require(binary.open(QIODevice::ReadOnly),"Helper unreadable");const auto hash=QString::fromLatin1(QCryptographicHash::hash(binary.readAll(),QCryptographicHash::Sha256).toHex());binary.close();
  QFile profile(root+"/data/vanity-runtime.json");require(profile.open(QIODevice::WriteOnly),"Profile unavailable");profile.write(QJsonDocument(QJsonObject{{"schema","pgn-vanity-runtime-1"},{"executable",native},{"sha256",hash},{"local_gpu_module","INERT-NEVER-LOAD-GPU"}}).toJson());profile.close();
  QWidget wrapper;auto* view=pgn::ui::createVanityView(root,nullptr,root+"/vault",true);view->setParent(&wrapper);diagnosticView=view;
  std::vector<std::function<void(QString)>> pauses;QString currentLease;int released=0;bool requestedResume=false,drained=false;
  pgn::ui::VanityGpuServices services;services.device="GPU-INERT";services.label="Isolated ownership fixture";services.host="local";services.state=[scenario]{return scenario==3||scenario==4?QString("on"):QString("off");};
  services.pause=[&](const QString& lease,std::function<void(QString)> done){currentLease=lease;pauses.push_back(std::move(done));};
  services.release=[&](const QString& lease,bool resume,std::function<void(QString)> done){require(lease==currentLease,"Foreign lease released");++released;requestedResume=resume;done({});};
  pgn::ui::bindVanityGpu(&wrapper,services);view->findChild<QLineEdit*>("pgn-vanity-prefix")->setText("a");stage=QString("%1 pattern").arg(scenario);wait([&]{return view->property("pgn_vanity_pattern_ready").toBool();});
  auto* generate=view->findChild<QPushButton*>("pgn-vanity-generate");auto* cancel=view->findChild<QPushButton*>("pgn-vanity-cancel");require(generate&&cancel&&generate->isEnabled(),"Controls not ready");
  QTimer confirmation;QObject::connect(&confirmation,&QTimer::timeout,[]{if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))if(auto* yes=box->button(QMessageBox::Yes))yes->click();});if(scenario==3||scenario==4)confirmation.start(10);
  const auto begin=[&]{generate->click();};
  stage=QString("%1 pause").arg(scenario);begin();wait([&]{return pauses.size()==1;});require(view->property("pgn_vanity_busy").toBool()&&!QFileInfo::exists(marker),"Worker ran before pause confirmation");stage=QString("%1 cancellation/release").arg(scenario);
  if(scenario==1){pauses[0]("Injected controller refusal");wait([&]{return !view->property("pgn_vanity_busy").toBool();});require(released==0,"Refused pause released an unowned lease");}
  else if(scenario==2){
   const auto old=pauses[0];old("Injected first pause refusal");wait([&]{return !view->property("pgn_vanity_busy").toBool();});begin();wait([&]{return pauses.size()==2;});const auto lease=currentLease;
   old({});app.processEvents();require(currentLease==lease&&view->property("pgn_vanity_busy").toBool()&&!QFileInfo::exists(marker),"Stale pause callback mutated next generation");
   cancel->click();pauses[1]({});wait([&]{return !view->property("pgn_vanity_busy").toBool();});require(released==1&&!requestedResume,"Next generation release was not exact");
  }else{
   const bool closing=scenario==4||scenario==5;if(closing)require(pgn::ui::drainVanityForClose(&wrapper,[&]{drained=true;}),"Close failed to drain pending pause");else cancel->click();
   pauses[0]({});pauses[0]({});wait([&]{return !view->property("pgn_vanity_busy").toBool();});require(released==1,"Duplicate pause callback released twice");require(requestedResume==(scenario==3),"Initial running/off/closing state not respected");if(closing)wait([&]{return drained;});
  }
  require(!QFileInfo::exists(marker),"Ownership failure dispatched a worker");delete view;app.processEvents();
 }
 qunsetenv("PGN_OWNERSHIP_TEST_MARKER");std::cout<<"PASS "<<checks<<" isolated actual-widget GPU ownership checks: pending cancellation, refusal, stale and duplicate callbacks, close drain, prior ON/OFF resume policy; inert helper, no keys/GPU/Core/user window touched\n";
}

#include "booking/booking_model.h"
#include "ui/queue_view.h"
#include "horizon_workspace_style.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
int checks{};
void require(bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);}
const QString gpuA="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
const QString gpuB="GPU-11111111-2222-3333-4444-555555555555";
}
int main(int argc,char** argv){
 if(argc!=3)return 2;
 const QString plugin=QString::fromLocal8Bit(argv[1]),images=QString::fromLocal8Bit(argv[2]);
 if(!QFileInfo(plugin).isFile())return 3;
 QPluginLoader preflight(plugin);if(!preflight.load()){std::cerr<<preflight.errorString().toStdString()<<'\n';return 4;}
 qputenv("QT_QPA_PLATFORM","offscreen");qputenv("QT_QPA_PLATFORM_PLUGIN_PATH",QFileInfo(plugin).absolutePath().toUtf8());
 QApplication app(argc,argv);if(QGuiApplication::platformName()!="offscreen")return 5;
 app.setStyleSheet("QWidget{background:#010b10;color:#c9eee7;}"+HorizonWorkspaceControlStyle());
 try{
  QTemporaryDir temp;require(temp.isValid()&&QDir().mkpath(temp.path()+"/data/runtime"),"isolated store");
  const auto database=temp.path()+"/data/runtime/local-bookings.sqlite";
  pgn::booking::BookingModel model(temp.path());
  std::unique_ptr<QWidget> view(pgn::ui::createQueueView(&model,{}));
  auto* toggle=view->findChild<QCheckBox*>("pgn-queue-automatic-after-manual");
  auto* message=view->findChild<QLabel*>("pgn-queue-order-message");
  require(toggle&&message,"production queue contains continuation switch and feedback");
  require(!toggle->isEnabled()&&!toggle->isChecked(),"unknown GPU cannot enable automatic fallback");
  const auto observe=[&](QChar id,const QString& device){model.observeRuntime(QString(32,id),"199932000000000000000","199933000000000000000",device,"Fixture GPU","30",QDateTime::currentDateTimeUtc(),false);};
  observe('a',gpuA);
  require(toggle->isEnabled()&&!toggle->isChecked(),"known GPU defaults OFF");
  const auto jobsBefore=model.jobs();
  toggle->click();
  require(toggle->isChecked()&&message->text().startsWith("Saved"),"toggle ON has persisted feedback");
  {
   pgn::booking::BookingStore read(database);
   const auto state=read.miningSessionMetadata(gpuA);
   require(state&&state->automaticAfterManual&&state->mode==pgn::mining::MiningSessionMode::Unselected,"switch saves preference only; no implicit mining start");
   require(read.active().size()==jobsBefore.size(),"switch creates no reservation or dispatch");
   require(read.setMiningSessionMode(gpuA,pgn::mining::MiningSessionMode::ManualReservations).isEmpty(),"fixture controller establishes manual provenance");
  }
  for(int i=0;i<25;++i)model.reloadDispatchState();
  require(toggle->isChecked(),"ordinary renders preserve ON");
  toggle->click();
  {pgn::booking::BookingStore read(database);const auto state=read.miningSessionMetadata(gpuA);require(state&&!state->automaticAfterManual&&state->mode==pgn::mining::MiningSessionMode::ManualReservations,"OFF does not reset manual session provenance");}
  sqlite3* held{};require(sqlite3_open(database.toUtf8().constData(),&held)==SQLITE_OK,"independent fixture writer");
  require(sqlite3_exec(held,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)==SQLITE_OK,"simulate competing local writer");
  toggle->click();
  require(!toggle->isChecked()&&!message->text().startsWith("Saved"),"failed save reverts visual state and reports refusal");
  require(sqlite3_exec(held,"ROLLBACK",nullptr,nullptr,nullptr)==SQLITE_OK,"release fixture writer");sqlite3_close(held);
  toggle->click();require(toggle->isChecked(),"retry saves after real writer releases");
  observe('b',gpuB);require(toggle->isEnabled()&&!toggle->isChecked(),"another GPU has independent OFF preference");
  observe('c',gpuA);require(toggle->isChecked(),"returning GPU reloads its own preference");
  {
   pgn::booking::BookingModel reopened(temp.path());
   std::unique_ptr<QWidget> second(pgn::ui::createQueueView(&reopened,{}));
   reopened.observeRuntime(QString(32,'e'),"199932000000000000000","199933000000000000000",gpuA,"Fixture GPU","30",QDateTime::currentDateTimeUtc(),false);
   require(second->findChild<QCheckBox*>("pgn-queue-automatic-after-manual")->isChecked(),"new model/view restores saved switch");
  }
  require(QDir().mkpath(images),"image output directory");
  for(const auto width:{320,440}){
   view->resize(width,235);view->show();app.processEvents();
   require(view->rect().contains(QRect(toggle->mapTo(view.get(),QPoint()),toggle->size())),"switch remains inside compact queue layout");
   require(view->grab().save(images+QString("/queue-switch-%1.png").arg(width)),"offscreen production widget saved");
  }
  require(!toggle->toolTip().contains("public lease"),"switch never claims public lease authority");
  std::cout<<"Queue automatic switch: "<<checks<<" checks / 0 failures; production widget, temporary SQLite, offscreen only; no relay/GPU/control\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"Queue automatic switch failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}

#include "ui/catalog_layer.h"
#include "mining/source_history.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <iostream>
#include <stdexcept>

namespace {
int checks{};
void need(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class Predicate> bool until(Predicate done){QElapsedTimer timer;timer.start();while(!done()&&timer.elapsed()<10000){QCoreApplication::processEvents();QThread::msleep(1);}return done();}
}
int main(int argc,char** argv){
 QCoreApplication application(argc,argv);
 try{
  QTemporaryDir temp;need(temp.isValid(),"own temporary archive");const auto file=temp.filePath("history.sqlite");
  {pgn::mining::SourceHistory writer(file);writer.append(QString(32,'a'),0,"[phase0-gpu] GAP merit=1.9000 gap=6 lower=23 upper=29 verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown\n");}
  QObject owner;int delivered{};QJsonObject packet;
  pgn::ui::loadNativeArchiveIndex(file,owner,[&](const QJsonObject& value){need(QThread::currentThread()==application.thread(),"archive callback on owner thread");++delivered;packet=value;});
  need(delivered==0,"archive bootstrap is asynchronous");need(until([&]{return delivered==1;}),"archive arrives without SOURCE/control/network");
  need(packet.value("complete")==true&&packet.value("archive_only")==true,"qualified local archive only");
  const auto all=packet.value("all").toObject();need(all.value("length").toObject().value("points").toInteger()==1&&all.value("time").toObject().value("points").toInteger()==0,"undated retained observation is not lost or retimed");
  need(!packet.contains("current")&&!packet.contains("source_run")&&!packet.contains("ready")&&!packet.contains("snapshot_committed_offset"),"no fabricated run/cursor/conservation");
  const auto absent=temp.filePath("absent.sqlite");int missing{};QJsonObject error;
  pgn::ui::loadNativeArchiveIndex(absent,owner,[&](const auto& value){++missing;error=value;});
  need(until([&]{return missing==1;}),"missing archive completes explicitly");need(error.value("complete")==false&&!error.value("error").toString().isEmpty()&&!error.contains("all"),"missing is not a fabricated zero index");need(!QFileInfo::exists(absent),"readonly bootstrap never creates a database");
  int destroyedDelivery{};auto* destroyed=new QObject;
  pgn::ui::loadNativeArchiveIndex(file,*destroyed,[&](const auto&){++destroyedDelivery;});delete destroyed;
  need(QThreadPool::globalInstance()->waitForDone(10000),"cancelled worker terminates");QCoreApplication::processEvents();need(destroyedDelivery==0,"destroyed UI owner receives no late callback");need(delivered==1&&missing==1,"one publication per startup request, no polling");
  std::cout<<"NATIVE_ARCHIVE_BOOTSTRAP "<<checks<<" checks / 0 failures; own SQLite only, no SSH/GPU/UI\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}

#include "mining/source_history.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <iostream>

// Read-only, existing-only production-data diagnostic. Its explicit cooperative
// SQL budget bounds this experiment, not archive capacity or the UI worker.
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);const auto args=app.arguments();
 bool valid=true;const qint64 budget=args.size()==3?args[2].toLongLong(&valid):30000;
 if((args.size()!=2&&args.size()!=3)||!valid||budget<1||budget>300000){std::cerr<<"Usage: pgn-read-native-archive ABSOLUTE_EXISTING_DATABASE [AUDIT_BUDGET_MS=30000, maximum300000]\n";return 2;}
 QElapsedTimer elapsed;elapsed.start();
 try{
  pgn::mining::SourceHistoryIndexReader reader(args[1]);
  auto packet=reader.readArchive([&]{return elapsed.elapsed()>=budget;});
  packet["diagnostic_elapsed_ms"]=elapsed.elapsed();packet["diagnostic_budget_ms"]=budget;
  std::cout<<QJsonDocument(packet).toJson(QJsonDocument::Compact).constData()<<"\n";return 0;
 }catch(const std::exception& e){
  const QJsonObject failure{{"complete",false},{"archive_only",true},{"error",QString::fromUtf8(e.what()).left(400)},{"diagnostic_elapsed_ms",elapsed.elapsed()},{"diagnostic_budget_ms",budget}};
  std::cout<<QJsonDocument(failure).toJson(QJsonDocument::Compact).constData()<<"\n";return 1;
 }
}

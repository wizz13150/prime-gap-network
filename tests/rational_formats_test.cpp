#include "ui/rational_formats.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);using namespace pgn::ui;int checks=0;
 const auto need=[&](bool good){++checks;if(!good)throw std::runtime_error("Rational formatting check "+std::to_string(checks));};
 try{
  if(argc==3&&QString::fromLocal8Bit(argv[1])=="--oracle"){
   QFile file(QString::fromLocal8Bit(argv[2]));need(file.open(QIODevice::ReadOnly));const auto rows=QJsonDocument::fromJson(file.readAll()).array();QJsonArray out;
   for(const auto& row:rows){const auto v=row.toObject();out.append(QJsonObject{{"rate",reportedRateText(v.value("positions").toString(),v.value("seconds").toInteger())},{"merit",meritTargetText(v.value("q16").toInteger())}});}
   std::cout<<QJsonDocument(out).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
  }
  need(reportedRateText("600000000000000",600)=="1 Tpos/s");
  need(reportedRateText("600000000000001",600)=="≈ 1 Tpos/s");
  need(reportedRateText("1",86400)=="≈ 0.0000116 pos/s");
  need(reportedRateText("3",30)=="0.1 pos/s");need(reportedRateText("0",600)=="0 pos/s");
  need(reportedRateText("01",600)=="—");need(reportedRateText("1",0)=="—");
  need(meritTargetText(29*65536)=="29.0000");need(meritTargetText(29*65536+137)=="≈ 29.0021");
  need(meritTargetText(1)=="≈ 0.00001526");need(meritTargetText(64*65536)=="64.0000");
  need(meritTargetText(0)=="—");need(meritTargetText(64*65536+1)=="—");
  std::cout<<"PASS "<<checks<<" bounded rational display checks; exact source ratios retained\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

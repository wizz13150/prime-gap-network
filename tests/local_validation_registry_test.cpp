#include "booking/source_registry.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);unsigned checks=0;auto check=[&](bool yes){++checks;if(!yes)throw std::runtime_error("Local registry check "+std::to_string(checks));};
 try{
  QTemporaryDir root;check(root.isValid());
  const QByteArray csv="Start *1e15,End *1e15,Status,Owner\n100000,100100,Open,\n20000000000000,20000000000100,Open,\n";
  QJsonObject meta{{"schema","pgn-local-validation-registry-1"},{"authority","OPERATOR_ENGINE_VALIDATION_NO_NETWORK_ACCEPTANCE"},
   {"source","local-validation://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},{"source_sha256",QString::fromLatin1(QCryptographicHash::hash(csv,QCryptographicHash::Sha256).toHex())},
   {"observed_utc","2026-10-30T00:00:00.000Z"},{"scope_start","73786976294838206464"},{"scope_end","39614081257132168796771975168"}};
  auto write=[&](const QJsonObject& m){QFile f(root.filePath("binding.json"));check(f.open(QIODevice::WriteOnly));f.write(QJsonDocument(QJsonObject{{"community",m}}).toJson());};
  {QFile f(root.filePath("community-source.csv"));check(f.open(QIODevice::WriteOnly));f.write(csv);}
  write(meta);const auto r=pgn::booking::loadSourceRegistry(root.path());check(r.valid()&&r.localValidation&&r.ranges.size()==2);
  check(r.declaredOpenContaining("100000000000000000000","100001000000000000000"));
  check(r.declaredOpenContaining("20000000000000000000000000000","20000000000001000000000000000"));
  check(!r.declaredOpenContaining("120000000000000000000","120001000000000000000"));
  for(const auto key:{"authority","source","source_sha256","scope_start","scope_end","observed_utc"}){auto bad=meta;bad.insert(key,"wrong");write(bad);check(!pgn::booking::loadSourceRegistry(root.path()).valid());}
  auto bad=meta;bad.remove("schema");write(bad);check(!pgn::booking::loadSourceRegistry(root.path()).valid());
  std::cout<<"Explicit local validation registry PASS "<<checks<<" checks; own files, no global availability/network claim\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

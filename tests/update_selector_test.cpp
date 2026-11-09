#include "app/horizon_update_selector.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);QTemporaryDir temp;int checks=0;
 const auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Selector check "+std::to_string(checks));};
 const auto write=[](const QString& name,const QByteArray& data){QFile f(name);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())throw std::runtime_error("Fixture write failed");};
 const auto sha=[](const QByteArray& b){return QString::fromLatin1(QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex());};
 const auto base=temp.path()+"/original",store=temp.path()+"/installation",candidate=store+"/cohorts/"+QString(64,'a');QDir().mkpath(base);QDir().mkpath(candidate);
 check(pgn::app::selectedUpdateCohort(base,store,"testnet",QString(64,'5'),false).executable.isEmpty());
#ifdef Q_OS_WIN
 const QString primary="PGN-Horizon.exe";
#else
 const QString primary="PGN-Horizon";
#endif
 const QByteArray exe="INERT UI FIXTURE: NEVER EXECUTED";write(candidate+"/"+primary,exe);
 const QJsonObject file{{"path",primary},{"bytes",exe.size()},{"sha256",sha(exe)}};
 const QJsonObject component{{"id","ui"},{"version","2.0"},{"files",QJsonArray{file}}};
 QJsonObject manifest{{"schema","PGN_COMPONENT_MANIFEST_V1"},{"network","testnet"},{"components",QJsonArray{component}}};
 auto raw=QJsonDocument(manifest).toJson(QJsonDocument::Compact);write(candidate+"/components.json",raw);
 QJsonObject pointer{{"schema","PGN_ACTIVE_COHORT_V1"},{"network","testnet"},{"genesis_hash",QString(64,'5')},{"cohort",candidate},{"inventory_sha256",sha(raw)}};
 const auto publish=[&]{write(store+"/current.json",QJsonDocument(pointer).toJson(QJsonDocument::Compact));};publish();
 auto select=[&](bool health=false){return pgn::app::selectedUpdateCohort(base,store,"testnet",QString(64,'5'),health);};
 check(select().executable==candidate+"/"+primary);check(select(true).executable.isEmpty());
 pointer.insert("genesis_hash",QString(64,'6'));publish();check(!select().error.isEmpty());pointer.insert("genesis_hash",QString(64,'5'));publish();
 write(candidate+"/"+primary,"TAMPERED");check(!select().error.isEmpty());write(candidate+"/"+primary,exe);
 write(candidate+"/components.json",raw+" ");check(!select().error.isEmpty());write(candidate+"/components.json",raw);
 const auto outside=temp.path()+"/outside";QDir().mkpath(outside);
 pointer.insert("cohort",outside);publish();check(!select().error.isEmpty());
 pointer.insert("cohort",candidate);publish();check(select().error.isEmpty());
 check(pgn::app::selectedUpdateCohort(candidate,store,"testnet",QString(64,'5'),false).executable.isEmpty());
 std::cout<<"PASS "<<checks<<" update shortcut/selection integrity checks; inert files only\n";
}

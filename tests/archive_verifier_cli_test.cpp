#include <QCoreApplication>
#include <QTemporaryDir>
#include <QProcess>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <sqlite3.h>
#include <iostream>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(argc!=2)return 1;QTemporaryDir dir;
 const auto folder=dir.path()+QString::fromUtf8("/finite proof épreuve");QDir().mkpath(folder);
 const auto source=folder+"/source.sqlite",target=folder+"/proofs.sqlite";sqlite3* db=nullptr;
 if(sqlite3_open(source.toUtf8().constData(),&db)!=SQLITE_OK)return 2;
 if(sqlite3_exec(db,"CREATE TABLE gaps(id INTEGER PRIMARY KEY,p TEXT NOT NULL,q TEXT NOT NULL);"
  "INSERT INTO gaps VALUES(1,'3','5'),(2,'7','11'),(3,'11','17');",nullptr,nullptr,nullptr)!=SQLITE_OK)return 3;
 sqlite3_close(db);
 const auto invoke=[&](bool fixed)->QJsonObject{
  QProcess process;
#ifdef Q_OS_WIN
  process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments){arguments->flags|=CREATE_NO_WINDOW;});
#endif
  process.start(QString::fromLocal8Bit(argv[1]),{source,target,"0",fixed?"3":"0","2","--drain"});
  if(!process.waitForFinished(10000)||process.exitStatus()!=QProcess::NormalExit)return {};
  if(!fixed)return {{"rejected_unbounded_head",process.exitCode()==2}};
  if(process.exitCode()!=0)return {};
  const auto lines=process.readAllStandardOutput().trimmed().split('\n');if(lines.size()!=3)return {};
  return QJsonDocument::fromJson(lines.back()).object();
 };
 if(!invoke(false).value("rejected_unbounded_head").toBool())return 4;
 const auto first=invoke(true);
 if(!first.value("job_complete").toBool()||first.value("processed").toString()!="3"||
    first.value("verified").toString()!="2"||first.value("rejected").toString()!="1"||
    first.value("cpu_mining").toBool()||first.value("network_credit").toBool())return 5;
 const auto replay=invoke(true);
 if(!replay.value("job_complete").toBool()||replay.value("cached").toString()!="3"||replay.value("evaluated").toString()!="0")return 6;
 std::cout<<"PASS actual CLI finite watermark drain, Unicode paths, truthful rejected pair, no unbounded source following or cache freshness fabrication\n";
}

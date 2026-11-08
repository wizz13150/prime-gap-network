#include "wallet/audit_activity_snapshot.h"
#include <QCoreApplication>
#include <QFile>
#include <iostream>
#include <stdexcept>
using namespace pgn::wallet;
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=2)return 2;QFile file(QString::fromLocal8Bit(argv[1]));if(!file.open(QIODevice::ReadOnly))return 3;
 const auto original=QJsonDocument::fromJson(file.readAll()).object();const auto genesis=original["state"].toObject()["genesis"].toString();const auto after=original["after_height"].toInteger();QJsonArray watched;
 for(const auto& row:original["audits"].toArray())watched.append(row.toObject()["request_id"]);for(const auto& id:original["absent_watched_requests"].toArray())watched.append(id);
 auto valid=[&](const QJsonObject& value){return validateAuditActivity(value,genesis,after,64,watched).isEmpty();};int checks=0;
 auto require=[&](bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);};
 require(valid(original),qPrintable(validateAuditActivity(original,genesis,after,64,watched)));
 auto bad=original;bad["scope"]="page";require(!valid(bad),"Page presented as global");bad=original;bad["after_height"]=after+1;require(!valid(bad),"Wrong cursor accepted");
 auto state=original["state"].toObject();state["genesis"]=QString(64,'a');bad=original;bad["state"]=state;require(!valid(bad),"Wrong chain accepted");
 auto totals=original["totals"].toObject();totals["active_jobs"]=-1;bad=original;bad["totals"]=totals;require(!valid(bad),"Negative global count accepted");
 bad=original;bad["truncated"]=!original["truncated"].toBool();require(!valid(bad),"Wrong truncation accepted");
 auto rows=original["audits"].toArray();require(!rows.isEmpty(),"Real public fixture must contain terminal audits");rows.append(rows[0]);bad=original;bad["audits"]=rows;require(!valid(bad),"Duplicate report accepted");
 rows=original["audits"].toArray();auto row=rows[0].toObject();row["end"]=row["start"];rows[0]=row;bad=original;bad["audits"]=rows;require(!valid(bad),"Empty interval accepted");
 rows=original["audits"].toArray();row=rows[0].toObject();auto jobs=row["jobs"].toArray();require(!jobs.isEmpty(),"Fixture missing real jobs");auto job=jobs[0].toObject();job["passed_height"]=original["state"].toObject()["height"].toInteger()+1;jobs[0]=job;row["jobs"]=jobs;rows[0]=row;bad=original;bad["audits"]=rows;require(!valid(bad),"Future passed height accepted");
 bad=original;bad["absent_watched_requests"]=QJsonArray{QString(64,'a')};require(!valid(bad),"Unexpected absent request accepted");
 bad=original;bad["audits"]=QJsonArray{};require(!valid(bad),"Watched terminal reports silently omitted");bad=original;bad["padding"]=QString(524288,'x');require(!valid(bad),"Oversized public projection accepted");
 std::cout<<"PASS "<<checks<<" native audit snapshot checks against actual public block replay\n";
}

#include "wallet/public_work_snapshot.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
using namespace pgn::wallet;
static QJsonObject sample(){const QString id(64,'1');return {{"schema","PGN_WORK_VIEW_V1"},{"kind","tickets"},{"offset",0},{"limit",1},{"request_id",id},{"state",QJsonObject{{"genesis",QString(64,'2')},{"bestblockhash",QString(64,'3')},{"height",100},{"leases",QJsonArray{QJsonObject{{"request",id}}}}}},{"reports",QJsonArray{QJsonObject{{"request_id",id},{"report_root",QString(64,'4')},{"block",QString(64,'5')},{"height",90}}}},{"audits",QJsonArray{QJsonObject{{"request_id",id},{"jobs",QJsonArray{QJsonObject{{"slot",0},{"issued",95}}}}}}}};}
int main(int argc,char** argv){QCoreApplication app(argc,argv);const QString genesis(64,'2'),id(64,'1');auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};auto valid=[&](const QJsonObject& v){return validatePublicWorkSnapshot(v,genesis,"tickets",0,1,id).isEmpty();};const auto original=sample();require(valid(original),"Coherent public fixture refused");
 auto bad=original;bad["kind"]="audits";require(!valid(bad),"Wrong view kind accepted");bad=original;bad["request_id"]=QString(64,'6');require(!valid(bad),"Wrong exact request accepted");
 auto state=original["state"].toObject();state["genesis"]=QString(64,'7');bad=original;bad["state"]=state;require(!valid(bad),"Other chain accepted");state=original["state"].toObject();state["height"]=99.5;bad["state"]=state;require(!valid(bad),"Fractional anchor accepted");
 auto reports=original["reports"].toArray();auto row=reports[0].toObject();row["height"]=101;reports[0]=row;bad=original;bad["reports"]=reports;require(!valid(bad),"Future report mixed into older anchor");
 reports=original["reports"].toArray();row=reports[0].toObject();row["report_root"]=QString(64,'0');reports[0]=row;bad=original;bad["reports"]=reports;require(!valid(bad),"Zero report root accepted");
 auto audits=original["audits"].toArray();row=audits[0].toObject();row["request_id"]=QString(64,'8');audits[0]=row;bad=original;bad["audits"]=audits;require(!valid(bad),"Audit for another report accepted");
 audits=original["audits"].toArray();row=audits[0].toObject();auto jobs=row["jobs"].toArray();jobs.append(jobs[0]);row["jobs"]=jobs;audits[0]=row;bad=original;bad["audits"]=audits;require(!valid(bad),"Duplicate audit slot accepted");
 audits=original["audits"].toArray();row=audits[0].toObject();jobs=row["jobs"].toArray();auto job=jobs[0].toObject();job["issued"]=101;jobs[0]=job;row["jobs"]=jobs;audits[0]=row;bad=original;bad["audits"]=audits;require(!valid(bad),"Future audit mixed into older anchor");
 bad=original;bad["padding"]=QString(524288,'x');require(!valid(bad),"Oversized view accepted");
 auto empty=original;empty["reports"]=QJsonArray{};empty["audits"]=QJsonArray{};require(valid(empty),"Pending report without assigned audits refused");
 auto page=original;page.remove("request_id");page["kind"]="audits";page["limit"]=100;require(validatePublicWorkSnapshot(page,genesis,"audits",0,100).isEmpty(),"Public audit page refused");
 auto labeled=original;audits=labeled["audits"].toArray();row=audits[0].toObject();row.insert("public_attribution",QJsonObject{{"schema","PGN_PUBLIC_ATTRIBUTION_V1"},{"name","Alice"}});audits[0]=row;labeled.insert("audits",audits);require(valid(labeled),"Optional public attribution broke authenticated snapshot");require(!row.value("audited").toBool(),"A public label must not confer audit acceptance");
 std::cout<<"PASS public work snapshot: chain/request binding, bounded page, exact report/audit matching, no future mixing, pending work retained, optional public label grants no audit acceptance\n";
}

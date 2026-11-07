#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
namespace pgn::wallet {
inline QString validatePublicWorkSnapshot(const QJsonObject& view,const QString& genesis,const QString& kind,int offset,int limit,const QString& request={}){
 static const QRegularExpression hash("^[0-9a-f]{64}$");
 const auto canonical=[&](const QJsonValue& v){return v.isString()&&hash.match(v.toString()).hasMatch()&&v.toString()!=QString(64,'0');};
 const auto integer=[](const QJsonValue& value,qint64 minimum,qint64 maximum){const auto n=value.toInteger(-1);return value.isDouble()&&n>=minimum&&n<=maximum&&value.toDouble()==double(n);};
 if(kind!="tickets"&&kind!="audits")return "Unsupported work view";
 if(view.value("schema")!="PGN_WORK_VIEW_V1"||view.value("kind")!=kind||view.value("offset").toInteger(-1)!=offset||view.value("limit").toInteger(-1)!=limit||offset<0||limit<1||limit>100)return "Work view identity or page bound changed";
 if((request.isEmpty()&&view.contains("request_id"))||(!request.isEmpty()&&view.value("request_id")!=request))return "Work request identity changed";
 const auto state=view.value("state").toObject();if(state.value("genesis")!=genesis||!canonical(state.value("bestblockhash"))||!integer(state.value("height"),0,2147483647))return "Work view chain anchor invalid";
 if(!state.value("leases").isArray()||!view.value("reports").isArray()||!view.value("audits").isArray())return "Work view arrays unavailable";
 const auto leases=state.value("leases").toArray(),reports=view.value("reports").toArray(),audits=view.value("audits").toArray();if(leases.size()>100||reports.size()>limit||audits.size()>limit)return "Work view exceeds page bounds";
 const auto accepts=[&](const QJsonValue& id){return canonical(id)&&(request.isEmpty()||id.toString()==request);};
 QSet<QString> leaseIds,reportIds,auditIds;
 for(const auto& value:leases){const auto row=value.toObject();const auto id=row.value("request");if(!accepts(id)||leaseIds.contains(id.toString()))return "Lease request identity invalid";leaseIds.insert(id.toString());}
 for(const auto& value:reports){const auto row=value.toObject();const auto id=row.value("request_id");if(!accepts(id)||reportIds.contains(id.toString())||!canonical(row.value("report_root"))||!canonical(row.value("block"))||!integer(row.value("height"),0,state.value("height").toInteger()))return "Report identity or anchor invalid";reportIds.insert(id.toString());}
 for(const auto& value:audits){const auto row=value.toObject();const auto id=row.value("request_id");if(!accepts(id)||!reportIds.contains(id.toString())||auditIds.contains(id.toString())||!row.value("jobs").isArray()||row.value("jobs").toArray().size()>2)return "Audit report binding invalid";auditIds.insert(id.toString());QSet<int> auditSlots;for(const auto& item:row.value("jobs").toArray()){const auto job=item.toObject();if(!integer(job.value("slot"),0,1)||auditSlots.contains(job.value("slot").toInt())||!integer(job.value("issued"),0,state.value("height").toInteger()))return "Audit slot or issue height invalid";auditSlots.insert(job.value("slot").toInt());}}
 if(QJsonDocument(view).toJson(QJsonDocument::Compact).size()>524288)return "Work view exceeds byte budget";
 return {};
}
}

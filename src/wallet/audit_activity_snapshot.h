#pragma once
#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace pgn::wallet {
inline QString validateAuditActivity(const QJsonObject& value,const QString& genesis,qint64 after,int limit,const QJsonArray& watched){
 static const QRegularExpression hash("^[0-9a-f]{64}$"),script("^[0-9a-f]+$"),decimal("^[1-9][0-9]{0,28}$");
 const auto canonical=[&](const QJsonValue& v){return v.isString()&&hash.match(v.toString()).hasMatch()&&v.toString()!=QString(64,'0');};
 const auto integer=[](const QJsonValue& v,qint64 low,qint64 high){const auto n=v.toInteger(-1);return v.isDouble()&&n>=low&&n<=high&&v.toDouble()==double(n);};
 const auto greater=[](const QString& a,const QString& b){return a.size()>b.size()||(a.size()==b.size()&&a>b);};
 if(after<0||limit<1||limit>100||watched.size()>limit||value.value("schema")!="PGN_AUDIT_ACTIVITY_V1"||value.value("scope")!="global_counts_with_bounded_public_job_rows"||value.value("after_height").toInteger(-1)!=after||!value.value("rebase").isBool()||!value.value("truncated").isBool())return "Audit activity identity or bounds changed";
 const auto state=value.value("state").toObject();const auto height=state.value("height").toInteger(-1);
 if(state.value("genesis")!=genesis||!canonical(state.value("bestblockhash"))||!integer(state.value("height"),0,2147483647)||!integer(state.value("median_time_past"),0,9007199254740991LL))return "Audit activity chain anchor invalid";
 const auto totals=value.value("totals").toObject();for(const auto* k:{"active_reports","active_jobs","recent_reports","passed_audits_since_height","selected_active_reports","selected_recent_reports"})if(!integer(totals.value(k),0,9007199254740991LL))return "Audit activity totals invalid";
 if(totals.value("active_jobs").toInteger()<totals.value("active_reports").toInteger()||totals.value("active_jobs").toInteger()>2*totals.value("active_reports").toInteger())return "Audit activity global slot count inconsistent";
 if(!value.value("audits").isArray()||!value.value("absent_watched_requests").isArray()||value.value("audits").toArray().size()>limit)return "Audit activity rows exceed bound";
 QSet<QString> requested,seen,absent;for(const auto& id:watched){if(!canonical(id)||requested.contains(id.toString()))return "Invalid watched audit identity";requested.insert(id.toString());}
 for(const auto& id:value.value("absent_watched_requests").toArray()){if(!canonical(id)||!requested.contains(id.toString())||absent.contains(id.toString()))return "Unexpected absent audit identity";absent.insert(id.toString());}
 qint64 activeRows=0,recentRows=0;
 for(const auto& item:value.value("audits").toArray()){
  const auto row=item.toObject();const auto id=row.value("request_id").toString(),start=row.value("start").toString(),end=row.value("end").toString();
  if(!canonical(row.value("request_id"))||seen.contains(id)||absent.contains(id)||!row.value("audited").isBool()||!row.value("jobs").isArray()||row.value("jobs").toArray().size()>2||!decimal.match(start).hasMatch()||!decimal.match(end).hasMatch()||!greater(end,start))return "Audit report identity or interval invalid";seen.insert(id);
  QSet<int> auditSlots;bool active=false,recent=false;
  for(const auto& entry:row.value("jobs").toArray()){
   const auto job=entry.toObject();const auto phase=job.value("state").toString();
   if(!integer(job.value("slot"),0,1)||auditSlots.contains(job.value("slot").toInt()))return "Audit job slot invalid or duplicated";
   if(!integer(job.value("attempt"),0,4294967295LL)||!integer(job.value("issued"),0,height))return "Audit attempt or issue height invalid";
   const auto actor=job.value("actor").toString();if(actor.size()<2||actor.size()>8192||actor.size()%2||!script.match(actor).hasMatch())return "Audit actor script invalid";
   if(!job.value("expired").isBool()||(phase!="assigned"&&phase!="committed"&&phase!="passed"&&phase!="failed"))return "Audit job state invalid";
   auditSlots.insert(job.value("slot").toInt());if(job.contains("passed_height")&&!integer(job.value("passed_height"),1,height))return "Audit accepted height invalid";
   active|=phase=="committed"||(phase=="assigned"&&!job.value("expired").toBool());recent|=!value.value("rebase").toBool()&&std::max(job.value("issued").toInteger(),job.value("passed_height").toInteger())>after;
  }
  activeRows+=active;recentRows+=recent;
 }
 for(const auto& id:requested)if(!seen.contains(id)&&!absent.contains(id))return "Watched audit response incomplete";
 if(activeRows!=totals.value("selected_active_reports").toInteger()||recentRows!=totals.value("selected_recent_reports").toInteger()||activeRows>totals.value("active_reports").toInteger()||recentRows>totals.value("recent_reports").toInteger())return "Audit selection counts inconsistent";
 const bool truncated=totals.value("active_reports").toInteger()>activeRows||totals.value("recent_reports").toInteger()>recentRows;if(value.value("truncated").toBool()!=truncated)return "Audit sampling limit inconsistent";
 if(QJsonDocument(value).toJson(QJsonDocument::Compact).size()>524288)return "Audit activity exceeds byte budget";
 return {};
}
}

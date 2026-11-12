#pragma once
#include "social/social_data.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
namespace pgn::social {
inline bool leaderboardArguments(const ReadRequest& request,QJsonArray& args){
 if(request.section!=Section::Miners||request.window!=Window::AllTime||request.limit<1||request.limit>100)return false;
 qint64 offset=0;QString anchor;
 if(!request.cursor.isEmpty()){
  const auto parts=request.cursor.split(':');bool ok=false;
  if(parts.size()!=2||!QRegularExpression("^[0-9a-f]{64}$").match(parts[0]).hasMatch())return false;
  offset=parts[1].toLongLong(&ok);if(!ok||offset<0||QString::number(offset)!=parts[1])return false;anchor=parts[0];
 }
 args={offset,request.limit};if(!anchor.isEmpty())args.append(anchor);return true;
}
inline QString decodeLeaderboard(const ReadRequest& request,const QJsonObject& value,Snapshot& out){
 const QRegularExpression hex("^[0-9a-f]{64}$"),decimal("^(0|[1-9][0-9]{0,255})$");QJsonArray args;
 if(!leaderboardArguments(request,args)||value.value("schema")!="PGN_LEADERBOARD_V1"||value.value("genesis")!=request.identity.genesis||!hex.match(value.value("bestblockhash").toString()).hasMatch()||!value.value("rows").isArray()||value.value("rows").toArray().size()>request.limit)return "Invalid authenticated leaderboard page.";
 const auto tip=value.value("bestblockhash").toString();const auto height=value.value("height").toInteger(-1),offset=args[0].toInteger(),next=value.value("next_offset").toInteger(-1),total=value.value("total").toInteger(-1);
 const auto rows=value.value("rows").toArray();
 if(height<0||value.value("offset").toInteger(-1)!=offset||next!=offset+rows.size()||total<next||!value.value("more").isBool()||value.value("more").toBool()!=(next<total)||(value.value("more").toBool()&&rows.isEmpty())||(args.size()==3&&args[2].toString()!=tip))return "Leaderboard snapshot or pagination changed; refresh the ranking.";
 out.identity=request.identity;out.section=Section::Miners;out.window=Window::AllTime;out.authenticated=true;out.observedUtc=QDateTime::currentDateTimeUtc();out.source="PGN verified chain · block "+QString::number(height)+" · "+tip;
 const Provenance provenance{request.identity.genesis,out.source,out.observedUtc,true};QSet<QString> seen;
 for(const auto& entry:rows){
  const auto row=entry.toObject();const auto id=row.value("operator_id").toString();if(!hex.match(id).hasMatch()||seen.contains(id))return "Invalid or repeated operator identity.";seen.insert(id);
  Miner miner;miner.id=id;miner.name="Miner "+id.left(8)+"…"+id.right(6);
  // The signed report label is public, self-declared and non-unique. The
  // canonical operator ID remains the sole row/permission identity.
  const auto label=row.value("display_name");
  if(label.isString()){
   const auto name=label.toString().trimmed();bool safe=!name.isEmpty()&&name.toUtf8().size()<=192;
   for(const auto ch:name)if(ch.isNull()||ch.category()==QChar::Other_Control||ch.category()==QChar::Other_Format||ch.category()==QChar::Separator_Line||ch.category()==QChar::Separator_Paragraph)safe=false;
   if(safe)miner.name=name+" · "+id.left(8)+"…"+id.right(6);
  }

  for(const auto& item:QList<QPair<QString,Counter*>>{{"tickets_reported",&miner.ticketsReported},{"tickets_validated",&miner.ticketsValidated},{"audits_finalized_valid",&miner.validatedAudits},{"validated_search_positions",&miner.validatedSearchPositions}}){const auto raw=row.value(item.first);if(!raw.isString()||!decimal.match(raw.toString()).hasMatch())return "An exact miner counter is unavailable.";*item.second={raw.toString(),provenance};}
  const auto last=row.value("last_activity_height").toInteger(-1);if(last<0||last>height)return "Invalid last-activity height.";miner.lastActivityHeight={QString::number(last),provenance};out.miners.append(miner);
 }
 if(value.value("more").toBool())out.nextCursor=tip+":"+QString::number(next);
 return {};
}
}

#pragma once
#include "booking/decimal_range.h"
#include <QJsonObject>
#include <QVector>
#include <algorithm>
namespace pgn::ui {
struct ZoneSourceCoverage {QString covered;double fraction{};bool complete{};};
// Merge exact completed source intervals before projecting pixels. A rounded
// fraction of 1.0 never proves complete coverage; exact endpoints own that fact.
inline ZoneSourceCoverage zoneSourceCoverage(const QJsonObject& zone,const QVector<QJsonObject>& pieces){
 using namespace pgn::booking;const auto first=zone["start"].toString(),last=zone["end"].toString();
 if(compareUnsigned(first,last)>=0)return {"0",0,false};QVector<QPair<QString,QString>> intervals;
 for(const auto& row:pieces)if(row["status"].toString()=="LOCAL_SOURCE_FINISHED"){
  const auto a=row["start"].toString(),b=row["end"].toString();if(compareUnsigned(a,b)>=0||!overlaps(first,last,a,b))continue;
  intervals.append({compareUnsigned(a,first)<0?first:a,compareUnsigned(b,last)>0?last:b});
 }
 std::sort(intervals.begin(),intervals.end(),[](const auto& a,const auto& b){return compareUnsigned(a.first,b.first)<0;});
 QVector<QPair<QString,QString>> merged;for(const auto& span:intervals){if(!merged.isEmpty()&&compareUnsigned(span.first,merged.last().second)<=0){if(compareUnsigned(span.second,merged.last().second)>0)merged.last().second=span.second;}else merged.append(span);}
 QString covered="0";for(const auto& span:merged)covered=addUnsigned(covered,subtractUnsigned(span.second,span.first));
 const bool complete=merged.size()==1&&merged.first().first==first&&merged.first().second==last;
 return {covered,rangeFraction(covered,"0",subtractUnsigned(last,first)),complete};
}
}

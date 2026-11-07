#pragma once
#include "wallet/participant_snapshot.h"
#include <QJsonDocument>
#include <cmath>

namespace pgn::wallet {
inline bool metricInteger(const QJsonValue& value,qint64 maximum=9007199254740991LL){
    const auto number=value.toDouble(-1);
    return value.isDouble()&&std::isfinite(number)&&number>=0&&number<=maximum&&std::floor(number)==number;
}
inline bool metricPositions(const QJsonValue& value){
    const auto text=value.toString();
    return value.isString()&&!text.isEmpty()&&text.size()<=48&&(text.size()==1||text[0]!='0')&&
        std::all_of(text.begin(),text.end(),[](QChar c){return c>='0'&&c<='9';});
}
inline bool metricLessEqual(const QString& left,const QString& right){
    return left.size()<right.size()||(left.size()==right.size()&&left<=right);
}
inline QString validateNetworkMetrics(const QJsonObject& state,const QString& genesis){
    const auto hash=state.value("bestblockhash").toString();
    const auto points=state.value("points").toArray();
    if(QJsonDocument(state).toJson(QJsonDocument::Compact).size()>131072||state.value("schema")!="PGN_NETWORK_METRICS_V1"||
       state.value("genesis")!=genesis||hash.size()!=64||!canonicalHex(hash,64)||
       !metricInteger(state.value("height"),2147483646)||!metricInteger(state.value("median_time_past"))||
       state.value("rate_scope")!="reported_search_positions_per_chain_second_excludes_audits"||
       state.value("audit_validation_scope")!="historical_reports_qualified_at_snapshot_tip"||
       !state.value("points").isArray()||points.isEmpty()||points.size()>256||
       !metricInteger(state.value("window_seconds"),86400)||state.value("window_seconds").toInteger()<30||
       !metricInteger(state.value("span_seconds"),604800)||state.value("span_seconds").toInteger()<60)
        return "Network metrics identity, units or bounds invalid";
    qint64 lastTime=-1,lastHeight=-1,lastGold=-1;
    for(const auto& value:points){
        const auto point=value.toObject();const auto block=point.value("hash").toString();
        const auto time=point.value("time").toInteger(-1),height=point.value("height").toInteger(-1),gold=point.value("gold_count").toInteger(-1);
        if(!value.isObject()||!metricInteger(point.value("time"))||time<=lastTime||time>state.value("median_time_past").toInteger()||
           !metricInteger(point.value("height"),state.value("height").toInteger())||height<lastHeight||
           !metricInteger(point.value("gold_count"),height)||gold<lastGold||block.size()!=64||!canonicalHex(block,64)||
           !metricInteger(point.value("merit_target_q16"),64*65536)||point.value("merit_target_q16").toInteger()<1||
           !metricInteger(point.value("window_seconds"),state.value("window_seconds").toInteger())||
           !metricPositions(point.value("reported_positions"))||!metricPositions(point.value("audited_positions"))||
           !metricLessEqual(point.value("audited_positions").toString(),point.value("reported_positions").toString()))
            return "Network metrics sample is not canonical or ordered";
        lastTime=time;lastHeight=height;lastGold=gold;
    }
    const auto final=points.last().toObject();
    if(final.value("hash")!=hash||lastHeight!=state.value("height").toInteger()||lastTime!=state.value("median_time_past").toInteger())
        return "Network metrics endpoint is not the declared chain tip";
    return {};
}
}

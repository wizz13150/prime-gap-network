#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>
#include "rational_formats.h"
#include <boost/multiprecision/cpp_int.hpp>

namespace pgn::ui {
// Presentation only: these are report arrivals/permissions, not GPU capacity or
// an online headcount. A missing/stale observation never becomes a zero rate.
inline QJsonObject networkSummaryRows(const QJsonObject& value){
    const QJsonObject missing{{"available",false},{"rows",QJsonArray{}}};
    if(value.value("schema")!="PGN_NETWORK_SUMMARY_V1"||value.value("available")!=true
       ||!QRegularExpression("^[0-9a-f]{64}$").match(value.value("parent").toString()).hasMatch()
       ||!QRegularExpression("^[0-9a-f]{64}$").match(value.value("genesis").toString()).hasMatch()
       ||value.value("rate_scope")!="reported_search_excludes_audits_not_hardware_capacity")return missing;
    const auto integer=[](const QJsonValue& v){const double n=v.toDouble(-1);return v.isDouble()&&std::isfinite(n)&&n>=0&&n<=9007199254740991.&&std::floor(n)==n;};
    for(const auto* key:{"registered_operators","reporting_operators_in_window","pending_audits","merit_target_q16","window_seconds"})if(!integer(value.value(key)))return missing;
    const auto seconds=value.value("window_seconds").toInteger(),target=value.value("merit_target_q16").toInteger();
    const auto positions=value.value("reported_positions").toString();
    if(seconds<30||seconds>86400||target<1||target>64*65536||!QRegularExpression("^(0|[1-9][0-9]{0,37})$").match(positions).hasMatch())return missing;
    const QString rateText=reportedRateText(positions,seconds);
    QJsonArray rows;
    const auto add=[&](QString label,QString text){rows.append(QJsonObject{{"label",label},{"value",text}});};
    add("Registered operators",QString::number(value.value("registered_operators").toInteger()));
    add("Reporting miners",QString::number(value.value("reporting_operators_in_window").toInteger()));
    add("Reported search",rateText);
    add("Gold merit",meritTargetText(target));
    add("Audits pending",QString::number(value.value("pending_audits").toInteger()));
    return {{"available",true},{"rows",rows},{"window_seconds",seconds},{"parent",value.value("parent")},
            {"scope","Reported search excludes audits; registrations are not online counts"}};
}
inline QJsonObject networkMiningSummary(const QJsonObject& packet,qint64 receivedAgeMs=0){
    const QJsonObject missing{{"available",false},{"rows",QJsonArray{}}};
    const auto observation=packet.value("network_admission").toObject();const auto age=observation.value("age_seconds");
    if(packet.value("source_stale")==true||observation.value("schema")!="PGN_NORMAL_MINER_ADMISSION_OBSERVATION_V1"
       ||observation.value("state")!="ready"||observation.value("available")!=true||!age.isDouble()
       ||!std::isfinite(age.toDouble())||age.toDouble()<0||receivedAgeMs<0||age.toDouble()+double(receivedAgeMs)/1000.>90.)return missing;
    const auto status=observation.value("status").toObject(),value=status.value("network_summary").toObject();
    if(status.value("schema")!="PGN_NORMAL_MINER_JOIN_STATUS_V1"||value.value("parent")!=status.value("parent")||value.value("genesis")!=status.value("genesis"))return missing;
    return networkSummaryRows(value);
}
// A verified Core snapshot has its own clock: pausing a producer does not pause
// the network. Reuse the same row formatter and strict numeric admission.
inline QJsonObject networkMiningCoreSummary(const QJsonObject& metrics,const QString& expectedGenesis,qint64 receivedAgeMs=0){
    const QJsonObject missing{{"available",false},{"rows",QJsonArray{}}};
    const auto integer=[](const QJsonValue& v){const double n=v.toDouble(-1);return v.isDouble()&&std::isfinite(n)&&n>=0&&n<=9007199254740991.&&std::floor(n)==n;};
    const auto points=metrics.value("points").toArray();const auto tip=points.isEmpty()?QJsonObject{}:points.last().toObject();
    if(receivedAgeMs<0||receivedAgeMs>90000||metrics.value("schema")!="PGN_NETWORK_METRICS_V1"
       ||metrics.value("genesis")!=expectedGenesis||points.isEmpty()||points.size()>256
       ||metrics.value("rate_scope")!="reported_search_positions_per_chain_second_excludes_audits"
       ||metrics.value("audit_validation_scope")!="historical_reports_qualified_at_snapshot_tip"
       ||!integer(metrics.value("height"))||!integer(metrics.value("median_time_past"))
       ||tip.value("hash")!=metrics.value("bestblockhash")||tip.value("height")!=metrics.value("height")
       ||tip.value("time")!=metrics.value("median_time_past"))return missing;
    const auto reported=tip.value("reported_positions").toString(),audited=tip.value("audited_positions").toString();
    const QRegularExpression exact("^(0|[1-9][0-9]{0,37})$");
    if(!exact.match(reported).hasMatch()||!exact.match(audited).hasMatch()||audited.size()>reported.size()
       ||(audited.size()==reported.size()&&audited>reported))return missing;
    qint64 pending=0;const auto queue=metrics.value("audit_queue").toObject();
    for(const auto* key:{"awaiting","awaiting_seed","assigned","committed"}){if(!integer(queue.value(key)))return missing;pending+=queue.value(key).toInteger();}
    QJsonObject value{{"schema","PGN_NETWORK_SUMMARY_V1"},{"available",true},{"parent",metrics.value("bestblockhash")},{"genesis",expectedGenesis},
        {"rate_scope","reported_search_excludes_audits_not_hardware_capacity"},{"registered_operators",metrics.value("audit_enabled_operators")},
        {"reporting_operators_in_window",metrics.value("reporting_operators_in_window")},{"pending_audits",pending},
        {"window_seconds",tip.value("window_seconds")},{"merit_target_q16",tip.value("merit_target_q16")},{"reported_positions",reported}};
    return networkSummaryRows(value);
}
// Coverage comes only from the authenticated Core snapshot, never from a page
// of tickets or an assumption that all reports passed independent audits.
inline QJsonObject networkBitCoverage(const QJsonObject& metrics,const QString& genesis,int selectedBits,qint64 ageMs=0){
    const QJsonObject missing{{"available",false}};
    if(!networkMiningCoreSummary(metrics,genesis,ageMs).value("available").toBool())return missing;
    const auto coverage=metrics.value("range_coverage").toObject();const auto rows=coverage.value("bands").toArray();
    if(coverage.value("schema")!="PGN_BIT_RANGE_COVERAGE_V1"||coverage.value("scope")!="unique_reported_ranges_audited_subset_excluded_prefix_separate"
       ||!coverage.value("bands").isArray()||rows.size()>29)return missing;
    using Integer=boost::multiprecision::cpp_int;
    const auto number=[](const QJsonValue& value){
        const auto text=value.toString();
        if(!value.isString()||!QRegularExpression("^(0|[1-9][0-9]{0,38})$").match(text).hasMatch())throw std::invalid_argument("Coverage decimal");
        return Integer(text.toStdString());
    };
    try{
        const auto origin=number(coverage.value("origin")),limit=number(coverage.value("limit"));
        if(limit<=origin||limit>(Integer{1}<<128))return missing;
        int previous=0;QJsonObject selected;
        for(const auto& item:rows){
            const auto row=item.toObject();const auto bitsValue=row.value("bits");const int bits=bitsValue.toInt();
            if(!bitsValue.isDouble()||bitsValue.toDouble()!=bits||bits<67||bits>95||bits<=previous)return missing;
            previous=bits;
            const Integer low=Integer{1}<<(bits-1),high=Integer{1}<<bits,start=std::max(origin,low),end=std::min(limit,high);
            if(end<=start||number(row.value("lower"))!=low||number(row.value("upper"))!=high
               ||number(row.value("search_start"))!=start||number(row.value("search_end"))!=end)return missing;
            const auto excluded=number(row.value("excluded_prefix")),reported=number(row.value("reported_positions")),audited=number(row.value("audited_positions"));
            if(excluded!=start-low||reported>end-start||audited>reported)return missing;
            for(const auto& entry:std::initializer_list<std::pair<const char*,Integer>>{{"excluded_ppb",excluded},{"reported_ppb",reported},{"audited_ppb",audited}}){
                const auto value=row.value(entry.first);const auto exact=(entry.second*1000000000/(high-low)).convert_to<qint64>();
                if(!value.isDouble()||!std::isfinite(value.toDouble())||value.toDouble()!=double(exact))return missing;
            }
            if(bits==selectedBits)selected=row;
        }
        if(selected.isEmpty())return missing;
        selected.insert("available",true);return selected;
    }catch(const std::exception&){return missing;}
}
inline QString coveragePercent(qint64 ppb,bool positive=false){
    if(ppb<0||ppb>1000000000)return "â€”";
    if(ppb==0)return positive?"<0.0000001%":"0%";
    QString value=QString::number(double(ppb)/10000000.,'f',7);
    while(value.endsWith('0'))value.chop(1);if(value.endsWith('.'))value.chop(1);return value+"%";
}
}

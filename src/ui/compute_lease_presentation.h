#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QTimeZone>
namespace pgn::ui {
inline QString computeLeaseDeadlineText(const QJsonObject& lease,const QTimeZone& zone=QTimeZone::systemTimeZone()){
    const auto clockValue=lease.value("deadline_clock");
    if(lease.contains("deadline_clock")&&!clockValue.isString())return "Unavailable";
    const auto clock=lease.contains("deadline_clock")?clockValue.toString():QString("ledger_height");
    if(clock=="ledger_height"){
        const auto value=lease.value("deadline");const auto height=value.toInteger(-1);
        return value.isDouble()&&height>0?QString("Ledger record %1").arg(height):QString("Unavailable");
    }
    if(clock=="median_chain_time"){
        const auto value=lease.value("deadline_time");const auto seconds=value.toInteger(-1);
        if(!value.isDouble()||seconds<=0)return "Unavailable";
        const auto date=QDateTime::fromSecsSinceEpoch(seconds,zone);
        return date.isValid()?date.toString("yyyy-MM-dd HH:mm"):QString("Unavailable");
    }
    return "Unavailable";
}
inline QString computeLeaseDeadlineHeading(const QJsonObject& lease){
    if(lease.contains("report_root"))return "Original compute lease (historical)";
    if(lease.value("deadline_clock")=="median_chain_time")return "Compute lease ends at chain time (local)";
    if(!lease.contains("deadline_clock")||lease.value("deadline_clock")=="ledger_height")return "Compute lease ends at ledger record";
    return "Compute lease deadline";
}
}

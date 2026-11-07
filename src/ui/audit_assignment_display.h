#pragma once
#include <QDateTime>
#include <QTimeZone>
#include <QJsonObject>
#include <QPair>
namespace pgn::ui {
inline QPair<QString,QString> auditAssignmentLeaseLine(const QJsonObject& job){
    if(job.value("deadline_clock")=="median_chain_time"&&job.value("deadline_time").isDouble()){
        const auto until=QDateTime::fromSecsSinceEpoch(job.value("deadline_time").toInteger(),QTimeZone::utc()).toLocalTime();
        return {"Assignment lease · chain time",until.isValid()?until.toString("yyyy-MM-dd HH:mm t"):QString("Unavailable")};
    }
    return {"Assignment lease · legacy ledger height",job.value("deadline").isDouble()?QString::number(job.value("deadline").toInteger()):QString("Unavailable")};
}
}

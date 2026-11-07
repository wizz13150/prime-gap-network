#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>

namespace pgn::ui {
struct TicketFlowEvidence {bool included{},audited{};QString block;};
// Consume only CoreRuntime's authenticated, current-branch readTicketDetails
// projection. A local computation or report inclusion is not an audit verdict.
inline TicketFlowEvidence ticketFlowEvidence(const QString& request,const QString& ticket,
                                             const QJsonObject& state,const QString& error){
 const QRegularExpression hash("^[0-9a-f]{64}$");
 const auto valid=[&](const QString& value){return hash.match(value).hasMatch()&&value!=QString(64,'0');};
 if(!error.isEmpty()||!valid(request))return {};
 const auto reports=state.value("reports").toArray();
 if(reports.size()!=1)return {};
 const auto report=reports.first().toObject();
 if(report.value("request_id").toString()!=request||!valid(report.value("block").toString())||
    !valid(report.value("report_root").toString()))return {};
 if(report.contains("ticket")&&report.value("ticket").toString()!=ticket)return {};
 TicketFlowEvidence result{true,false,report.value("block").toString()};
 const auto audits=state.value("audits").toArray();
 if(audits.size()==1){const auto verdict=audits.first().toObject();
  result.audited=verdict.value("request_id").toString()==request&&verdict.value("audited").isBool()&&
      verdict.value("audited").toBool()&&valid(verdict.value("details_root").toString());
 }
 return result;
}
}

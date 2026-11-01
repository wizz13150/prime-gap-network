#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace pgn::ui {
// A common volume unit, not a count of accepted tickets or mined blocks.
// The denominator comes from the verified network's ticket-width rule.
inline QJsonObject ticketProductivity(const QJsonArray& devices,const QString& width){
    static const QRegularExpression decimal("^[1-9][0-9]{0,28}$");
    bool valid=false;const double positions=width.toDouble(&valid);
    valid=valid&&decimal.match(width).hasMatch()&&std::isfinite(positions)&&positions>0;
    QJsonArray rows;QSet<QString> seen;double total=0;bool complete=valid;int active=0;
    for(const auto& value:devices){
        const auto device=value.toObject();const auto id=device.value("id").toString();
        if(id.isEmpty()||seen.contains(id))continue;seen.insert(id);
        auto row=device;row.remove("positions_per_second");row.insert("tickets_per_hour",QJsonValue{});
        if(device.value("active")==true){
            ++active;const auto raw=device.value("positions_per_second");const double rate=raw.toDouble(-1);
            const double normalized=valid?rate/positions*3600.:0.;
            if(valid&&raw.isDouble()&&std::isfinite(rate)&&rate>=0&&std::isfinite(normalized)){
                row.insert("tickets_per_hour",normalized);total+=normalized;
            }else complete=false;
        }
        rows.append(row);
    }
    QJsonArray groups;
    for(const auto& kind:{QString("GPU"),QString("CPU")}){
        double sum=0;bool known=valid;int count=0;
        for(const auto& value:rows){const auto row=value.toObject();if(row.value("kind")!=kind||row.value("active")!=true)continue;++count;
            if(row.value("tickets_per_hour").isDouble())sum+=row.value("tickets_per_hour").toDouble();else known=false;}
        groups.append(QJsonObject{{"label",kind},{"active",count>0},{"tickets_per_hour",known&&count?QJsonValue(sum):QJsonValue{}}});
    }
    return {{"ticket_width",valid?QJsonValue(width):QJsonValue{}},{"devices",rows},{"groups",groups},
        {"active_devices",active},{"total_tickets_per_hour",complete&&active&&std::isfinite(total)?QJsonValue(total):QJsonValue{}},
        {"scope","Live volume estimate; includes audit computation, excludes waiting and network acceptance"}};
}
}

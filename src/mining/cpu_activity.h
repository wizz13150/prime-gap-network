#pragma once
#include <QJsonObject>
namespace pgn::mining {
// A resident calculation remains active while its next closed piece is being
// queued. Instantaneous queue occupancy is not the controller's phase.
inline bool cpuCalculationActive(const QJsonObject& status,bool connected){
    return connected&&status.value("enabled").toBool()&&status.value("compute_started").toBool()&&
        status.value("state")=="RUNNING"&&(status.contains("resident_workers")?status.value("resident_workers").toInt():status.value("active_workers").toInt())>0&&
        !status.value("result_ready").toBool();
}
}

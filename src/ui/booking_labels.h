#pragma once
#include <QString>

namespace pgn::ui {
// Storage/protocol states remain unchanged. Only this presentation adapter
// names what the user can expect; it never implies a public reservation.
inline QString localWorkLabel(const QString& state){
    if(state=="ACTIVE")return "MINING NOW";
    if(state=="ARMED")return "STARTS AFTER CURRENT RANGE";
    if(state=="QUEUED")return "SAVED · NOT SCHEDULED";
    if(state=="DISPATCHING")return "STARTING";
    if(state=="DISPATCHED")return "START REQUESTED";
    if(state=="PAUSED")return "PAUSED";
    if(state=="SOURCE_FINISHED")return "PROCESSED · PROOF PENDING";
    if(state=="RELEASED")return "CANCELLED";
    return "STATE UNAVAILABLE";
}
}

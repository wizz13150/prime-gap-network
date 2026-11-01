#pragma once
#include "mining/source_age.h"
#include "telemetry.h"

namespace pgn::mining {
// Receipt age is a LOWER BOUND: transit and remote collection latency are
// unmeasured. Never replace the original source UTC with local wall time.
// A typed local witness is supplied only by the owned in-process runtime;
// legacy packets keep their conservative wall-clock fallback.
inline std::optional<qint64> resourceReceiptAgeMs(const FrontierResourceTelemetrySnapshot& sample,
    qint64 nowNs=receiptMonotonicNs(),const QDateTime& nowUtc=QDateTime::currentDateTimeUtc()) {
    if(!sample.observedUtc.isValid())return {};
    if(sample.receivedMonotonicNs){
        const auto received=*sample.receivedMonotonicNs;
        if(received<0||nowNs<received)return {};
        return (nowNs-received)/1000000;
    }
    const auto age=sample.observedUtc.msecsTo(nowUtc);
    return age<0?std::nullopt:std::optional{age};
}
}

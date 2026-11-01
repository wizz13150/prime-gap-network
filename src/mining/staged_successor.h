#pragma once
#include "mining/source_decimal.h"
#include <QJsonObject>
#include <QUuid>

namespace pgn::mining {
// Observation only: configured future work is NOT the completed process.
// SQLite, conserved RAW, STOP, generation and remote launch locks still decide
// whether this exact staged dispatch can be reconciled and started.
inline bool stagedSuccessor(const QJsonObject& packet) {
    const auto m=packet.value("manifest").toObject(),b=packet.value("binding").toObject();
    const auto p=b.value("predecessor").toObject();
    const auto id=b.value("queued_intent").toString();const auto u=QUuid::fromString(id);
    const auto a=b.value("start"),z=b.value("end");
    return packet.value("owned")==false&&packet.value("state")=="OFF"&&
        m.value("uuid")==b.value("uuid")&&p.value("run")==m.value("run")&&
        p.value("start")==m.value("start")&&p.value("end")==m.value("end")&&
        a.isString()&&z.isString()&&a.toString().size()<=160&&z.toString().size()<=160&&
        source_decimal::canonical(a.toString())&&source_decimal::canonical(z.toString())&&
        source_decimal::less(a.toString(),z.toString())&&
        !u.isNull()&&u.toString(QUuid::WithoutBraces)==id&&b.value("queued_intent")!=m.value("queued_intent");
}
}

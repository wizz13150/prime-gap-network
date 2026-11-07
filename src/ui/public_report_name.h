#pragma once
#include "ui/public_profile_store.h"
#include <QJsonArray>
#include <QJsonObject>

namespace pgn::ui {
// The caller's snapshot must already have passed the Core branch/anchor checks.
// Require the same request and immutable report root, then enforce the complete
// display policy. A chosen pseudonym never replaces the signing identity.
inline std::optional<QString> publicReportName(const QJsonObject& snapshot) {
    const auto leases=snapshot.value("leases").toArray();
    if(leases.size()!=1)return {};
    const auto lease=leases.first().toObject();
    const auto id=lease.value("request").toString(),root=lease.value("report_root").toString();
    static const QRegularExpression hash("^[0-9a-f]{64}$");
    if(!hash.match(id).hasMatch()||!hash.match(root).hasMatch()||root==QString(64,'0'))return {};
    int reports=0,audits=0;QJsonObject attribution;
    for(const auto& value:snapshot.value("reports").toArray()){
        const auto report=value.toObject();
        if(report.value("request_id")!=id)continue;
        if(report.value("report_root")!=root)return {};
        ++reports;
    }
    for(const auto& value:snapshot.value("audits").toArray()){
        const auto audit=value.toObject();
        if(audit.value("request_id")!=id)continue;
        ++audits;attribution=audit.value("public_attribution").toObject();
    }
    if(reports!=1||audits!=1||attribution.size()!=2||attribution.value("schema")!="PGN_PUBLIC_ATTRIBUTION_V1"
            ||!attribution.value("name").isString())return {};
    const auto name=attribution.value("name").toString();
    const auto valid=canonicalPublicName(name);
    return valid&&*valid==name?valid:std::nullopt;
}
}

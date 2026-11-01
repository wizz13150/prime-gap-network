#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
namespace pgn::mining {
// A closed local computation is still subject to Core commitment, auditing
// and proof rules. This validator never acknowledges or grants acceptance.
inline QString cpuAuditResultError(const QJsonObject& result,const QString& expectedDigest){
    static const QRegularExpression hash("^[0-9a-f]{64}$"),number("^(0|[1-9][0-9]{0,12})$");
    if(!hash.match(expectedDigest).hasMatch()||result.value("request_sha256")!=expectedDigest||
       result.value("schema")!="PGN_CPU_SHARED68_AUDIT_COMPUTATION_V1"||result.value("state")!="AUDIT_COMPUTED_SOURCE_PROOF_PENDING"||
       result.value("full_audit")!=true||result.value("completed_positions")!="10000000000000"||
       result.value("coverage_contract")!="SOURCE_PRP_INDEPENDENT_PROOF_PENDING"||
       result.value("production_admitted")!=false||result.value("network_accepted")!=false)
        return "CPU result does not close this assigned audit; retained result was not acknowledged";
    const auto counts=result.value("counts").toArray();
    if(!result.value("counts").isArray()||counts.size()!=5)return "Five complete assigned partition counts required";
    for(const auto& count:counts){
        bool okay=false;const auto n=count.toString().toULongLong(&okay);
        if(!count.isString()||!number.match(count.toString()).hasMatch()||!okay||n>1000000000000ULL)return "CPU partition count is outside its exact domain";
    }
    return {};
}
}

#pragma once
#include <QJsonObject>
#include <QSet>
#include <QRegularExpression>
namespace pgn::ui {
struct LocalSourceProofPresentation {bool present{},active{};QString caption;};
inline QJsonObject projectLocalSourceProofStatus(const QString& currentRun,QJsonObject incoming,const QJsonObject& previous){
    if(incoming.value("run").toString()==currentRun)return incoming;
    if(!incoming.value("error").toString().isEmpty()){
        incoming["error"]="Background source "+incoming.value("run").toString()+" · "+incoming.value("error").toString();
        incoming["run"]="";return incoming; // Global worker Held, never a failure of the current mathematics.
    }
    return previous; // Deferred historic progress cannot erase the current rail.
}
inline LocalSourceProofPresentation localSourceProofPresentation(const QJsonObject& data,const QSet<QString>& activeRuns){
    if(data.value("scope").toString()!="LOCAL_CERTIFIED_PAIRS_ONLY"||!data.value("network_credit").isBool()||data.value("network_credit").toBool()||
        !data.value("range_coverage").isBool()||data.value("range_coverage").toBool())return {};
    const auto run=data.value("run").toString();const auto error=data.value("error").toString();
    if(!error.isEmpty()&&(run.isEmpty()||activeRuns.contains(run)))return {true,false,"LOCAL PROOF WORKER HELD · "+error.left(160)};
    if(!activeRuns.contains(run))return {}; // Never relabel a prior run's archive as current.
    static const QRegularExpression hash("^[0-9a-f]{64}$"),number("^(0|[1-9][0-9]*)$");
    if(!hash.match(data.value("declared_identity_sha256").toString()).hasMatch()||!hash.match(data.value("policy_sha256").toString()).hasMatch())return {};
    const auto read=[&](const char* key,qint64& out){const auto v=data.value(key);if(!v.isString()||!number.match(v.toString()).hasMatch())return false;bool okay=false;out=v.toString().toLongLong(&okay);return okay;};
    qint64 total{},verified{},rejected{},after{},through{};
    if(!read("total",total)||!read("verified",verified)||!read("rejected",rejected)||!read("after_gap_id",after)||!read("source_watermark",through)||
        verified>total||rejected!=total-verified||after>through||!data.value("more").isBool())return {};
    const bool more=data.value("more").toBool();
    return {true,more,QString("CPU AUDIT · %1 CHECKED · %2 REJECTED · %3").arg(verified).arg(rejected).arg(more?"BACKGROUND QUEUE":"CAUGHT UP")};
}
} // namespace pgn::ui

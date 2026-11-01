#pragma once
#include "wallet/chain_observation.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <limits>
#include <boost/multiprecision/cpp_int.hpp>

namespace pgn::mining {
struct CpuAuditWork {
    pgn::wallet::CoreEpoch core;
    QString genesis,tip,requestId,reportRoot,actor,requestSha256;
    int slot{};quint32 attempt{};
    QJsonObject serviceRequest;
};
struct CpuAuditWorkResult {std::optional<CpuAuditWork> work;QString error;};
inline std::optional<boost::multiprecision::cpp_int> cpuAuditUnsigned(const QJsonValue& value){
    static const QRegularExpression decimal("^(0|[1-9][0-9]{0,28})$");
    const auto text=value.toString();if(!value.isString()||!decimal.match(text).hasMatch())return {};
    boost::multiprecision::cpp_int out=0;for(auto ch:text){out*=10;out+=ch.unicode()-'0';}return out;
}
// Capability preflight only. It must run before signing an assignment; it is
// never evidence that a proposed assignment has been confirmed by the Core.
inline bool cpuAuditProposalSupported(const QJsonObject& proposal){
    using boost::multiprecision::cpp_int;const auto ranges=proposal.value("ranges").toArray();
    if(!proposal.value("ranges").isArray()||ranges.size()!=5)return false;
    int previous=-1;
    for(const auto& item:ranges){
        const auto part=item.toObject();const auto index=part.value("index").toInteger(-1);
        const auto start=cpuAuditUnsigned(part.value("start")),end=cpuAuditUnsigned(part.value("end"));
        if(!item.isObject()||part.size()!=3||!part.value("index").isDouble()||double(index)!=part.value("index").toDouble()||index<=previous||index>=100||
           !start||!end||*end-*start!=cpp_int("2000000000000")||*start<(cpp_int(1)<<67)||*end>(cpp_int(1)<<68))return false;
        previous=int(index);
    }
    return true;
}
// This checks the transport boundary, not signatures or consensus. Callers
// must obtain all three rows from one authenticated Core epoch and tip.
// Only the five assigned intervals cross into the computation service.
inline CpuAuditWorkResult cpuAuditWork(const pgn::wallet::ChainObservation& anchor,const QString& pinnedGenesis,
    const QJsonObject& report,const QJsonObject& lease,const QJsonObject& job,const QString& expectedActor,const QDateTime& now){
    using boost::multiprecision::cpp_int;
    const auto fail=[](const QString& error){return CpuAuditWorkResult{{},error};};
    static const QRegularExpression hash("^[0-9a-f]{64}$"),script("^[0-9a-f]+$");
    if(!anchor.freshAt(now)||!hash.match(pinnedGenesis).hasMatch()||anchor.genesis!=pinnedGenesis||!hash.match(anchor.tipHash).hasMatch())return fail("Fresh verified Core identity required");
    const auto request=report.value("request_id").toString(),root=report.value("report_root").toString(),ticket=report.value("ticket").toString();
    if(!hash.match(request).hasMatch()||!hash.match(root).hasMatch()||!hash.match(ticket).hasMatch()||
       lease.value("request")!=request||lease.value("report_root")!=root||lease.value("state")!="submitted_pending_audits")return fail("Confirmed report and owned interval do not match");
    if(expectedActor.isEmpty()||expectedActor.size()>8192||expectedActor.size()%2||!script.match(expectedActor).hasMatch()||job.value("actor")!=expectedActor||job.value("state")!="assigned"||job.value("expired")!=false)return fail("Current unexpired assignment for this auditor required");
    const QSet<QString> allowed{"slot","attempt","actor","issued","deadline_clock","issued_time","deadline_time","expired","state","ranges"};
    for(auto i=job.begin();i!=job.end();++i)if(!allowed.contains(i.key()))return fail("Unexpected audit assignment field; answers must not enter the worker");
    if(job.value("deadline_clock")!="median_chain_time"||!job.value("issued_time").isDouble()||!job.value("deadline_time").isDouble()||
       job.value("issued_time").toInteger(-1)<0||job.value("deadline_time").toInteger(-1)<=job.value("issued_time").toInteger(-1))return fail("Qualified time-based CPU assignment lease required");
    const auto slot=job.value("slot").toInteger(-1),attempt=job.value("attempt").toInteger(-1);
    if(!job.value("slot").isDouble()||double(slot)!=job.value("slot").toDouble()||slot<0||slot>1||
       !job.value("attempt").isDouble()||double(attempt)!=job.value("attempt").toDouble()||attempt<0||attempt>std::numeric_limits<quint32>::max())return fail("Invalid audit slot or attempt");
    const auto integer=cpuAuditUnsigned;
    const auto start=integer(lease.value("start")),end=integer(lease.value("end"));
    if(!start||!end||*end-*start!=cpp_int("200000000000000")||*start<(cpp_int(1)<<66)||*end>(cpp_int(1)<<95))return fail("A complete owned network ticket is required");
    if(!cpuAuditProposalSupported(job))return fail("Assigned partitions exceed the qualified CPU engine's domain");
    const auto partitions=job.value("ranges").toArray();if(!job.value("ranges").isArray()||partitions.size()!=5)return fail("Exactly five complete audit partitions required");
    QJsonArray assigned;int previous=-1;
    for(const auto& value:partitions){
        const auto part=value.toObject();const auto index=part.value("index").toInteger(-1);
        const auto first=integer(part.value("start")),last=integer(part.value("end"));
        if(!value.isObject()||part.size()!=3||!part.value("index").isDouble()||double(index)!=part.value("index").toDouble()||index<=previous||index>=100||
           !first||!last||*first!=*start+cpp_int("2000000000000")*index||*last!=*first+cpp_int("2000000000000"))return fail("Audit partition differs from its full assigned interval");
        previous=int(index);assigned.append(part);
    }
    QJsonObject service{{"schema","PGN_EXPLICIT_CPU_AUDIT_REQUEST_V1"},{"ticket_id",ticket},{"ticket_start",lease.value("start")},{"ticket_end",lease.value("end")},{"partitions",assigned}};
    const auto bytes=QJsonDocument(service).toJson(QJsonDocument::Compact);
    CpuAuditWork work{anchor.owner,anchor.genesis,anchor.tipHash,request,root,expectedActor,
        QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()),int(slot),quint32(attempt),service};
    return {std::move(work),{}};
}
}

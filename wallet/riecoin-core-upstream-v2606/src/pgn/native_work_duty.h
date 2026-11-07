// Copyright (c) 2026 The PGN developers. MIT.
// Parent-snapshot service credits. No wire version or network activation here.
#pragma once
#include <pgn/native_report_audit.h>
#include <tuple>
namespace pgn::core {
struct NativeDutyReference {
    uint256 report;
    uint8_t slot{};
    uint32_t attempt{};
    std::string commitment;
    bool operator==(const NativeDutyReference&)const=default;
    bool operator<(const NativeDutyReference& b)const {
        return std::tie(report,slot,attempt,commitment)<std::tie(b.report,b.slot,b.attempt,b.commitment);
    }
};
// Reconstructed from authenticated canonical request history, never an RPC cache.
using NativeDutySpent=std::map<NativeDutyReference,uint256>;
enum class NativeDutyVerdict {Ready,InvalidContext,NotEligible,InvalidClaim,AlreadySpent,MissingClaim,AuditRequired};
struct NativeDutyCheck;
class VerifiedNativeWorkDuty {
    uint256 parent;
    CScript actor;
    std::vector<NativeDutyReference> references;
    VerifiedNativeWorkDuty(uint256 p,CScript a,std::vector<NativeDutyReference> r)
        :parent(p),actor(std::move(a)),references(std::move(r)){}
    friend NativeDutyCheck InspectNativeWorkDuty(const NativeAuditLedger&,const CBlockIndex&,
        const CScript&,std::span<const NativeDutyReference>,const NativeDutySpent&,int32_t);
    friend bool ConsumeNativeWorkDuty(const VerifiedNativeWorkDuty&,const uint256&,const CScript&,
        const uint256&,NativeDutySpent&);
};
struct NativeDutyCheck {NativeDutyVerdict verdict;std::optional<VerifiedNativeWorkDuty> proof;};
inline bool NativeDutyDigest(const std::string& text){
    return text.size()==64&&text.find_first_not_of("0123456789abcdef")==std::string::npos;
}
inline NativeDutyReference NativeDutyRef(const uint256& report,uint8_t slot,const NativeAuditJob& job){
    return {report,slot,job.attempt,job.commitment};
}
inline bool NativeDutyDelivered(const NativeAuditJob& job,const CScript& actor,int32_t first_height){
    // A fixed commitment proves delivery of an answer, not its correctness or
    // human independence. Payment still requires native PASSED adjudication.
    return job.actor==actor&&job.issued>=first_height&&NativeDutyDigest(job.commitment)&&
        (job.stage==NativeAuditJob::Committed||job.stage==NativeAuditJob::Passed);
}
inline NativeDutyCheck InspectNativeWorkDuty(const NativeAuditLedger& ledger,const CBlockIndex& parent,
    const CScript& actor,std::span<const NativeDutyReference> claims,const NativeDutySpent& spent,
    int32_t first_assignment_height){
    // Caller must have obtained the complete ledger from ReadNativeAuditLedger
    // at this native parent. This helper does not authenticate an arbitrary
    // caller-created ledger, authorize a signature or activate a wire policy.
    if(!parent.phashBlock||parent.GetBlockHash().IsNull()||parent.nHeight<0||!parent.IsValid(BLOCK_VALID_SCRIPTS)||
       first_assignment_height<0||first_assignment_height>parent.nHeight||claims.size()>2)
        return {NativeDutyVerdict::InvalidContext,{}};
    if(!NativeAuditEligible(ledger,actor))return {NativeDutyVerdict::NotEligible,{}};
    std::set<NativeDutyReference> unique;
    for(const auto& claim:claims){
        if(claim.report.IsNull()||claim.slot>1||!NativeDutyDigest(claim.commitment)||!unique.insert(claim).second)
            return {NativeDutyVerdict::InvalidClaim,{}};
        const auto found=ledger.find(claim.report);
        if(found==ledger.end()||found->second.report.height>=parent.nHeight)
            return {NativeDutyVerdict::InvalidClaim,{}};
        const auto& report=found->second;const auto& job=report.jobs[claim.slot];
        if(!job||!NativeDutyDelivered(*job,actor,first_assignment_height)||job->attempt!=claim.attempt||
           job->commitment!=claim.commitment||NativeAuditOperator(ledger,report.owner)==NativeAuditOperator(ledger,actor))
            return {NativeDutyVerdict::InvalidClaim,{}};
        const auto& other=report.jobs[1-claim.slot];
        if(other&&NativeAuditOperator(ledger,other->actor)==NativeAuditOperator(ledger,actor))
            return {NativeDutyVerdict::InvalidClaim,{}};
        if(spent.contains(claim))return {NativeDutyVerdict::AlreadySpent,{}};
    }
    if(claims.size()<2){
        // No caller-supplied "pool empty" flag. A missing claim cannot conceal
        // another completed answer, an outstanding own assignment, or an
        // automatically selectable independent task at this exact parent.
        for(const auto& [id,report]:ledger){
            if(report.report.height>=parent.nHeight||NativeAuditOperator(ledger,report.owner)==NativeAuditOperator(ledger,actor))continue;
            for(uint8_t slot=0;slot<2;++slot){
                const auto& job=report.jobs[slot];if(!job||job->actor!=actor)continue;
                const auto ref=NativeDutyRef(id,slot,*job);
                if(NativeDutyDelivered(*job,actor,first_assignment_height)&&!spent.contains(ref)&&!unique.contains(ref))
                    return {NativeDutyVerdict::MissingClaim,{}};
                if(job->stage==NativeAuditJob::Assigned)
                    return {NativeDutyVerdict::AuditRequired,{}};
            }
        }
        if(SelectNativeAudit(ledger,actor,parent.nHeight,parent.GetMedianTimePast()))
            return {NativeDutyVerdict::AuditRequired,{}};
    }
    return {NativeDutyVerdict::Ready,VerifiedNativeWorkDuty{parent.GetBlockHash(),actor,
        std::vector<NativeDutyReference>{claims.begin(),claims.end()}}};
}
inline bool ConsumeNativeWorkDuty(const VerifiedNativeWorkDuty& proof,const uint256& parent,
    const CScript& actor,const uint256& issued_request,NativeDutySpent& spent){
    if(parent!=proof.parent||actor!=proof.actor||issued_request.IsNull())return false;
    // Check the entire debit before changing a single credit. Exact duplicate
    // application is idempotent; a different request cannot reuse the answers.
    for(const auto& ref:proof.references){
        const auto old=spent.find(ref);if(old!=spent.end()&&old->second!=issued_request)return false;
    }
    for(const auto& ref:proof.references)spent.emplace(ref,issued_request);
    return true;
}
struct NativeAutomaticWorkDuty {
    NativeDutyCheck checked;
    std::vector<NativeDutyReference> references;
};
inline NativeAutomaticWorkDuty InspectAutomaticNativeWorkDuty(const NativeAuditLedger& ledger,
    const CBlockIndex& parent,const CScript& actor,const NativeDutySpent& spent,int32_t first_assignment_height){
    using Candidate=std::pair<int32_t,NativeDutyReference>;
    std::vector<Candidate> oldest;
    for(const auto& [id,report]:ledger){
        if(report.report.height>=parent.nHeight||NativeAuditOperator(ledger,report.owner)==NativeAuditOperator(ledger,actor))continue;
        for(uint8_t slot=0;slot<2;++slot){
            const auto& job=report.jobs[slot];if(!job||!NativeDutyDelivered(*job,actor,first_assignment_height))continue;
            const auto& other=report.jobs[1-slot];
            if(other&&NativeAuditOperator(ledger,other->actor)==NativeAuditOperator(ledger,actor))continue;
            auto ref=NativeDutyRef(id,slot,*job);if(spent.contains(ref))continue;
            oldest.emplace_back(report.report.height,std::move(ref));
            std::sort(oldest.begin(),oldest.end());if(oldest.size()>2)oldest.pop_back();
        }
    }
    std::vector<NativeDutyReference> references;
    for(auto& item:oldest)references.push_back(std::move(item.second));
    return {InspectNativeWorkDuty(ledger,parent,actor,references,spent,first_assignment_height),std::move(references)};
}
} // namespace pgn::core

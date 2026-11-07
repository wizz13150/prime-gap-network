// RPC-only direct-child projection. Consensus uses ReadNativeAuditLedger.
#pragma once
#include <pgn/native_report_audit.h>

namespace pgn::core {
inline std::optional<NativeAuditLedger> AdvanceRpcAuditLedger(
    const NativeAuditLedger& previous,const CCoinsView& view,const CBlockIndex& tip,
    const NativeRequestHistoryArchive& history,const RangePolicyReadBudget& policy_budget,
    const RangeScheduleBudget& schedule_budget,ScriptWorkBudget signature_budget){
    AssertLockHeld(cs_main);
    try{
        const auto head=view.GetPgnFactHead();
        if(previous.authority.empty()||!tip.pprev||!tip.phashBlock||!tip.IsValid(BLOCK_VALID_SCRIPTS)||
           !head||!head->Valid()||head->tip!=tip.GetBlockHash()||view.GetBestBlock()!=tip.GetBlockHash())return {};
        auto reports=ReadConfirmedNativeReports(view,tip,history,schedule_budget);
        if(!reports||reports->size()<previous.size())return {};
        const auto policy=ReadChainRangePolicy(view,tip,history,schedule_budget.archive,policy_budget);
        if(!policy.policy||!ChainRangePolicyOnBranch(view,tip,*policy.policy)||policy.policy->Chain()!=head->chain)return {};
        // Re-derive the authenticated current schedule rather than assuming
        // that a cached assignment survives a policy or retention transition.
        const auto schedule=ReconstructCandidateRangeSchedule(view,tip,*policy.policy,history,schedule_budget);
        if(!schedule.schedule)return {};
        std::map<uint256,const CandidateRangeLease*> leases;
        for(const auto& lease:schedule.schedule->active)leases.emplace(lease.owner.Id(),&lease);
        NativeAuditLedger next=previous;
        for(const auto& [id,report]:*reports){
            const auto lease=leases.find(id);if(lease==leases.end())return {};
            const auto& owned=*lease->second;
            const auto old=next.find(id);
            if(old!=next.end()){
                const auto& retained=old->second;const auto& a=retained.report.commitment;const auto& b=report.commitment;
                if(a.request!=b.request||a.ticket!=b.ticket||a.root!=b.root||a.parent!=b.parent||a.signature!=b.signature||
                   retained.report.block!=report.block||retained.report.height!=report.height||
                   retained.owner!=owned.owner.Request().miner||retained.range.start!=owned.assignment.start||
                   retained.range.length!=owned.assignment.end-owned.assignment.start||retained.range.segments!=100||
                   !std::equal(retained.range.ticket_id.begin(),retained.range.ticket_id.end(),report.commitment.ticket.begin()))return {};
            }else{
                // Only the direct child may add a previously unseen report.
                // A missing earlier report requires full reconstruction.
                if(report.height!=tip.nHeight||report.block!=tip.GetBlockHash())return {};
                NativeReportAudit entry;entry.report=report;entry.owner=owned.owner.Request().miner;
                std::copy(report.commitment.ticket.begin(),report.commitment.ticket.end(),entry.range.ticket_id.begin());
                entry.range.start=owned.assignment.start;entry.range.length=owned.assignment.end-owned.assignment.start;entry.range.segments=100;
                next.emplace(id,std::move(entry));
            }
        }
        // No old entry may silently disappear from the confirmed registry.
        if(next.size()!=reports->size())return {};
        const auto block=history.Load(view,tip,tip.nHeight,schedule_budget.archive);
        if(!block.block||!block.block->Coinbase())return {};
        for(const auto& output:block.block->Coinbase()->vout){
            if(!IsNativeAuditScript(output.scriptPubKey))continue;
            const auto event=DecodeNativeAuditEnvelope(output.scriptPubKey);if(!event||output.nValue!=0)return {};
            ApplyNativeAuditEvent(next,*event,*tip.pprev,policy.policy->Chain(),signature_budget);
        }
        return next;
    }catch(const std::exception&){return {};}
}
}

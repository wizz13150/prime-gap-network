// Private task13 read-only consumer. No audit rules, wire or activation changes.
#pragma once
#include <pgn/native_report_audit.h>
#include <pgn/hourly_return_snapshot.h>
#include <algorithm>
#include <map>
#include <vector>

namespace pgn::discussion {
namespace complete_return_detail {
using namespace pgn::core;
inline ReceiptId Id(const uint256& value){
    ReceiptId out;std::copy(value.begin(),value.end(),out.begin());return out;
}
struct Receipt {
    AdmittedReturn fact;
    uint256 block,root;
    int32_t height;
};
struct Pending {
    uint256 request,block,root;
    int32_t height;
    int64_t parent_mtp;
    bool non_expired;
};
struct State {
    std::map<ReceiptId,Receipt> recent;
    std::vector<Pending> pending;
};
struct Context {
    uint256 policy;
    bool operator==(const Context&)const=default;
};
using Cache=native_audit_replay_detail::Snapshot<State,Context>;
}
}

// This is the difficulty consumer's own optional checkpoint state. Its bytes
// never authorize a return: Native retains the validated digest in memory and
// independently replays after a process restart. Codec2 is not a wire version.
namespace pgn::core::native_audit_replay_detail {
template<>struct ExtraCodec<pgn::discussion::complete_return_detail::State>{
    using State=pgn::discussion::complete_return_detail::State;
    static constexpr uint8_t id=2;
    static void Write(DataStream& s,const State& state,std::vector<std::pair<uint256,size_t>>*){
        s<<uint8_t{1}<<uint64_t(state.recent.size());
        for(const auto& [id,row]:state.recent){
            s<<id<<row.fact.first_received_utc<<row.fact.non_expired_at_first_receipt;
            s<<bool(row.fact.failure_known_utc);if(row.fact.failure_known_utc)s<<*row.fact.failure_known_utc;
            s<<row.block<<row.root<<row.height;
        }
        s<<uint64_t(state.pending.size());
        for(const auto& row:state.pending)s<<row.request<<row.block<<row.root<<row.height<<row.parent_mtp<<row.non_expired;
    }
    static State Read(DataStream& s){
        State state;uint8_t version{};uint64_t count{};s>>version>>count;
        RequireAudit(version==1&&count<=s.size()/100,"Invalid hourly receipt checkpoint");
        while(count--){
            pgn::discussion::ReceiptId id;pgn::discussion::complete_return_detail::Receipt row;
            bool failed{};s>>id>>row.fact.first_received_utc>>row.fact.non_expired_at_first_receipt>>failed;
            if(failed){int64_t time;s>>time;row.fact.failure_known_utc=time;}
            s>>row.block>>row.root>>row.height;row.fact.ticket=id;
            RequireAudit(row.height>0&&row.fact.first_received_utc>=0&&state.recent.emplace(id,row).second,
                         "Invalid or duplicate complete receipt");
        }
        s>>count;RequireAudit(count<=s.size()/100,"Invalid pending receipt count");
        while(count--){
            pgn::discussion::complete_return_detail::Pending row;
            s>>row.request>>row.block>>row.root>>row.height>>row.parent_mtp>>row.non_expired;
            RequireAudit(row.height>0&&row.parent_mtp>=0,"Invalid pending receipt clock");state.pending.push_back(row);
        }
        return state;
    }
};
}

namespace pgn::discussion {
namespace complete_return_detail {

// Inspect the state AFTER the pending block. An envelope, signature or commit
// by itself never creates a receipt: Native must have accepted the entire body.
inline bool Flush(const NativeAuditLedger& ledger,const CBlockIndex& through,State& state){
    for(const auto& pending:state.pending){
        if(pending.height!=through.nHeight||pending.block!=through.GetBlockHash())return false;
        const auto found=ledger.find(pending.request);
        if(found==ledger.end())return false;
        const auto& report=found->second;
        if(report.owner_reveal.empty())continue; // Event was not admitted.
        const Bytes body{report.owner_reveal.begin(),report.owner_reveal.end()};
        if(HexStr(AuditSha(body))!=pending.root.GetHex())return false;
        const auto id=Id(pending.request);
        Receipt receipt{{id,pending.parent_mtp,pending.non_expired,{}},
                        pending.block,pending.root,pending.height};
        const auto [old,inserted]=state.recent.emplace(id,receipt);
        if(!inserted&&(old->second.block!=receipt.block||old->second.root!=receipt.root||
                       old->second.height!=receipt.height||old->second.fact!=receipt.fact))return false;
    }
    state.pending.clear();
    return true;
}
inline void Prune(State& state,int64_t parent_mtp){
    const auto hour=parent_mtp-parent_mtp%3600;
    const auto oldest=hour-3900;
    // Native's immutable owner_reveal prevents a later replay of an old body
    // from entering the window again. Reorgs rebuild via the native cache key.
    std::erase_if(state.recent,[&](const auto& entry){return entry.second.fact.first_received_utc<oldest;});
}
}

// All clocks come from the canonical validation parent. The original lease
// remains a separate input check: a report commitment retaining a schedule
// slot does NOT extend that lease. Missing history yields unavailable, not0.
inline std::optional<std::vector<AdmittedReturn>> ReadNativeCompleteReturns(
    const CCoinsView& view,const CBlockIndex& parent,
    const pgn::core::ChainRangePolicy& policy,
    const pgn::core::NativeRequestHistoryArchive& history,
    const pgn::core::RangePolicyReadBudget& policy_budget,
    const pgn::core::RangeScheduleBudget& schedule_budget,
    pgn::core::ScriptWorkBudget signature_budget,
    complete_return_detail::Cache& cached)
{
    using namespace pgn::core;
    using namespace complete_return_detail;
    AssertLockHeld(cs_main);
    if(!ChainRangePolicyOnBranch(view,parent,policy))return {};
    const auto before=[&](const NativeAuditLedger& ledger,const CBlockIndex& prior,
                          const VerifiedNativeRequestBlock& block,State& state){
        if(!Flush(ledger,prior,state)||block.Height()!=prior.nHeight+1)return false;
        Prune(state,prior.GetMedianTimePast());
        std::set<uint256> queued;
        for(const auto& output:block.Coinbase()->vout){
            if(!IsNativeAuditScript(output.scriptPubKey))continue;
            const auto event=DecodeNativeAuditEnvelope(output.scriptPubKey);
            if(!event||output.nValue!=0)return false;
            if(event->action!=NativeAuditAction::ReportReveal)continue;
            const auto found=ledger.find(event->request);
            if(found==ledger.end())continue; // Native decides admissibility.
            const auto& report=found->second;
            if(!report.owner_reveal.empty()||!queued.insert(event->request).second)continue;
            const auto lease=ReconstructCandidateRangeLease(view,parent,policy,history,schedule_budget,event->request);
            if(lease.verdict!=RangeScheduleVerdict::ReconstructedCandidate||!lease.lease)return false;
            const auto& original=*lease.lease;
            if(original.assignment.start!=report.range.start||
               original.assignment.end-original.assignment.start!=report.range.length)return false;
            const auto now=prior.GetMedianTimePast();
            const bool timely=original.issued<=prior.nHeight&&
                (original.time_lease?!original.time_lease->expired(prior.nHeight,now):original.deadline>prior.nHeight);
            state.pending.push_back({event->request,block.Block(),report.report.commitment.root,
                                     block.Height(),now,timely});
        }
        return true;
    };
    const auto charge=[](const State& state)->std::optional<size_t>{
        constexpr auto per=sizeof(ReceiptId)+sizeof(Receipt)+4*sizeof(void*);
        if(state.recent.size()>(std::numeric_limits<size_t>::max()-sizeof(State))/per)return {};
        const auto used=sizeof(State)+state.recent.size()*per;
        if(state.pending.capacity()>(std::numeric_limits<size_t>::max()-used)/sizeof(Pending))return {};
        return used+state.pending.capacity()*sizeof(Pending);
    };
    auto result=native_audit_replay_detail::ReadIncremental(view,parent,history,policy_budget,
        schedule_budget,signature_budget,cached,Context{policy.Id()},before,charge);
    if(!result||!Flush(result->audits,parent,result->extra))return {};
    Prune(result->extra,parent.GetMedianTimePast());
    // Cache pruning is a local optimization only. A failed cache admission
    // never truncates, drops or changes the returned consensus-derived facts.
    const auto bytes=charge(result->extra);
    if(cached.state){
        if(bytes&&native_audit_replay_detail::Fits(result->audits,*bytes))cached.state->extra=result->extra;
        else cached.state.reset(); // Preserve any authenticated on-disk fallback.
    }
    std::vector<AdmittedReturn> rows;rows.reserve(result->extra.recent.size());
    for(const auto& [id,receipt]:result->extra.recent)rows.push_back(receipt.fact);
    return rows;
}
}

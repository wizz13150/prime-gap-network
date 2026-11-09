// Copyright (c) 2026 The PGN developers. MIT.
// Deterministic service accounting over authenticated native history.
#pragma once
#include <pgn/native_work_duty.h>
#include <pgn/testnet_service_activation.h>
#include <pgn/proposed_ticket_requests.h>
namespace pgn::core {
struct NativeWorkDutyActivation {
    uint256 chain,genesis,anchor;
    int32_t anchor_height{},height{},first_assignment_height{};
    bool operator==(const NativeWorkDutyActivation&)const=default;
    bool Valid()const {
        return !chain.IsNull()&&!genesis.IsNull()&&!anchor.IsNull()&&anchor_height>=0&&
            height>anchor_height&&first_assignment_height>=0&&first_assignment_height<=height;
    }
};
inline std::optional<NativeWorkDutyActivation> ConfiguredNativeWorkDutyActivation(const uint256& chain){
    const auto pin=PgnTestnetServiceActivation(chain);
    if(!pin)return {};
    return NativeWorkDutyActivation{chain,pin->genesis,pin->genesis,0,pin->height,pin->first_assignment_height};
}
inline bool NativeWorkDutyActivationOnBranch(const CCoinsView& view,const CBlockIndex& parent,
    const NativeWorkDutyActivation& activation){
    AssertLockHeld(cs_main);
    if(!activation.Valid()||!parent.phashBlock||!parent.IsValid(BLOCK_VALID_SCRIPTS)||parent.nHeight<activation.anchor_height)return false;
    const auto head=view.GetPgnFactHead();
    const auto* genesis=parent.GetAncestor(0);const auto* anchor=parent.GetAncestor(activation.anchor_height);
    return head&&head->chain==activation.chain&&head->tip==parent.GetBlockHash()&&view.GetBestBlock()==head->tip&&
        genesis&&genesis->phashBlock&&genesis->GetBlockHash()==activation.genesis&&
        anchor&&anchor->phashBlock&&anchor->GetBlockHash()==activation.anchor;
}
struct NativeWorkDutyState {NativeAuditLedger audits;NativeDutySpent spent;};
inline bool ApplyNativeWorkDutyBlock(const CCoinsView& view,const CBlockIndex& parent,
    const RangeScheduleBudget& schedule_budget,const NativeWorkDutyActivation& activation,
    const NativeAuditLedger& ledger,const CBlockIndex& prior,const VerifiedNativeRequestBlock& block,NativeDutySpent& spent)
{
        if(prior.nHeight+1<activation.height)return true;
        const auto scan=ScanConfirmedTicketRequests(view,parent,block,schedule_budget.scan);
        if(!scan.batch)return false;
        for(const auto& confirmed:scan.batch->Requests()){
            const auto& request=confirmed.Request();
            auto selected=InspectAutomaticNativeWorkDuty(ledger,prior,request.Request().miner,spent,activation.first_assignment_height);
            if(!selected.checked.proof||!ConsumeNativeWorkDuty(*selected.checked.proof,prior.GetBlockHash(),
                request.Request().miner,request.Id(),spent))return false;
        }
        return true;
}
// Full history remains the independent replay oracle and cold fallback.
inline std::optional<NativeWorkDutyState> ReadNativeWorkDutyStateUncached(const CCoinsView& view,
    const CBlockIndex& parent,const NativeRequestHistoryArchive& history,const RangePolicyReadBudget& policy_budget,
    const RangeScheduleBudget& schedule_budget,ScriptWorkBudget signature_budget,const NativeWorkDutyActivation& activation)
{
    if(!NativeWorkDutyActivationOnBranch(view,parent,activation))return {};
    NativeDutySpent spent;
    auto ledger=ReadNativeAuditLedgerWithBlockHook(view,parent,history,policy_budget,schedule_budget,signature_budget,
        [&](const NativeAuditLedger& state,const CBlockIndex& prior,const VerifiedNativeRequestBlock& block){
            return ApplyNativeWorkDutyBlock(view,parent,schedule_budget,activation,state,prior,block,spent);
        });
    if(!ledger)return {};
    return NativeWorkDutyState{std::move(*ledger),std::move(spent)};
}
inline std::optional<size_t> NativeWorkDutyRetainedCharge(const NativeDutySpent& spent)
{
    constexpr size_t limit=16*1024*1024;
    size_t used=sizeof(spent)+256;
    for(const auto& [reference,request]:spent){
        const size_t fixed=sizeof(reference)+sizeof(request)+256;
        if(fixed>limit-used)return {};
        used+=fixed;
        if(reference.commitment.capacity()>limit-used)return {};
        used+=reference.commitment.capacity();
    }
    return used;
}
inline std::optional<NativeWorkDutyState> ReadNativeWorkDutyState(const CCoinsView& view,
    const CBlockIndex& parent,const NativeRequestHistoryArchive& history,const RangePolicyReadBudget& policy_budget,
    const RangeScheduleBudget& schedule_budget,ScriptWorkBudget signature_budget,const NativeWorkDutyActivation& activation)
{
    if(!NativeWorkDutyActivationOnBranch(view,parent,activation))return {};
    using namespace native_audit_replay_detail;
    static Snapshot<NativeDutySpent,NativeWorkDutyActivation> cached;
    auto result=ReadIncremental(view,parent,history,policy_budget,schedule_budget,signature_budget,cached,activation,
        [&](const NativeAuditLedger& state,const CBlockIndex& prior,const VerifiedNativeRequestBlock& block,NativeDutySpent& spent){
            return ApplyNativeWorkDutyBlock(view,parent,schedule_budget,activation,state,prior,block,spent);
        },NativeWorkDutyRetainedCharge);
    if(!result)return {};
    return NativeWorkDutyState{std::move(result->audits),std::move(result->extra)};
}
inline bool VerifyProposedNativeWorkDuties(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& history,const VerifiedProposedTicketRequests& proposed,
    const RangePolicyReadBudget& policy_budget,const RangeScheduleBudget& schedule_budget,ScriptWorkBudget signature_budget,
    const std::optional<NativeWorkDutyActivation>& activation){
    if(!activation||int64_t(parent.nHeight)+1<activation->height)return true;
    if(!NativeWorkDutyActivationOnBranch(view,parent,*activation)||proposed.Parent()!=parent.GetBlockHash()||
       proposed.Height()!=int64_t(parent.nHeight)+1||proposed.Chain()!=activation->chain)return false;
    if(proposed.Requests().empty())return true;
    auto state=ReadNativeWorkDutyState(view,parent,history,policy_budget,schedule_budget,signature_budget,*activation);
    if(!state)return false;
    for(const auto& item:proposed.Requests()){
        const auto& request=item.request;
        auto selected=InspectAutomaticNativeWorkDuty(state->audits,parent,request.Request().miner,state->spent,activation->first_assignment_height);
        if(!selected.checked.proof||!ConsumeNativeWorkDuty(*selected.checked.proof,parent.GetBlockHash(),
            request.Request().miner,request.Id(),state->spent))return false;
    }
    return true;
}
inline bool VerifyProposedNativeWorkDuties(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& history,const VerifiedProposedTicketRequests& proposed,
    const RangePolicyReadBudget& policy_budget,const RangeScheduleBudget& schedule_budget,ScriptWorkBudget signature_budget){
    return VerifyProposedNativeWorkDuties(view,parent,history,proposed,policy_budget,schedule_budget,
        signature_budget,ConfiguredNativeWorkDutyActivation(proposed.Chain()));
}
} // namespace pgn::core

// Exact elapsed-time controller candidate. No network activation in this header.
#pragma once
#include <pgn/native_retarget_policy.h>
#include <pgn/testnet_service_activation.h>
#include <vector>
namespace pgn::core {
struct ElapsedMeritQuote {
    uint32_t target_q16,scientific_count,blocks_since_anchor;
    int64_t elapsed_seconds;
};
// Both indices must belong to verified ancestry. Retain the fractional Q16
// numerator between records, clamp its state at the limits (no integral windup),
// and add work credit ONLY for scientific headers. Coordination advances MTP.
inline std::optional<ElapsedMeritQuote> DeriveElapsedMeritTarget(
    const CBlockIndex& parent,const CBlockIndex& anchor,
    const MeritControllerDefinition& legacy,uint32_t half_life_seconds,uint32_t retained_floor_q16=0,
    int64_t higher_floor_activation_mtp=0,uint32_t higher_floor_q16=0){
    if(!MeritControllerValid(legacy)||half_life_seconds<legacy.spacing_seconds||
       higher_floor_activation_mtp<0||higher_floor_q16>legacy.maximum_q16||
       (higher_floor_q16&&higher_floor_activation_mtp==0)||
       !parent.phashBlock||!anchor.phashBlock||anchor.nHeight<0||parent.nHeight<anchor.nHeight)return {};
    const auto* ancestor=parent.GetAncestor(anchor.nHeight);
    if(!ancestor||!ancestor->phashBlock||ancestor->GetBlockHash()!=anchor.GetBlockHash())return {};
    const auto base=DeriveHistoricalMeritTarget(anchor,legacy);
    const auto minimum=std::max(legacy.minimum_q16,retained_floor_q16);
    if(!base||minimum>base->target_q16)return {};
    const auto elapsed=parent.GetMedianTimePast()-anchor.GetMedianTimePast();
    if(elapsed<0)return {};
    const std::array<uint64_t,9> policy{legacy.initial_q16,legacy.minimum_q16,legacy.maximum_q16,
        legacy.spacing_seconds,legacy.half_life_seconds,half_life_seconds,minimum,
        uint64_t(higher_floor_activation_mtp),higher_floor_q16};
    struct State {uint256 anchor;std::array<uint64_t,9> policy;int64_t numerator;uint32_t count;};
    // All intermediates fit signed 64 bits: Q16 <= 64*65536, H <= UINT32_MAX,
    // and each accepted header timestamp is uint32. No floating-point consensus.
    const int64_t low=int64_t(minimum)*half_life_seconds;
    const int64_t high=int64_t(legacy.maximum_q16)*half_life_seconds;
    static std::mutex mutex;static std::map<uint256,State> cache;static std::deque<uint256> order;
    const std::lock_guard lock(mutex);
    State state{anchor.GetBlockHash(),policy,int64_t(base->target_q16)*half_life_seconds,0};
    const auto floorAt=[&](const CBlockIndex& index){return higher_floor_q16&&index.GetMedianTimePast()>=higher_floor_activation_mtp?std::max(low,int64_t(higher_floor_q16)*half_life_seconds):low;};
    state.numerator=std::clamp(state.numerator,floorAt(anchor),high);
    std::vector<const CBlockIndex*> suffix;const auto* node=&parent;
    while(node->nHeight>anchor.nHeight){
        if(!node->phashBlock||!node->pprev||node->pprev->nHeight!=node->nHeight-1)return {};
        const auto known=cache.find(node->GetBlockHash());
        if(known!=cache.end()&&known->second.anchor==state.anchor&&known->second.policy==policy){state=known->second;break;}
        suffix.push_back(node);node=node->pprev;
    }
    for(auto it=suffix.rbegin();it!=suffix.rend();++it){
        const auto& current=**it;const auto kind=ClassifyAdaptiveHeader(current,legacy);
        const auto step=current.GetMedianTimePast()-current.pprev->GetMedianTimePast();
        if(kind==AdaptiveHeaderKind::Invalid||step<0)return {};
        const bool gold=kind==AdaptiveHeaderKind::Scientific;
        state.numerator=std::clamp(state.numerator+45426*(int64_t(gold?legacy.spacing_seconds:0)-step),floorAt(current),high);
        state.count+=gold;
        const auto id=current.GetBlockHash();
        if(cache.contains(id))cache[id]=state;else{cache.emplace(id,state);order.push_back(id);}
        while(cache.size()>4096){cache.erase(order.front());order.pop_front();}
    }
    return ElapsedMeritQuote{uint32_t(state.numerator/half_life_seconds),base->scientific_count+state.count,state.count,elapsed};
}

struct ElapsedRetargetSchedule {uint256 chain;int32_t height;uint32_t half_life_seconds,retained_floor_q16;int64_t higher_floor_activation_mtp{};uint32_t higher_floor_q16{};};
inline constexpr int32_t TestnetElapsedRetargetHeight=3456;
inline constexpr int64_t TestnetGoldFloorActivationMtp=1794607200; // 2026-11-13 22:00 UTC
inline constexpr uint32_t TestnetGoldFloorQ16=55U*32768U; // Exact27.5
inline std::optional<ElapsedRetargetSchedule> ConfiguredElapsedRetarget(const uint256& chain){
    if(!PgnTestnetServiceActivation(chain))return {};
    return ElapsedRetargetSchedule{chain,TestnetElapsedRetargetHeight,1800,21U*65536U,TestnetGoldFloorActivationMtp,TestnetGoldFloorQ16};
}
// Old history remains interpreted by the original rule. The first new-rule
// candidate retains exactly the old target at its parent; no genesis rewrite.
inline std::optional<HistoricalMeritQuote> DeriveScheduledMeritTarget(
    const CBlockIndex& parent,const MeritControllerDefinition& legacy,
    const std::optional<ElapsedRetargetSchedule>& schedule){
    auto quote=DeriveHistoricalMeritTarget(parent,legacy);if(!quote||!schedule)return quote;
    const auto* genesis=parent.GetAncestor(0);
    if(!genesis||!genesis->phashBlock)return {};
    if(genesis->GetBlockHash()!=schedule->chain)return quote;
    if(schedule->height<=0||schedule->half_life_seconds<legacy.spacing_seconds)return {};
    if(parent.nHeight<schedule->height-1)return quote;
    const auto* anchor=parent.GetAncestor(schedule->height-1);if(!anchor)return {};
    const auto elapsed=DeriveElapsedMeritTarget(parent,*anchor,legacy,schedule->half_life_seconds,schedule->retained_floor_q16,
        schedule->higher_floor_activation_mtp,schedule->higher_floor_q16);
    if(!elapsed)return {};
    quote->target_q16=elapsed->target_q16;quote->scientific_count=elapsed->scientific_count;
    return quote;
}
inline std::optional<HistoricalMeritQuote> DeriveNextMeritTarget(
    const CBlockIndex& parent,const MeritControllerDefinition& legacy){
    const auto* genesis=parent.GetAncestor(0);if(!genesis||!genesis->phashBlock)return {};
    // Existing-chain TESTNET upgrade, shared by validation, loading, RPC and
    // templates. 21 keeps every eligible Gold above retained/count output.
    return DeriveScheduledMeritTarget(parent,legacy,ConfiguredElapsedRetarget(genesis->GetBlockHash()));
}
}

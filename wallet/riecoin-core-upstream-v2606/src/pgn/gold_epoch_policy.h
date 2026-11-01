// Copyright (c) 2026 The PGN developers. MIT.
#pragma once
#include <pgn/issuance_budget.h>
#include <arith_uint256.h>
#include <algorithm>
#include <optional>
#include <span>
#include <vector>
#include <string_view>
#include <cstdint>
namespace pgn::core {
inline constexpr std::string_view GoldEpochPolicyTag{"PGNTESTNET-REWARDS-1"};
struct GoldEpochDefinition {
    uint32_t era_golds,audit_period;
    CAmount gold,silver,bronze,red,audit_period_amount,exceptional;
    uint32_t exceptional_target_q16;
};
inline bool GoldEpochDefinitionValid(const GoldEpochDefinition& d){
    if(!d.era_golds||!d.audit_period||d.era_golds%d.audit_period||!d.exceptional_target_q16||d.exceptional_target_q16>64U*65536U)return false;
    for(const auto amount:{d.gold,d.silver,d.bronze,d.red,d.audit_period_amount,d.exceptional})
        if(amount<0||amount>PGN_MAX_SUPPLY_ATOMIC)return false;
    return d.gold>0&&d.exceptional>0;
}
inline std::optional<GoldEpochDefinition> DecodeGoldEpochPolicy(std::span<const unsigned char> bytes){
    if(bytes.size()!=61||bytes.front()!=1)return {};
    size_t at=1;const auto get=[&](unsigned width){uint64_t result{};for(unsigned i=0;i<width;++i)result|=uint64_t(bytes[at++])<<(8*i);return result;};
    GoldEpochDefinition d;d.era_golds=get(4);d.audit_period=get(4);
    CAmount* amounts[]{&d.gold,&d.silver,&d.bronze,&d.red,&d.audit_period_amount,&d.exceptional};
    for(auto* amount:amounts){const auto value=get(8);if(value>uint64_t(PGN_MAX_SUPPLY_ATOMIC))return {};*amount=CAmount(value);}
    d.exceptional_target_q16=get(4);return GoldEpochDefinitionValid(d)?std::optional{d}:std::nullopt;
}
struct GoldEpochGrant {uint64_t era;CAmount gold,silver,bronze,red,audit;};
inline std::optional<GoldEpochGrant> QuoteGoldEpoch(const GoldEpochDefinition& d,uint64_t golds_before){
    if(!GoldEpochDefinitionValid(d))return {};
    const uint64_t era=golds_before/d.era_golds;
    const auto half=[era](CAmount value){return era>=63?CAmount{0}:CAmount(uint64_t(value)>>era);};
    const CAmount period=half(d.audit_period_amount);
    const uint64_t index=(golds_before%d.era_golds)%d.audit_period;
    const auto accumulated=[&](uint64_t count){return CAmount((arith_uint256{uint64_t(period)}*arith_uint256{count}/arith_uint256{d.audit_period}).GetLow64());};
    return GoldEpochGrant{era,half(d.gold),half(d.silver),half(d.bronze),half(d.red),accumulated(index+1)-accumulated(index)};
}
inline std::optional<CAmount> GoldEpochLifetimeOrdinaryBudget(const GoldEpochDefinition& d){
    if(!GoldEpochDefinitionValid(d))return {};
    arith_uint256 total;
    for(uint64_t era=0;era<63;++era){
        const auto q=QuoteGoldEpoch(d,era*d.era_golds);if(!q)return {};
        const auto near_gold=arith_uint256{uint64_t(q->gold)}+arith_uint256{uint64_t(q->silver)}+arith_uint256{uint64_t(q->bronze)}+arith_uint256{uint64_t(q->red)};
        total+=near_gold*arith_uint256{d.era_golds};
        total+=arith_uint256{uint64_t(d.audit_period_amount)>>era}*arith_uint256{d.era_golds/d.audit_period};
        if(total>arith_uint256{uint64_t(PGN_MAX_SUPPLY_ATOMIC)})return {};
    }return CAmount(total.GetLow64());
}
// Stable task ordering is supplied by authenticated (report, slot, attempt)
// identity. The remainder is atomic units; no coins disappear through rounding.
inline std::optional<std::vector<CAmount>> SplitAuditPot(CAmount pot,size_t tasks){
    if(pot<0||pot>PGN_MAX_SUPPLY_ATOMIC||tasks>262144)return {};
    if(!tasks)return std::vector<CAmount>{};
    std::vector<CAmount> payments(tasks,pot/tasks);
    for(size_t i=0;i<size_t(pot%tasks);++i)++payments[i];
    return payments;
}
}

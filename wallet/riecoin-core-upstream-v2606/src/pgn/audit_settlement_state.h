// Copyright (c) 2026 The PGN developers. MIT.
// Provisional V1 settlement metadata; no deployment or issuance authority.
#pragma once
#include <pgn/gold_epoch_policy.h>
#include <serialize.h>
#include <uint256.h>
namespace pgn::core {
struct AuditSettlementActive {
    uint64_t golds{},tasks{},next{};
    int32_t height{};
    uint256 cutoff,root;
    CAmount pot{};
    bool Valid() const {
        return golds&&tasks&&next<tasks&&height>0&&!cutoff.IsNull()&&!root.IsNull()&&
            pot>=0&&pot<=PGN_MAX_SUPPLY_ATOMIC;
    }
    bool operator==(const AuditSettlementActive&)const=default;
    SERIALIZE_METHODS(AuditSettlementActive,obj){READWRITE(obj.golds,obj.tasks,obj.next,obj.height,obj.cutoff,obj.root,obj.pot);}
};
struct AuditSettlementBalances {
    uint64_t completed_golds{};
    int32_t completed_height{};
    CAmount total_paid{};
    std::optional<AuditSettlementActive> active;
    bool Valid(CAmount pool) const {
        if(completed_height<0||total_paid<0||total_paid>PGN_MAX_SUPPLY_ATOMIC||pool<0)return false;
        if(!active)return true;
        if(!active->Valid())return false;
        const auto remaining=QuoteAuditPotRange(active->pot,active->tasks,active->next,active->tasks-active->next);
        return active->golds>completed_golds&&active->height>completed_height&&remaining&&*remaining<=pool;
    }
    bool operator==(const AuditSettlementBalances&)const=default;
    template<typename Stream> void Serialize(Stream& stream) const {
        stream<<completed_golds<<completed_height<<total_paid<<uint8_t(active.has_value());
        if(active)stream<<*active;
    }
    template<typename Stream> void Unserialize(Stream& stream) {
        AuditSettlementBalances next;uint8_t present{};
        stream>>next.completed_golds>>next.completed_height>>next.total_paid>>present;
        if(present>1)throw std::ios_base::failure("Invalid audit settlement active marker");
        if(present){AuditSettlementActive value;stream>>value;next.active=value;}
        *this=std::move(next);
    }
};
// Integral cumulative reservation, including partial periods and era halvings.
inline std::optional<CAmount> QuoteCumulativeAuditAccrual(const GoldEpochDefinition& d,uint64_t golds){
    if(!GoldEpochDefinitionValid(d))return {};
    arith_uint256 total;
    for(uint64_t era=0;era<63&&golds;++era){
        const auto count=std::min<uint64_t>(golds,d.era_golds);golds-=count;
        const auto amount=uint64_t(d.audit_period_amount)>>era;
        total+=arith_uint256{amount}*arith_uint256{count/d.audit_period};
        total+=arith_uint256{amount}*arith_uint256{count%d.audit_period}/arith_uint256{d.audit_period};
        if(total>arith_uint256{uint64_t(PGN_MAX_SUPPLY_ATOMIC)})return {};
    }
    return CAmount(total.GetLow64());
}
}

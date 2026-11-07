// Copyright (c) 2026 The PGN developers. MIT.
// Publishable consensus contract; constructing it never activates a network.
#pragma once
#include <pgn/issuance_chainstate.h>
#include <hash.h>
#include <script/script.h>
namespace pgn::core {
inline constexpr std::string_view AuditSettlementActivationTag{"PGN-AUDIT-ACTIVATION-1"};
struct AuditSettlementActivation {
    uint256 chain,genesis,monetary_policy,anchor;
    int32_t anchor_height{},height{};
    uint8_t version{1},receipt_mode{1},grandfather{1}; // 1 = all authentic unpaid historical PASSED
    uint16_t page_outputs{};
    bool coordination_after_gold{};
    bool Valid()const{
        return !chain.IsNull()&&!genesis.IsNull()&&!monetary_policy.IsNull()&&!anchor.IsNull()&&
            anchor_height>=0&&height>anchor_height&&version==1&&receipt_mode==1&&grandfather==1&&
            page_outputs&&page_outputs<=128&&coordination_after_gold;
    }
    uint256 Id()const{
        HashWriter out{TaggedHash("PGN/AUDIT-ACTIVATION/1")};
        out<<chain<<genesis<<monetary_policy<<anchor<<anchor_height<<height<<version<<receipt_mode<<grandfather<<page_outputs<<coordination_after_gold;
        return out.GetHash();
    }
    bool operator==(const AuditSettlementActivation&)const=default;
};
inline bool AuditSettlementActivationBinds(const AuditSettlementActivation& a,const uint256& chain,
    const uint256& genesis,const uint256& policy,const uint256& anchor,int32_t anchor_height,int64_t next_height){
    return a.Valid()&&a.chain==chain&&a.genesis==genesis&&a.monetary_policy==policy&&
        a.anchor==anchor&&a.anchor_height==anchor_height&&next_height>=a.height;
}
inline CScript AuditSettlementActivationReceipt(const AuditSettlementActivation& activation){
    const auto id=activation.Id();
    return CScript{}<<OP_RETURN<<std::vector<unsigned char>{AuditSettlementActivationTag.begin(),AuditSettlementActivationTag.end()}<<std::vector<unsigned char>{id.begin(),id.end()};
}
// Exact legacy financial-rule replay. Missing v4 is allowed only while the
// complete pre-epoch monetary state is empty; it is not an inferred paid zero.
inline bool VerifyLegacyAuditUnpaidStep(const GoldEpochDefinition& d,
    const IssuanceSnapshot& before,const IssuanceSnapshot& after,int32_t height){
    if(!GoldEpochDefinitionValid(d)||!before.Valid()||!after.Valid()||before.audit_settlement||after.audit_settlement||
       !before.partition||!after.partition||!before.pools||!after.pools||height<=0)return false;
    auto expected=before;expected.tip=after.tip;
    if(after.epochs!=before.epochs){
        if(!after.epochs)return false;
        auto epochs=before.epochs;
        if(!epochs){
            if(before.minted||before.promised||before.pools->audit||before.pools->near_miss)return false;
            epochs=GoldEpochBalances{};
        }
        if(epochs->audit_paid_height||epochs->golds==std::numeric_limits<uint64_t>::max())return false;
        const auto q=QuoteGoldEpoch(d,epochs->golds);if(!q)return false;
        ++epochs->golds;epochs->last_gold_height=height;
        epochs->silver+=q->silver;epochs->bronze+=q->bronze;epochs->red+=q->red;
        expected.epochs=epochs;
        const auto near_amount=q->silver+q->bronze+q->red;
        expected.minted+=q->gold;expected.promised+=near_amount+q->audit;
        expected.partition->ordinary_minted+=q->gold;expected.partition->ordinary_promised+=near_amount+q->audit;
        expected.pools->near_miss+=near_amount;expected.pools->audit+=q->audit;
    }
    if(expected!=after||!expected.Valid()||(after.epochs&&after.epochs->audit_paid_height))return false;
    const auto accrued=QuoteCumulativeAuditAccrual(d,after.epochs?after.epochs->golds:0);
    return accrued&&after.pools->audit==*accrued;
}
inline bool InitializeAuditSettlementActivation(const AuditSettlementActivation& activation,
    const IssuanceSnapshot& before,int32_t next_height,IssuanceSnapshot& after){
    if(!activation.Valid()||next_height!=activation.height||!before.Valid()||before.audit_settlement||
       !before.partition||before.partition->policy!=activation.monetary_policy||!before.pools||
       (before.epochs&&before.epochs->audit_paid_height)||after.audit_settlement)return false;
    // A v3 chain can activate before its first Gold. Absence is preserved in
    // the authenticated before snapshot; only the explicit migration adds the
    // empty v4 counters required by v5. Never infer an unpaid zero after value
    // has already been minted or reserved without those counters.
    if(!before.epochs&&(before.minted||before.promised||before.pools->audit||before.pools->near_miss))return false;
    auto unchanged=after;unchanged.tip=before.tip;
    if(unchanged!=before)return false;
    auto next=after;if(!next.epochs)next.epochs=GoldEpochBalances{};
    next.audit_settlement=AuditSettlementBalances{};
    if(!next.Valid())return false;
    after=std::move(next);return true;
}
}

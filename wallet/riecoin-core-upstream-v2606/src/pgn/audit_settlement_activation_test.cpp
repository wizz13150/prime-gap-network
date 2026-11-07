#undef NDEBUG
#include <pgn/audit_settlement_activation.h>
#include <streams.h>
#include <cassert>
#include <cstdio>
using namespace pgn::core;
uint256 Id(unsigned n){uint256 id;id.begin()[0]=n;return id;}
int main(){
    constexpr CAmount c=PGN_ATOMIC_UNITS_PER_COIN;
    const GoldEpochDefinition d{200000,10,171*c,32*c,24*c,8*c,125*c,1000*c,40*65536};
    IssuanceSnapshot before{Id(1),PGN_MAX_SUPPLY_ATOMIC,0,0,
        IssuanceBudgetPartition{Id(2),99000000*c,0,0,0,0},OrdinaryRewardPools{}};
    assert(before.Valid());
    const AuditSettlementActivation empty_activation{Id(9),Id(1),Id(2),Id(1),0,20,1,1,1,2,true};
    const auto empty_before=before;
    auto empty_after=before;empty_after.tip=Id(20);
    if(!InitializeAuditSettlementActivation(empty_activation,empty_before,20,empty_after)){
        std::puts("{\"PASS\":false,\"case\":\"zero-Gold-v3-activation\",\"before_golds\":0,\"before_epochs_absent\":true,\"minted_atomic\":0,\"promised_atomic\":0}");
        return 86;
    }
    assert(empty_after.Valid()&&empty_after.epochs&&empty_after.epochs->golds==0&&empty_after.audit_settlement);
    assert(empty_after.minted==0&&empty_after.promised==0&&empty_after.pools==empty_before.pools);
    assert(InvertIssuanceCommit({empty_before,empty_after},empty_after)->after==empty_before);
    assert(!empty_before.epochs&&!empty_before.audit_settlement);
    DataStream empty_stream;empty_stream<<empty_before<<empty_after;
    IssuanceSnapshot restored_empty_before,restored_empty_after;empty_stream>>restored_empty_before>>restored_empty_after;
    assert(empty_stream.empty()&&restored_empty_before==empty_before&&restored_empty_after==empty_after);
    auto hidden_value=empty_before;hidden_value.minted=1;hidden_value.partition->ordinary_minted=1;
    assert(hidden_value.Valid());auto hidden_after=hidden_value;hidden_after.tip=Id(20);
    assert(!InitializeAuditSettlementActivation(empty_activation,hidden_value,20,hidden_after));
    auto hidden_reserved=empty_before;hidden_reserved.promised=1;hidden_reserved.partition->ordinary_promised=1;
    hidden_reserved.pools->audit=1;assert(hidden_reserved.Valid());auto reserved_after=hidden_reserved;reserved_after.tip=Id(20);
    assert(!InitializeAuditSettlementActivation(empty_activation,hidden_reserved,20,reserved_after));
    auto empty_replay=empty_after;
    assert(!InitializeAuditSettlementActivation(empty_activation,empty_after,20,empty_replay));
    for(unsigned n=0;n<10;++n){
        auto next=before;next.tip=Id(3+n);const auto q=*QuoteGoldEpoch(d,n);
        next.epochs=before.epochs.value_or(GoldEpochBalances{});
        ++next.epochs->golds;next.epochs->last_gold_height=n+1;
        next.epochs->silver+=q.silver;next.epochs->bronze+=q.bronze;next.epochs->red+=q.red;
        next.minted+=q.gold;next.promised+=q.silver+q.bronze+q.red+q.audit;
        next.partition->ordinary_minted=next.minted;next.partition->ordinary_promised=next.promised;
        next.pools->near_miss+=q.silver+q.bronze+q.red;next.pools->audit+=q.audit;
        assert(VerifyLegacyAuditUnpaidStep(d,before,next,n+1));
        auto hidden=next;--hidden.pools->audit;--hidden.promised;--hidden.partition->ordinary_promised;
        ++hidden.minted;++hidden.partition->ordinary_minted;
        assert(hidden.Valid()&&!VerifyLegacyAuditUnpaidStep(d,before,hidden,n+1));
        before=next;
    }
    assert(before.pools->audit==125*c&&before.epochs->golds==10&&!before.audit_settlement);
    const AuditSettlementActivation a{Id(9),Id(1),Id(2),Id(1),0,20,1,1,1,2,true};assert(a.Valid());
    assert(AuditSettlementActivationBinds(a,Id(9),Id(1),Id(2),Id(1),0,20));
    assert(!AuditSettlementActivationBinds(a,Id(8),Id(1),Id(2),Id(1),0,20));
    assert(!AuditSettlementActivationBinds(a,Id(9),Id(8),Id(2),Id(1),0,20));
    assert(!AuditSettlementActivationBinds(a,Id(9),Id(1),Id(8),Id(1),0,20));
    assert(!AuditSettlementActivationBinds(a,Id(9),Id(1),Id(2),Id(8),0,20));
    assert(!AuditSettlementActivationBinds(a,Id(9),Id(1),Id(2),Id(1),1,20));
    assert(!AuditSettlementActivationBinds(a,Id(9),Id(1),Id(2),Id(1),0,19));
    auto after=before;after.tip=Id(20);assert(InitializeAuditSettlementActivation(a,before,20,after));
    auto restored=after;restored.audit_settlement.reset();restored.tip=before.tip;assert(restored==before);
    assert(after.audit_settlement->total_paid==0&&after.promised==before.promised&&after.minted==before.minted);
    auto replay=after;assert(!InitializeAuditSettlementActivation(a,after,20,replay));
    auto wrong=before;wrong.tip=Id(20);assert(!InitializeAuditSettlementActivation(a,before,19,wrong));
    auto bad=a;bad.monetary_policy=Id(7);assert(!InitializeAuditSettlementActivation(bad,before,20,wrong));
    bad=a;bad.grandfather=0;assert(!bad.Valid());bad=a;bad.height=0;assert(!bad.Valid());
    DataStream data;data<<before<<after;IssuanceSnapshot old,current;data>>old>>current;
    assert(old==before&&!old.audit_settlement&&current==after&&data.empty());
    assert(InvertIssuanceCommit({before,after},after)->after==before);
    std::puts("{\"PASS\":true,\"evidence_layer\":\"legacy-rule replay + exact snapshot migration kernel\",\"old_golds\":10,\"reserved_atomic\":\"1250000000000\",\"v4_absence_preserved\":true,\"v5_only_extension_added\":true,\"hidden_old_payment_rejected\":true,\"wrong_height_policy_rejected\":true,\"full_core_activation\":false}");
}

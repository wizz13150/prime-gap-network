#undef NDEBUG
#include <pgn/testnet_definition.h>
#include <pgn/testnet_service_activation.h>
#include <pgn/native_work_duty_admission.h>
#include <cassert>
#include <cstdio>
using namespace pgn::core;
int main(){
    const auto definition=BuildPgnTestnetDefinition();
    const auto chain=definition.genesis.GetHash();
    const auto pin=PgnTestnetServiceActivation(chain);
    assert(pin&&pin->genesis==chain&&chain==definition.pinned_genesis);
    Bytes money;
    for(const auto& output:definition.genesis.vtx.front()->vout){
        auto at=output.scriptPubKey.begin();opcodetype op;Bytes tag,payload;
        assert(output.scriptPubKey.GetOp(at,op)&&op==OP_RETURN);
        assert(output.scriptPubKey.GetOp(at,op,tag)&&output.scriptPubKey.GetOp(at,op,payload));
        if(std::string{tag.begin(),tag.end()}==MonetaryPolicyEnvelopeTag)money=payload;
    }
    assert(!money.empty());
    HashWriter writer{TaggedHash("PGN/CANDIDATE/NATIVE-MONETARY-POLICY/1")};
    writer<<chain<<chain<<money;
    assert(writer.GetHash()==pin->monetary_policy);
    const auto audit=ConfiguredAuditSettlementActivation(chain);
    const auto duty=ConfiguredNativeWorkDutyActivation(chain);
    assert(audit&&duty&&audit->Valid()&&duty->Valid());
    assert(audit->height==64&&audit->height==duty->height);
    assert(audit->anchor_height==0&&duty->anchor_height==0);
    assert(audit->anchor==chain&&duty->anchor==chain);
    assert(audit->page_outputs==128&&duty->first_assignment_height==0);
    assert(audit->grandfather==1&&audit->receipt_mode==1&&audit->coordination_after_gold);
    assert(!AuditSettlementActivationBinds(*audit,chain,chain,pin->monetary_policy,chain,0,63));
    assert(AuditSettlementActivationBinds(*audit,chain,chain,pin->monetary_policy,chain,0,64));
    assert(AuditSettlementActivationBinds(*audit,chain,chain,pin->monetary_policy,chain,0,65));
    for(unsigned i=0;i<4096;++i){
        HashWriter other_writer;other_writer<<i;
        const auto other=other_writer.GetHash();assert(other!=chain);
        assert(!PgnTestnetServiceActivation(other));
        assert(!ConfiguredAuditSettlementActivation(other));
        assert(!ConfiguredNativeWorkDutyActivation(other));
        assert(!AuditSettlementActivationBinds(*audit,other,chain,pin->monetary_policy,chain,0,64));
        assert(!AuditSettlementActivationBinds(*audit,chain,other,pin->monetary_policy,chain,0,64));
        assert(!AuditSettlementActivationBinds(*audit,chain,chain,other,chain,0,64));
        assert(!AuditSettlementActivationBinds(*audit,chain,chain,pin->monetary_policy,other,0,64));
    }
    assert(!PgnTestnetServiceActivation(uint256{}));
    std::puts("{\"PASS\":true,\"scope\":\"Native parameter binding, real unchanged epoch2 genesis and monetary identity, height boundary and chain isolation; no live activation or payout\",\"candidate_height\":64,\"foreign_chain_cases\":4096}");
}

// MIT. Native bootstrap body candidate; no selected network or startup authority.
#pragma once
#include <pgn/native_bootstrap_hash.h>
#include <pgn/native_bootstrap_phase.h>
#include <pgn/candidate_block_science.h>
#include <pgn/proposed_ticket_requests.h>
#include <pgn/native_report_commitment.h>
#include <pgn/native_report_audit.h>
#include <pgn/native_audit_settlement.h>
#include <pgn/native_work_duty_admission.h>
namespace pgn::core {
struct NativeBlockBootstrapBudget {
    RangePolicyReadBudget policy;RequestReplayBudget requests;
    uint32_t maximum_phase_blocks; // Bounded authenticated phase lookup.
    bool ledger_continuation{}; // Explicit testnet profile: Gold closes an epoch, not the ledger.
};
// Decode only an EXACT preselected native genesis object and its pinned ID.
// This is immutable profile metadata, never an online header-supplied target.
inline std::optional<uint32_t> ReadPinnedGenesisBootstrapBits(const CBlock& genesis,const uint256& expected,RangePolicyReadBudget budget){
    if(expected.IsNull()||genesis.GetHash()!=expected||genesis.vtx.size()!=1||!genesis.vtx.front()||!genesis.vtx.front()->IsCoinBase()||
       genesis.nNonce!=NativeBootstrapNonce(0)||!budget.maximum_outputs||!budget.maximum_script_bytes||budget.maximum_payload_bytes<5||
       genesis.vtx.front()->vout.size()>budget.maximum_outputs)return {};
    bool mutated{};if(BlockMerkleRoot(genesis,&mutated)!=genesis.hashMerkleRoot||mutated)return {};
    std::optional<uint32_t> found;uint64_t remaining=budget.maximum_script_bytes;
    const Bytes wanted{BootstrapHashPolicyEnvelopeTag.begin(),BootstrapHashPolicyEnvelopeTag.end()};
    for(const auto& out:genesis.vtx.front()->vout){
        if(out.nValue!=0||out.scriptPubKey.size()>remaining)return {};remaining-=out.scriptPubKey.size();
        const auto& script=out.scriptPubKey;auto cursor=script.begin();opcodetype op;Bytes tag,payload;
        if(!script.GetOp(cursor,op)||op!=OP_RETURN||!script.GetOp(cursor,op,tag)||tag!=wanted)continue;
        if(found||!script.GetOp(cursor,op,payload)||cursor!=script.end()||payload.size()!=5||payload[0]!=1||script!=(CScript{}<<OP_RETURN<<tag<<payload))return {};
        uint32_t bits{};for(unsigned i=0;i<4;++i)bits|=uint32_t(payload[i+1])<<(8*i);
        if(!CanonicalBootstrapHashTarget(bits))return {};found=bits;
    }
    return found;
}
enum class NativeBootstrapBlockVerdict {VerifiedCandidate,InvalidBlock,ScientificPhase,HistoryHeld,BudgetExceeded,InvalidRequest,InvalidWork};
struct NativeBootstrapBlockCheck;
class VerifiedNativeBootstrapBlock {
    uint256 m_block,m_parent;ChainBootstrapHashPolicy m_policy;
    VerifiedNativeBootstrapHash m_work;VerifiedProposedTicketRequests m_requests;
    VerifiedNativeBootstrapBlock(uint256 block,uint256 parent,ChainBootstrapHashPolicy policy,
        VerifiedNativeBootstrapHash work,VerifiedProposedTicketRequests requests)
        :m_block(block),m_parent(parent),m_policy(std::move(policy)),m_work(std::move(work)),m_requests(std::move(requests)){}
    friend NativeBootstrapBlockCheck VerifyNativeBootstrapBlock(const CCoinsView&,const CBlockIndex&,
        const NativeRequestHistoryArchive&,const CBlock&,const NativeBlockBootstrapBudget&,const NativeBlockScienceBudget*);
public:
    VerifiedNativeBootstrapBlock(const VerifiedNativeBootstrapBlock&)=default;
    const uint256& Block()const{return m_block;}
    const uint256& Parent()const{return m_parent;}
    const ChainBootstrapHashPolicy& Policy()const{return m_policy;}
    const VerifiedProposedTicketRequests& Requests()const{return m_requests;}
};
struct NativeBootstrapBlockCheck {NativeBootstrapBlockVerdict verdict;std::optional<VerifiedNativeBootstrapBlock> candidate;};
inline NativeBootstrapBlockCheck VerifyNativeBootstrapBlock(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& history,const CBlock& block,const NativeBlockBootstrapBudget& budget,
    const NativeBlockScienceBudget* science_budget=nullptr){
    std::optional<ChainMonetaryPolicy> monetary;
    if(science_budget){
        const auto checked=ReadChainMonetaryPolicy(view,parent,history,science_budget->schedule.archive,science_budget->policy);
        if(!checked.policy)return {NativeBootstrapBlockVerdict::HistoryHeld,{}};
        monetary=checked.policy;
    }
    const bool continuation=budget.ledger_continuation||(monetary&&monetary->Audits()&&monetary->Activation()&&monetary->Activation()->coordination_after_gold);
    const auto phase=InspectNativeBootstrapPhase(view,parent,budget.maximum_phase_blocks);
    if(phase==NativeBootstrapPhaseVerdict::Scientific&&!continuation)return {NativeBootstrapBlockVerdict::ScientificPhase,{}};
    if(phase==NativeBootstrapPhaseVerdict::BudgetExceeded)return {NativeBootstrapBlockVerdict::BudgetExceeded,{}};
    if(!NativeLedgerAllowsCoordination(phase,continuation))return {NativeBootstrapBlockVerdict::HistoryHeld,{}};
    // Coordination carries no spends or scientific subsidy. A versioned audit
    // page may mint ONLY its exact previously reserved audit obligations.
    if(block.hashPrevBlock!=parent.GetBlockHash()||block.vtx.size()!=1||!block.vtx.front()||
       !block.vtx.front()->IsCoinBase()||HasNativeCandidateBlockScienceEnvelope(block))return {NativeBootstrapBlockVerdict::InvalidBlock,{}};
    if(!science_budget)for(const auto& output:block.vtx.front()->vout)if(output.nValue!=0)return {NativeBootstrapBlockVerdict::InvalidBlock,{}};
    const auto policy=ReadChainBootstrapHashPolicy(view,parent,history,budget.requests.archive,budget.policy);
    if(!policy.policy)return {NativeBootstrapBlockVerdict::HistoryHeld,{}};
    // Pay the cheap authenticated work check before request signature work.
    const auto work=VerifyNativeBootstrapHash(view,parent,*policy.policy,block);
    if(!work.proof)return {NativeBootstrapBlockVerdict::InvalidWork,{}};
    if(science_budget){
        const NativeAuditSettlementContext context{history,science_budget->policy,science_budget->schedule,science_budget->signature};
        const auto settlement=PrepareNativeAuditCoordinationPlan(view,parent,block,*monetary,context);
        if(!settlement)return {NativeBootstrapBlockVerdict::HistoryHeld,{}};
        if(!VerifyAuditSettlementOutputs(settlement->page,*block.vtx.front(),{},0))return {NativeBootstrapBlockVerdict::InvalidBlock,{}};
    }
    std::optional<VerifiedRequestRegistry> registry;
    const auto replay=ReplayConfirmedRequestHistoryToParent(view,parent,history,registry,budget.requests);
    if(replay.verdict!=RequestReplayVerdict::AtParent||!registry)return {replay.verdict==RequestReplayVerdict::BudgetExceeded?
        NativeBootstrapBlockVerdict::BudgetExceeded:NativeBootstrapBlockVerdict::HistoryHeld,{}};
    const auto proposed=VerifyProposedTicketRequests(view,parent,*registry,block,budget.requests.scan,budget.requests.maximum_total_requests);
    if(!proposed.batch)return {proposed.verdict==ProposedRequestVerdict::BudgetExceeded?NativeBootstrapBlockVerdict::BudgetExceeded:NativeBootstrapBlockVerdict::InvalidRequest,{}};
    const auto duties=ConfiguredNativeWorkDutyActivation();
    if(duties&&int64_t(parent.nHeight)+1>=duties->height&&!proposed.batch->Requests().empty()&&
       (!science_budget||!VerifyProposedNativeWorkDuties(view,parent,history,*proposed.batch,
            science_budget->policy,science_budget->schedule,science_budget->signature,duties)))
        return {NativeBootstrapBlockVerdict::InvalidRequest,{}};
    bool has_report{};for(const auto& out:block.vtx.front()->vout)has_report|=IsNativeReportScript(out.scriptPubKey);
    if(has_report&&(!science_budget||!VerifyProposedNativeReportCommitments(view,parent,history,block,science_budget->policy,science_budget->schedule,science_budget->signature)))
        return {NativeBootstrapBlockVerdict::InvalidRequest,{}};
    bool has_audit{};for(const auto& out:block.vtx.front()->vout)has_audit|=IsNativeAuditScript(out.scriptPubKey);
    if(has_audit&&(!science_budget||!VerifyProposedNativeAuditEvents(view,parent,history,block,science_budget->policy,science_budget->schedule,science_budget->signature)))
        return {NativeBootstrapBlockVerdict::InvalidRequest,{}};
    return {NativeBootstrapBlockVerdict::VerifiedCandidate,VerifiedNativeBootstrapBlock{block.GetHash(),parent.GetBlockHash(),*policy.policy,*work.proof,*proposed.batch}};
}
// This typed seal still requires maintained native body/UTXO validation and
// admission wiring. It is not science, a ticket, issuance, unbiased randomness,
// a fork rule, an actual bootstrap network or production-safety qualification.
}

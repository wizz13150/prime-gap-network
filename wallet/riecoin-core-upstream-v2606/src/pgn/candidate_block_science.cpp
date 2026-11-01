#include <pgn/candidate_block_science.h>
#include <pgn/canonical_encoding.h>
#include <pgn/proposed_ticket_requests.h>
#include <pgn/native_report_commitment.h>
#include <pgn/native_report_audit.h>
#include <consensus/merkle.h>
#include <kernel/cs_main.h>
#include <primitives/block.h>
#include <chain.h>
#include <coins.h>
#include <hash.h>
#include <algorithm>
#include <array>
#include <limits>
namespace pgn::core {
namespace {
constexpr std::string_view Domain{"PGN/SCIENCE-BLOCK/1"};
EncodingLimits Limits(const NativeBlockScienceBudget& b){return {0,b.maximum_payload_bytes,0,0};}
bool Valid(const NativeBlockScienceBudget& b){return b.maximum_coinbase_outputs&&b.maximum_script_bytes&&b.maximum_payload_bytes&&b.signature.signature_bytes;}
void Put(detail::Writer& out,const Bytes& bytes){out.count(bytes.size());out.raw(bytes);}
Bytes Get(detail::Reader& in,uint64_t maximum){
    const auto size=in.count();detail::encodingRequire(size<=maximum&&size<=in.data.size()-in.position,"Block proof field exceeds input or limit");
    Bytes result(in.data.begin()+in.position,in.data.begin()+in.position+static_cast<size_t>(size));in.position+=size;return result;
}
Bytes Encode(const CandidateBlockScienceProof& proof,const NativeBlockScienceBudget& budget){
    detail::encodingRequire(Valid(budget)&&!proof.request.IsNull()&&!proof.signature.empty()&&
        proof.signature.size()<=budget.signature.signature_bytes&&proof.evidence.inclusion.siblings.size()<=32,"Invalid block proof fields");
    const auto limits=Limits(budget);detail::Writer out{{},limits};out.domain(Domain);
    out.raw({proof.request.begin(),32});out.raw(proof.report_root);Put(out,proof.signature);Put(out,proof.evidence.segment_bytes);
    out.little(proof.evidence.inclusion.index,4);out.count(proof.evidence.inclusion.siblings.size());
    for(const auto& sibling:proof.evidence.inclusion.siblings)out.raw(sibling);
    out.little(proof.evidence.record_index,8);Put(out,proof.evidence.scientific_bytes);return std::move(out.data);
}
CandidateBlockScienceProof Decode(const Bytes& bytes,const ChainSciencePolicy& policy,const NativeBlockScienceBudget& budget){
    const auto limits=Limits(budget);detail::Reader in{bytes,limits};in.domain(Domain);CandidateBlockScienceProof result;
    const auto request=in.digest();std::copy(request.begin(),request.end(),result.request.begin());result.report_root=in.digest();
    result.signature=Get(in,budget.signature.signature_bytes);result.evidence.segment_bytes=Get(in,policy.Definition().evidence.encoding.max_encoded_bytes);
    result.evidence.inclusion.index=in.little(4);const auto siblings=in.count();
    detail::encodingRequire(siblings<=32&&siblings<=(in.data.size()-in.position)/32,"Oversized/truncated Merkle branch");
    result.evidence.inclusion.siblings.reserve(siblings);for(uint64_t i=0;i<siblings;++i)result.evidence.inclusion.siblings.push_back(in.digest());
    result.evidence.record_index=in.little(8);result.evidence.scientific_bytes=Get(in,policy.Definition().proof.max_encoded_bytes);
    detail::encodingRequire(in.position==bytes.size()&&Encode(result,budget)==bytes,"Trailing/noncanonical block proof");return result;
}
}
std::optional<CScript> BuildCandidateBlockScienceEnvelope(const CandidateBlockScienceProof& proof,const NativeBlockScienceBudget& budget){
    try{const auto bytes=Encode(proof,budget);CScript script=CScript{}<<OP_RETURN<<Bytes{BlockScienceEnvelopeTag.begin(),BlockScienceEnvelopeTag.end()}<<bytes;
        if(script.size()>budget.maximum_script_bytes)return {};
        return script;
    }catch(const std::invalid_argument&){return {};}
}
bool HasNativeCandidateBlockScienceEnvelope(const CBlock& block){
    if(block.vtx.empty()||!block.vtx.front())return false;
    for(const auto& output:block.vtx.front()->vout){
        const auto& script=output.scriptPubKey;auto cursor=script.begin();opcodetype operation;Bytes tag;
        if(script.GetOp(cursor,operation)&&operation==OP_RETURN&&script.GetOp(cursor,operation,tag)&&
           tag==Bytes{BlockScienceEnvelopeTag.begin(),BlockScienceEnvelopeTag.end()})return true;
    }
    return false;
}
NativeBlockScienceCheck VerifyNativeCandidateBlockScience(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& history,const CBlock& block,const NativeBlockScienceBudget& budget){
    AssertLockHeld(cs_main);
    if(!Valid(budget))return {NativeBlockScienceVerdict::BudgetExceeded,{},{}};
    if(!parent.phashBlock||block.hashPrevBlock!=parent.GetBlockHash()||block.vtx.empty()||!block.vtx[0]||!block.vtx[0]->IsCoinBase())
        return {NativeBlockScienceVerdict::InvalidBlock,{},{}};
    if(!VerifyProposedNativeReportCommitments(view,parent,history,block,budget.policy,budget.schedule,budget.signature)||
       !VerifyProposedNativeAuditEvents(view,parent,history,block,budget.policy,budget.schedule,budget.signature))
        return {NativeBlockScienceVerdict::InvalidBlock,{},{}};
    for(size_t i=1;i<block.vtx.size();++i)if(!block.vtx[i]||block.vtx[i]->IsCoinBase())return {NativeBlockScienceVerdict::InvalidBlock,{},{}};
    bool mutated=false;if(BlockMerkleRoot(block,&mutated)!=block.hashMerkleRoot||mutated)return {NativeBlockScienceVerdict::InvalidBlock,{},{}};
    if(block.vtx[0]->vout.size()>budget.maximum_coinbase_outputs)return {NativeBlockScienceVerdict::BudgetExceeded,{},{}};
    std::optional<Bytes> payload;
    for(const auto& output:block.vtx[0]->vout){
        const auto& script=output.scriptPubKey;if(script.size()>budget.maximum_script_bytes)return {NativeBlockScienceVerdict::BudgetExceeded,{},{}};
        auto cursor=script.begin();opcodetype operation;Bytes tag,bytes;
        if(!script.GetOp(cursor,operation)||operation!=OP_RETURN||!script.GetOp(cursor,operation,tag)||
           tag!=Bytes{BlockScienceEnvelopeTag.begin(),BlockScienceEnvelopeTag.end()})continue;
        if(payload)return {NativeBlockScienceVerdict::DuplicateProof,{},{}};
        if(!script.GetOp(cursor,operation,bytes)||cursor!=script.end()||output.nValue!=0||
           script!=(CScript{}<<OP_RETURN<<tag<<bytes))return {NativeBlockScienceVerdict::InvalidEncoding,{},{}};
        if(bytes.size()>budget.maximum_payload_bytes)return {NativeBlockScienceVerdict::BudgetExceeded,{},{}};
        payload=std::move(bytes);
    }
    if(!payload)return {NativeBlockScienceVerdict::MissingProof,{},{}};
    const auto ranges=ReadChainRangePolicy(view,parent,history,budget.schedule.archive,budget.policy);
    const auto policy=ReadChainSciencePolicy(view,parent,history,budget.schedule.archive,budget.policy);
    if(!ranges.policy||!policy.policy)return {NativeBlockScienceVerdict::NativeHistoryHeld,{},{}};
    if(!policy.policy->EffectiveParent().IsNull()){
        const auto& e=policy.policy->Definition().evidence;
        const Integer scaled=e.target_numerator*65536/e.target_denominator;
        if(scaled<1||scaled>64U*65536U||block.nNonce!=0||block.nBits!=scaled.convert_to<uint32_t>())
            return {NativeBlockScienceVerdict::InvalidBlock,{},{}};
    }
    // Requests are NEW proposed bytes, not yet a SCRIPT-valid native ancestor.
    // Never manufacture an accepted index merely to reuse the history scanner.
    // The candidate path still pays full registry replay; production incremental
    // native persistence remains unpaid, so this is NOT a hot-path speed claim.
    std::optional<VerifiedRequestRegistry> registry;
    const RequestReplayBudget replay_budget{budget.schedule.maximum_blocks,budget.schedule.maximum_requests,
        budget.schedule.archive,budget.schedule.scan};
    const auto replay=ReplayConfirmedRequestHistoryToParent(view,parent,history,registry,replay_budget);
    if(replay.verdict!=RequestReplayVerdict::AtParent||!registry)
        return {replay.verdict==RequestReplayVerdict::BudgetExceeded?NativeBlockScienceVerdict::BudgetExceeded:
            NativeBlockScienceVerdict::NativeHistoryHeld,{},{}};
    const auto proposed=VerifyProposedTicketRequests(view,parent,*registry,block,budget.schedule.scan,budget.schedule.maximum_requests);
    if(!proposed.batch)return {proposed.verdict==ProposedRequestVerdict::BudgetExceeded?NativeBlockScienceVerdict::BudgetExceeded:
        NativeBlockScienceVerdict::InvalidBlock,{},{}};
    std::optional<CandidateBlockScienceProof> proof;
    try{proof=Decode(*payload,*policy.policy,budget);}catch(const std::invalid_argument&){return {NativeBlockScienceVerdict::InvalidEncoding,{},{}};}
    const auto reserved=DeriveNativeCandidateReservation(view,parent,*ranges.policy,history,budget.schedule,proof->request);
    if(!reserved.reservation)return {NativeBlockScienceVerdict::NativeHistoryHeld,{},{}};
    // A mathematically valid gap alone cannot bypass committed report audits.
    const auto audits=ReadNativeAuditLedger(view,parent,history,budget.policy,budget.schedule,budget.signature);
    if(!audits)return {NativeBlockScienceVerdict::NativeHistoryHeld,{},{}};
    const auto audited=audits->find(proof->request);
    if(audited==audits->end()||!audited->second.Audited()||!NativeAuditOperatorsIndependent(*audits,audited->second)||audited->second.details_root!=std::optional{proof->report_root})
        return {NativeBlockScienceVerdict::ScienceRejected,{},{}};
    auto checked=VerifyNativeProfiledScience(view,parent,*ranges.policy,*policy.policy,*reserved.reservation,
        proof->report_root,proof->signature,proof->evidence,budget.signature);
    if(!checked.candidate)return {NativeBlockScienceVerdict::ScienceRejected,{},checked.verdict};
    HashWriter source{TaggedHash("PGN/UNAPPROVED/CANONICAL-SCIENCE-SOURCE/1")};
    source<<policy.policy->Id()<<checked.candidate->Science().Science().Key();
    return {NativeBlockScienceVerdict::VerifiedCandidate,VerifiedNativeBlockScience{block.GetHash(),parent.GetBlockHash(),source.GetHash(),
        block.vtx.size(),*ranges.policy,*policy.policy,*checked.candidate},checked.verdict};
}
FactHistoryStep PrepareNativeBlockScienceForward(const CCoinsView& view,const CBlockIndex& parent,
    const CBlockIndex& proposed,const VerifiedNativeBlockScience& candidate){
    AssertLockHeld(cs_main);
    if(!parent.phashBlock||!proposed.phashBlock||proposed.GetBlockHash()!=candidate.m_block||
       parent.GetBlockHash()!=candidate.m_parent||proposed.nTx!=candidate.m_transactions)
        throw std::logic_error("Scientific block metadata must match the exact checked block and parent");
    const std::array claims{candidate.m_science};
    return PrepareNativeProfiledScienceForward(view,parent,proposed,candidate.m_ranges,candidate.m_policy,claims);
}
NativeBlockScienceCheck RestoreNativePublishedBlockScience(const CCoinsView& view,
    const CBlockIndex& tip,const CBlockIndex& source,const NativeRequestHistoryArchive& history,
    const CBlock& block,const NativeBlockScienceBudget& budget){
    AssertLockHeld(cs_main);
    const auto head=view.GetPgnFactHead();
    if(!head||!head->Valid()||!tip.phashBlock||!source.phashBlock||!source.pprev||!source.pprev->phashBlock||
       source.nHeight<=0||source.nHeight>tip.nHeight||tip.GetAncestor(source.nHeight)!=&source||
       !tip.IsValid(BLOCK_VALID_SCRIPTS)||!source.IsValid(BLOCK_VALID_SCRIPTS)||
       head->tip!=tip.GetBlockHash()||view.GetBestBlock()!=head->tip||block.GetHash()!=source.GetBlockHash())
        return {NativeBlockScienceVerdict::InvalidBlock,{},{}};
    const auto retained=view.GetPgnFactForward(source.GetBlockHash());
    if(!retained||!retained->Valid()||retained->before!=std::optional{FactHead{head->chain,source.pprev->GetBlockHash()}}||
       retained->after!=FactHead{head->chain,source.GetBlockHash()}||retained->changes.size()!=1)
        return {NativeBlockScienceVerdict::NativeHistoryHeld,{},{}};
    const auto& delta=retained->changes.front();
    if(delta.before||!delta.after||delta.after->block!=source.GetBlockHash()||view.GetPgnFact(delta.fact)!=delta.after)
        return {NativeBlockScienceVerdict::NativeHistoryHeld,{},{}};
    class BeforeSource final:public CCoinsViewBacked {
        FactHead m_head;uint256 m_fact;
    public:
        BeforeSource(const CCoinsView& current,FactHead before,uint256 fact)
            :CCoinsViewBacked(const_cast<CCoinsView*>(&current)),m_head(before),m_fact(fact){}
        uint256 GetBestBlock()const override{return m_head.tip;}
        std::optional<FactHead> GetPgnFactHead()const override{return m_head;}
        std::optional<FactClaim> GetPgnFact(const uint256& key)const override{
            return key==m_fact?std::nullopt:base->GetPgnFact(key);
        }
    } before{view,*retained->before,delta.fact};
    auto checked=VerifyNativeCandidateBlockScience(before,*source.pprev,history,block,budget);
    if(!checked.candidate)return checked;
    try{
        if(PrepareNativeBlockScienceForward(before,*source.pprev,source,*checked.candidate).forward!=*retained)
            return {NativeBlockScienceVerdict::NativeHistoryHeld,{},{}};
    }catch(const std::logic_error&){return {NativeBlockScienceVerdict::NativeHistoryHeld,{},{}};}
    return checked;
}
TicketSeedCheck DeriveScientificTicketSeed(const CCoinsView& view,const CBlockIndex& parent,
    const ConfirmedTicketRequest& confirmation,uint32_t delay,const VerifiedNativeBlockScience& science){
    AssertLockHeld(cs_main);
    const uint64_t height=uint64_t(confirmation.Height())+delay;
    if(!delay||height>uint64_t(std::numeric_limits<int32_t>::max()))return {TicketSeedVerdict::InvalidDelay,{}};
    if(!TicketRequestConfirmationOnBranch(view,parent,confirmation)||
       !ChainSciencePolicyOnBranch(view,parent,science.m_policy))return {TicketSeedVerdict::InvalidParent,{}};
    if(height>uint64_t(parent.nHeight))return {TicketSeedVerdict::WaitingForLaterBlock,{}};
    const auto* source=parent.GetAncestor(static_cast<int32_t>(height));
    if(!source||!source->phashBlock||!source->pprev||!source->pprev->phashBlock||
       source->GetBlockHash()!=science.m_block||source->pprev->GetBlockHash()!=science.m_parent||
       source->nTx!=science.m_transactions)return {TicketSeedVerdict::InvalidSeedBlock,{}};
    HashWriter hash{TaggedHash("PGN/UNAPPROVED/SCIENTIFIC-TICKET-SEED/1")};
    hash<<science.m_policy.Chain()<<science.m_source<<confirmation.Request().Id();
    ConfirmedTicketSeed result{confirmation,science.m_block,hash.GetHash(),static_cast<int32_t>(height)};
    result.m_scientific=true;
    result.m_material=science.m_source;result.m_policy=science.m_policy.Id();
    result.m_fact=science.m_science.Science().Science().Key();
    result.m_work=science.m_science.Science().Science().OwnerProof().Context().work;
    if(!TicketSeedOnBranch(view,parent,result))return {TicketSeedVerdict::InvalidSeedBlock,{}};
    return {TicketSeedVerdict::DerivedAtParent,std::move(result)};
}
}

#include <pgn/chain_range_policy.h>
#include <pgn/profile_policy_codec_internal.h>
#include <pgn/canonical_encoding.h>
#include <chain.h>
#include <coins.h>
#include <hash.h>
#include <kernel/cs_main.h>
#include <algorithm>
#include <limits>
namespace pgn::core {
namespace {using namespace range_policy_codec;}
GenesisPolicyPayloadRead ReadCandidateGenesisPolicyPayload(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& archive,RequestHistoryArchiveBudget archiveBudget,RangePolicyReadBudget budget,std::string_view expectedTag){
    AssertLockHeld(cs_main);
    if(!Valid(budget))return {ChainRangePolicyVerdict::BudgetExceeded,{}};
    const auto genesis=archive.Load(view,parent,0,archiveBudget);
    if(!genesis.block)return {genesis.verdict==RequestArchiveVerdict::Missing?ChainRangePolicyVerdict::MissingHistory:
        genesis.verdict==RequestArchiveVerdict::BudgetExceeded?ChainRangePolicyVerdict::BudgetExceeded:
        genesis.verdict==RequestArchiveVerdict::InvalidParent?ChainRangePolicyVerdict::InvalidParent:ChainRangePolicyVerdict::InvalidConfiguration,{}};
    const auto& outputs=genesis.block->Coinbase()->vout;
    if(outputs.size()>budget.maximum_outputs)return {ChainRangePolicyVerdict::BudgetExceeded,{}};
    uint64_t remaining=budget.maximum_script_bytes;
    for(const auto& out:outputs){if(out.scriptPubKey.size()>remaining)return {ChainRangePolicyVerdict::BudgetExceeded,{}};remaining-=out.scriptPubKey.size();}
    bool found=false;Bytes canonical;
    for(const auto& output:outputs){
        const auto& script=output.scriptPubKey;auto cursor=script.begin();opcodetype op;Bytes tag,payload;
        if(!script.GetOp(cursor,op)||op!=OP_RETURN||!script.GetOp(cursor,op,tag)||
           tag.size()!=expectedTag.size()||!std::equal(tag.begin(),tag.end(),expectedTag.begin()))continue;
        if(found)return {ChainRangePolicyVerdict::DuplicateConfiguration,{}};
        if(!CheckMinimalPush(tag,op)||output.nValue!=0)return {ChainRangePolicyVerdict::InvalidConfiguration,{}};
        // Maintained GetOp without a data destination validates/skips the push
        // BEFORE allocating its payload. No local payload limit is merely a
        // post-allocation check; the full-output script budget is also paid.
        auto end=cursor;opcodetype push;
        if(!script.GetOp(end,push)||push>OP_PUSHDATA4||end!=script.end())return {ChainRangePolicyVerdict::InvalidConfiguration,{}};
        const size_t prefix=push<OP_PUSHDATA1?1:push==OP_PUSHDATA1?2:push==OP_PUSHDATA2?3:5;
        const size_t payloadBytes=size_t(end-cursor)-prefix;
        if(payloadBytes>budget.maximum_payload_bytes)return {ChainRangePolicyVerdict::BudgetExceeded,{}};
        if(!script.GetOp(cursor,op,payload)||!CheckMinimalPush(payload,op)||cursor!=script.end())
            return {ChainRangePolicyVerdict::InvalidConfiguration,{}};
        if(payload.size()>budget.maximum_payload_bytes)return {ChainRangePolicyVerdict::BudgetExceeded,{}};
        found=true;canonical=std::move(payload);
    }
    if(!found)return {ChainRangePolicyVerdict::MissingConfiguration,{}};
    return {ChainRangePolicyVerdict::Confirmed,genesis.block,std::move(canonical)};
}
ChainRangePolicyCheck ReadChainRangePolicy(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& archive,RequestHistoryArchiveBudget archiveBudget,RangePolicyReadBudget budget){
    auto payload=ReadCandidateGenesisPolicyPayload(view,parent,archive,archiveBudget,budget,RangePolicyEnvelopeTag);
    if(!payload.genesis)return {payload.verdict,{}};
    std::optional<RangePoolDefinition> definition;
    try{const auto limits=Limits(budget);definition=Decode(payload.payload,limits);}
    catch(const ResourceLimit&){return {ChainRangePolicyVerdict::BudgetExceeded,{}};}
    catch(const std::invalid_argument&){return {ChainRangePolicyVerdict::InvalidConfiguration,{}};}
    HashWriter hash{TaggedHash("PGN/UNAPPROVED/CHAIN-RANGE-POLICY/1")};
    hash<<payload.genesis->Chain()<<payload.genesis->Block()<<payload.payload;
    return {ChainRangePolicyVerdict::Confirmed,ChainRangePolicy{payload.genesis->Chain(),payload.genesis->Block(),hash.GetHash(),std::move(*definition)}};
}
bool ChainRangePolicyOnBranch(const CCoinsView& view,const CBlockIndex& parent,const ChainRangePolicy& policy){
    AssertLockHeld(cs_main);const auto head=view.GetPgnFactHead();
    if(!head||!parent.phashBlock||parent.nHeight<0||!parent.IsValid(BLOCK_VALID_SCRIPTS)||head->chain!=policy.Chain()||
       head->tip!=parent.GetBlockHash()||head->tip!=view.GetBestBlock())return false;
    const auto* genesis=parent.GetAncestor(0);
    return genesis&&genesis->phashBlock&&genesis->IsValid(BLOCK_VALID_SCRIPTS)&&genesis->GetBlockHash()==policy.Genesis()&&Valid(policy.Definition());
}
}

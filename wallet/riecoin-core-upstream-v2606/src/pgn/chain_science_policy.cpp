#include <pgn/native_elapsed_retarget.h>
#include <pgn/chain_science_policy.h>
#include <pgn/profile_policy_codec_internal.h>
#include <pgn/canonical_encoding.h>
#include <pgn/native_retarget_policy.h>
#include <pgn/native_hourly_activation.h>
#include <pgn/native_hourly_consensus_quote.h>
#include <numeric>
#include <coins.h>
#include <chain.h>
#include <hash.h>
#include <kernel/cs_main.h>
namespace pgn::core {
namespace {using namespace science_policy_codec;}
ChainSciencePolicyCheck ReadChainSciencePolicy(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& archive,RequestHistoryArchiveBudget archiveBudget,RangePolicyReadBudget budget,
    const RangeScheduleBudget* schedule,const ScriptWorkBudget* signature){
    auto payload=ReadCandidateGenesisPolicyPayload(view,parent,archive,archiveBudget,budget,SciencePolicyEnvelopeTag);
    if(!payload.genesis)return {payload.verdict,{}};
    const auto hourly=ConfiguredNativeHourlyMerit(payload.genesis->Block());
    if(hourly&&(hourly->genesis!=payload.genesis->Block()||hourly->median_time<0||hourly->median_time%3600))
        return {ChainRangePolicyVerdict::InvalidConfiguration,{}};
    const bool hourly_active=hourly&&parent.GetMedianTimePast()>=hourly->median_time;
    std::optional<ScienceVerificationDefinition> definition;
    try{const auto limits=Limits(budget);definition=Decode(payload.payload,limits);}
    catch(const ResourceLimit&){return {ChainRangePolicyVerdict::BudgetExceeded,{}};}
    catch(const std::invalid_argument&){return {ChainRangePolicyVerdict::InvalidConfiguration,{}};}
    uint256 effective_parent;
    const auto target_payload=ReadCandidateGenesisPolicyPayload(view,parent,archive,archiveBudget,budget,NativeRetargetPolicyTag);
    if(target_payload.verdict!=ChainRangePolicyVerdict::MissingConfiguration){
        if(!target_payload.genesis)return {target_payload.verdict,{}};
        const auto target=DecodeNativeRetargetPayload(target_payload.payload);
        if(!target)return {ChainRangePolicyVerdict::InvalidConfiguration,{}};
        const auto& original=definition->evidence;
        const Integer scaled=original.target_numerator*65536;
        if(scaled%original.target_denominator!=0||scaled/original.target_denominator!=target->initial_q16)
            return {ChainRangePolicyVerdict::InvalidConfiguration,{}};
        if(hourly_active){
            if(!schedule||!signature)return {ChainRangePolicyVerdict::MissingHistory,{}};
            const auto ranges=ReadChainRangePolicy(view,parent,archive,archiveBudget,budget);
            if(!ranges.policy)return {ranges.verdict,{}};
            static pgn::discussion::complete_return_detail::Cache cached;
            const auto quote=pgn::discussion::ReadNativeHourlyConsensusQuote(view,parent,*ranges.policy,archive,
                budget,*schedule,*signature,cached,target->maximum_q16);
            if(!quote.quote)return {quote.verdict==pgn::discussion::HourlyConsensusVerdict::AwaitHistory?
                ChainRangePolicyVerdict::MissingHistory:ChainRangePolicyVerdict::InvalidParent,{}};
            definition->evidence.target_numerator=quote.quote->target.proof_numerator;
            definition->evidence.target_denominator=quote.quote->target.proof_denominator;
        }else{
            const auto quote=DeriveNextMeritTarget(parent,*target);
            if(!quote)return {ChainRangePolicyVerdict::InvalidParent,{}};
            const auto divisor=std::gcd(quote->target_q16,65536U);
            definition->evidence.target_numerator=quote->target_q16/divisor;
            definition->evidence.target_denominator=65536U/divisor;
        }
        effective_parent=parent.GetBlockHash();
    }else if(hourly_active)return {ChainRangePolicyVerdict::InvalidConfiguration,{}};
    HashWriter hash{TaggedHash("PGN/UNAPPROVED/CHAIN-SCIENCE-POLICY/1")};hash<<payload.genesis->Chain()<<payload.genesis->Block()<<payload.payload;
    return {ChainRangePolicyVerdict::Confirmed,ChainSciencePolicy{payload.genesis->Chain(),payload.genesis->Block(),hash.GetHash(),std::move(*definition),effective_parent}};
}
bool ChainSciencePolicyOnBranch(const CCoinsView& view,const CBlockIndex& parent,const ChainSciencePolicy& policy){
    AssertLockHeld(cs_main);const auto head=view.GetPgnFactHead();
    if(!head||!parent.phashBlock||parent.nHeight<0||!parent.IsValid(BLOCK_VALID_SCRIPTS)||head->chain!=policy.Chain()||
       head->tip!=parent.GetBlockHash()||head->tip!=view.GetBestBlock()||
       (!policy.EffectiveParent().IsNull()&&policy.EffectiveParent()!=parent.GetBlockHash()))return false;
    const auto* genesis=parent.GetAncestor(0);return genesis&&genesis->phashBlock&&genesis->IsValid(BLOCK_VALID_SCRIPTS)&&genesis->GetBlockHash()==policy.Genesis();
}
}

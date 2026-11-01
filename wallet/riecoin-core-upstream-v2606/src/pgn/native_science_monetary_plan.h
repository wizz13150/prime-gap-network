// Copyright (c) 2026 The PGN developers
// MIT. Own verified-science monetary planning; NOT runtime payout activation.
#pragma once
#include <pgn/native_zero_issuance.h>
#include <consensus/merkle.h>
#include <pgn/candidate_block_science.h>
namespace pgn::core {
class NativeScienceMonetaryPlan {
    uint256 m_block;
    IssuanceHistoryStep m_forward;
    ChainMonetaryPolicy m_policy;
    CScript m_recipient;
    CAmount m_gross,m_winner;
    NativeScienceMonetaryPlan(uint256 block,IssuanceHistoryStep forward,ChainMonetaryPolicy policy,
        CScript recipient,CAmount gross,CAmount winner):m_block(block),m_forward(std::move(forward)),
        m_policy(std::move(policy)),m_recipient(std::move(recipient)),m_gross(gross),m_winner(winner){}
    friend std::optional<NativeScienceMonetaryPlan> PrepareNativeScienceMonetaryPlan(
        const CCoinsView&,const CBlockIndex&,const CBlock&,const VerifiedNativeBlockScience&,const ChainMonetaryPolicy&);
public:
    const uint256& Block()const{return m_block;}
    const IssuanceHistoryStep& Forward()const{return m_forward;}
    const ChainMonetaryPolicy& Policy()const{return m_policy;}
    const CScript& Recipient()const{return m_recipient;}
    CAmount Gross()const{return m_gross;}CAmount WinnerSubsidy()const{return m_winner;}
};
inline std::optional<NativeScienceMonetaryPlan> PrepareNativeScienceMonetaryPlan(
    const CCoinsView& view,const CBlockIndex& parent,const CBlock& block,const VerifiedNativeBlockScience& science,const ChainMonetaryPolicy& policy){
    if(!NativeMonetaryPolicyOnBranch(view,parent,policy)||block.hashPrevBlock!=parent.GetBlockHash()||
       science.Block()!=block.GetHash()||BlockMerkleRoot(block)!=block.hashMerkleRoot)return {};
    const auto& reservation=science.Science().Science().Reservation();
    const auto& request=reservation.Lease().owner.Request();
    if(reservation.Parent()!=parent.GetBlockHash()||request.chain!=policy.Chain()||request.reward.empty()||request.reward.IsUnspendable())return {};
    const auto before=view.GetPgnIssuanceState();
    if(!before||!before->Valid()||before->tip!=parent.GetBlockHash()||!before->partition||!before->pools||before->partition->policy!=policy.Id()||
       before->partition->ordinary_cap!=policy.Definition().ordinary_cap)return {};
    const auto total=IssuanceBudgetState::Create(before->cap,before->minted,before->promised);
    const auto ordinary=IssuanceBudgetState::Create(before->partition->ordinary_cap,before->partition->ordinary_minted,before->partition->ordinary_promised);
    if(!total||!ordinary)return {};
    CAmount gross{};std::optional<SubsidyDistributionQuote> split;
    std::optional<GoldEpochBalances> epochs=before->epochs;
    if(policy.Epochs()){
        if(!epochs){
            // A pre-epoch zero-issuance testnet can migrate only with its exact
            // authenticated empty scientific history, not an inferred zero.
            if(before->minted||before->promised||before->pools->near_miss||before->pools->audit)return {};
            for(auto* index=&parent;index&&index->nHeight>0;index=index->pprev){
                const auto step=view.GetPgnFactForward(index->GetBlockHash());
                if(!step||!step->Valid()||!index->pprev||step->before!=std::optional{FactHead{policy.Chain(),index->pprev->GetBlockHash()}}||
                   step->after!=FactHead{policy.Chain(),index->GetBlockHash()}||!step->changes.empty())return {};
            }
            epochs=GoldEpochBalances{};
        }
        if(epochs->golds==std::numeric_limits<uint64_t>::max())return {};
        const auto grant=QuoteGoldEpoch(*policy.Epochs(),epochs->golds);if(!grant)return {};
        gross=grant->gold+grant->silver+grant->bronze+grant->red+grant->audit;
        split=SubsidyDistributionQuote{grant->gold,grant->silver+grant->bronze+grant->red,grant->audit};
        epochs->silver+=grant->silver;epochs->bronze+=grant->bronze;epochs->red+=grant->red;
        ++epochs->golds;epochs->last_gold_height=parent.nHeight+1;
    }else{
        const auto quote=QuoteIssuedSupplyLinearSubsidy(policy.Definition().curve,*total,*ordinary);if(!quote)return {};
        gross=quote->maximum_unpromised_grant;split=QuoteSubsidyDistribution(gross,policy.Definition());
    }
    if(!split)return {};
    const auto total_reserved=total->Reserve(gross),ordinary_reserved=ordinary->Reserve(gross);
    if(!total_reserved||!ordinary_reserved)return {};
    const auto total_after=total_reserved->MintReserved(split->winner),ordinary_after=ordinary_reserved->MintReserved(split->winner);
    if(!total_after||!ordinary_after)return {};
    auto after=*before;after.epochs=epochs;after.tip=block.GetHash();after.minted=total_after->CumulativeMinted();after.promised=total_after->Promised();
    after.partition->ordinary_minted=ordinary_after->CumulativeMinted();after.partition->ordinary_promised=ordinary_after->Promised();
    after.pools->near_miss+=split->near_miss;after.pools->audit+=split->audit;
    IssuanceHistoryStep forward{IssuanceDirection::Forward,IssuanceCommit{before,after}};
    if(!forward.Valid()||after.minted-before->minted+after.promised-before->promised!=gross)return {};
    return NativeScienceMonetaryPlan{block.GetHash(),forward,policy,request.reward,gross,split->winner};
}
// actual_fees MUST come only from maintained native CheckTxInputs, never an RPC,
// block sender or this arithmetic object. This check alone never writes coins.
inline bool VerifyNativeScienceCoinbase(const NativeScienceMonetaryPlan& plan,const CBlock& block,CAmount actual_fees){
    if(block.GetHash()!=plan.Block()||BlockMerkleRoot(block)!=block.hashMerkleRoot||block.vtx.empty()||!block.vtx.front()||!block.vtx.front()->IsCoinBase())return false;
    const auto fees=QuoteNativeWinnerFees(actual_fees,plan.Policy().Definition());
    if(!fees||*fees>PGN_MAX_SUPPLY_ATOMIC-plan.WinnerSubsidy())return false;
    const CAmount expected=plan.WinnerSubsidy()+*fees;unsigned paid{};
    for(const auto& output:block.vtx.front()->vout){
        if(output.nValue<0)return false;
        if(output.nValue==0)continue;
        if(++paid>1||output.scriptPubKey!=plan.Recipient()||output.nValue!=expected)return false;
    }
    return paid==(expected>0?1U:0U);
}
}

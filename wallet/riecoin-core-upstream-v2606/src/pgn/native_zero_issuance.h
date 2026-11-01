// Copyright (c) 2026 The PGN developers
// MIT. Native zero-issuance continuity, never positive-reward authority.
#pragma once
#include <chain.h>
#include <coins.h>
#include <primitives/block.h>
#include <pgn/chain_monetary_policy.h>
#include <pgn/issuance_history.h>
namespace pgn::core {
inline bool NativeMonetaryPolicyOnBranch(const CCoinsView& view,const CBlockIndex& parent,const ChainMonetaryPolicy& policy){
    const auto head=view.GetPgnFactHead();const auto* genesis=parent.GetAncestor(0);
    return parent.phashBlock&&genesis&&genesis->phashBlock&&head&&head->chain==policy.Chain()&&
        head->tip==parent.GetBlockHash()&&view.GetBestBlock()==parent.GetBlockHash()&&genesis->GetBlockHash()==policy.Genesis();
}
inline bool ZeroCoinbase(const CBlock& block){
    if(block.vtx.empty()||!block.vtx.front()||!block.vtx.front()->IsCoinBase())return false;
    for(const auto& output:block.vtx.front()->vout)if(output.nValue!=0)return false;
    return true;
}
// Call ONLY after the actual pinned genesis passed native body validation and
// its fact head was staged in this same private child. An old absence at a
// non-genesis tip can never be converted to a fabricated zero budget.
inline std::optional<IssuanceCommit> PrepareNativeZeroIssuanceEnrollment(
    const CCoinsView& view,const CBlockIndex& index,const CBlock& genesis,const ChainMonetaryPolicy& policy){
    if(index.pprev||index.nHeight!=0||!index.phashBlock||!index.IsValid(BLOCK_VALID_SCRIPTS)||
       index.GetBlockHash()!=genesis.GetHash()||policy.Genesis()!=index.GetBlockHash()||
       !NativeMonetaryPolicyOnBranch(view,index,policy)||view.GetPgnIssuanceState()||!ZeroCoinbase(genesis))return {};
    IssuanceSnapshot initial{index.GetBlockHash(),PGN_MAX_SUPPLY_ATOMIC,0,0,
        IssuanceBudgetPartition{policy.Id(),policy.Definition().ordinary_cap,0,0,0,0},OrdinaryRewardPools{0,0}};
    IssuanceCommit commit{{},initial};return commit.Valid()?std::optional{commit}:std::nullopt;
}
// Call before native connection, publish ONLY after own scientific/bootstrap
// proof and all native scripts/body checks passed. No supplied quote authorizes
// a payout, and no missing/v1 partition is silently enrolled on a later block.
inline std::optional<IssuanceHistoryStep> PrepareNativeZeroIssuanceForward(
    const CCoinsView& view,const CBlockIndex& parent,const CBlock& block,const ChainMonetaryPolicy& policy){
    if(!NativeMonetaryPolicyOnBranch(view,parent,policy)||block.hashPrevBlock!=parent.GetBlockHash()||!ZeroCoinbase(block))return {};
    const auto before=view.GetPgnIssuanceState();
    if(!before||!before->Valid()||before->tip!=parent.GetBlockHash()||!before->partition||
       before->partition->policy!=policy.Id()||before->partition->ordinary_cap!=policy.Definition().ordinary_cap)return {};
    auto after=*before;after.tip=block.GetHash();
    IssuanceHistoryStep step{IssuanceDirection::Forward,IssuanceCommit{before,after}};
    return step.Valid()?std::optional{step}:std::nullopt;
}
}

// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <chain.h>
#include <coins.h>

namespace pgn::core {
// Local EMPTY fact-history continuity, never scientific/block admission.
// No head means no enrollment and no operation on an upstream/legacy chain.
// Preflight against the actual parent BEFORE native ConnectBlock. Stage ONLY
// AFTER that native validation succeeds, in its SAME private UTXO child.
// Retaining an empty forward is necessary: a later disconnect must know this
// block's parent even when the block did not publish a mathematical claim.
inline std::optional<FactHistoryStep> PrepareLocalEmptyFactForward(
    const CCoinsViewCache& view,const CBlockIndex& index)
{
    const auto head=view.GetPgnFactHead();
    if(!head)return {};
    if(!head->Valid()||!index.phashBlock||!index.pprev||!index.pprev->phashBlock||
       index.nHeight<=0||int64_t(index.pprev->nHeight)+1!=index.nHeight||
       !index.pprev->IsValid(BLOCK_VALID_SCRIPTS)||index.GetBlockHash().IsNull()||
       index.GetBlockHash()==head->tip||head->tip!=view.GetBestBlock()||
       index.pprev->GetBlockHash()!=head->tip)
        throw std::logic_error("Fact forward requires exact enrolled native parent");
    FactHistoryStep step{FactDirection::Forward,
        FactCommit{head,FactHead{head->chain,index.GetBlockHash()},{}}};
    if(!step.Valid())throw std::logic_error("Invalid empty fact forward");
    return step;
}
} // namespace pgn::core

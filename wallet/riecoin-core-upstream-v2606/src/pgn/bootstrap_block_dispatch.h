// MIT. Routing only: this predicate never authorizes a block.
#pragma once

#include <pgn/candidate_block_science.h>
#include <chain.h>
#include <consensus/merkle.h>
#include <primitives/block.h>

namespace pgn::core {

// A body without a science envelope can be checked directly by the complete
// bootstrap verifier. Retain the science verifier's cheap body/budget bounds:
// bootstrap acceptance still verifies its authenticated history, work,
// requests, reports, audits and exact settlement outputs. A true result is
// only a dispatch decision, never a proof or an admission seal.
inline bool CanDispatchDirectlyToNativeBootstrap(const CBlock& block,
    const CBlockIndex& parent, const NativeBlockScienceBudget& budget)
{
    if (!budget.maximum_coinbase_outputs || !budget.maximum_script_bytes ||
        !budget.maximum_payload_bytes || !budget.signature.signature_bytes ||
        !parent.phashBlock || block.hashPrevBlock != parent.GetBlockHash() ||
        block.vtx.size() != 1 || !block.vtx.front() ||
        !block.vtx.front()->IsCoinBase() ||
        block.vtx.front()->vout.size() > budget.maximum_coinbase_outputs) return false;

    bool mutated{};
    if (BlockMerkleRoot(block, &mutated) != block.hashMerkleRoot || mutated) return false;
    for (const auto& output : block.vtx.front()->vout) {
        if (output.scriptPubKey.size() > budget.maximum_script_bytes) return false;
    }

    // Recognize known nonminimal/malformed tags as well. Their errors belong
    // to the complete science verifier, never to a permissive fallback.
    return !HasNativeCandidateBlockScienceEnvelope(block);
}

} // namespace pgn::core

// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <chain.h>
#include <pgn/fact_history.h>

namespace pgn::core {
// Bind an already storage-validated LOCAL history to actual canonical native
// header parents. Neither this check nor ReplayBlocks authenticates scientific
// facts, work admission or coordinated forged databases. Every intermediate
// forward is checked, including those cancelled in the net transition.
template <typename LookupIndex>
void ValidateLocalFactReplayParents(const std::optional<FactCommit>& pending,
    const std::optional<FactHistoryTrail>& trail, LookupIndex&& lookup)
{
    if (!trail) return; // Unenrolled/R1 compatibility, not an archive claim.
    if (!pending || !trail->ValidFor(*pending))
        throw std::logic_error("Local fact replay trail/net mismatch");
    for (const auto& step : trail->steps) {
        const auto& forward = step.forward;
        const CBlockIndex* child = lookup(forward.after.tip);
        const CBlockIndex* parent = lookup(forward.before->tip);
        if (!child || !parent || !child->phashBlock || !parent->phashBlock ||
            child->GetBlockHash() != forward.after.tip || parent->GetBlockHash() != forward.before->tip ||
            child->pprev != parent)
            throw std::logic_error("Local fact replay requires every exact native direct parent");
    }
}
} // namespace pgn::core

// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <chain.h>
#include <pgn/issuance_history.h>
#include <optional>
#include <stdexcept>

namespace pgn::core {
/** Bind LOCAL provisional per-block history to known native header parents.
 * Call only AFTER the native DB K/I/J/M/N/HEAD validators. Missing trail may
 * mean a genuinely legacy/K1 record, NOT an invented zero or a K2 fallback:
 * the DB validator must reject missing N for an enrolled history before here.
 * No budget is computed, no header/coin/cache is mutated, and no coordinated
 * forged tuple or header is authenticated. Lookup returns canonical index
 * objects, never inserts them. Cost: two lookups per recorded history step.
 * Check forward's original before->after relation even for Rewind. In a net
 * identity A->B->A these are actual edges, never a fake A->A block parent. */
template <typename LookupIndex>
void ValidateLocalIssuanceReplayParents(
    const std::optional<IssuanceCommit>& validated_pending,
    const std::optional<IssuanceHistoryTrail>& validated_trail,
    LookupIndex&& lookup)
{
    if (!validated_trail) return; // Legacy/K1 compatibility, not a history claim.
    if (!validated_pending || !validated_trail->ValidFor(*validated_pending))
        throw std::logic_error("Local issuance replay trail/net mismatch");
    for (const auto& step : validated_trail->steps) {
        const auto& forward = step.forward;
        const CBlockIndex* child = lookup(forward.after.tip);
        const CBlockIndex* parent = lookup(forward.before->tip);
        if (!child || !parent || !child->phashBlock || !parent->phashBlock ||
            child->GetBlockHash() != forward.after.tip || parent->GetBlockHash() != forward.before->tip ||
            child->pprev != parent)
            throw std::logic_error("Local issuance replay requires every exact native direct parent");
    }
}
} // namespace pgn::core

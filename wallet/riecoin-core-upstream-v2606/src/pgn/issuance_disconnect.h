// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <chain.h>
#include <coins.h>
#include <pgn/issuance_history.h>
#include <optional>
#include <stdexcept>

namespace pgn::core {
/** Read-only preflight for the normal DisconnectTip path, NOT ReplayBlocks.
 * Binds provisional local history to the real native block/parent hashes and
 * the complete immediately known parent snapshot (anchor or parent forward).
 * It does not scan global ancestry or authenticate coordinated forged tuples.
 * does not authenticate issuance or define emission/fees/scientific rewards.
 * Absence means genuinely un-enrolled legacy metadata, NEVER a zero budget or
 * an approved PGN state. Native corruption/mismatch propagates as a local error.
 * Caller holds cs_main, keeps this view private, and stages the returned Rewind
 * only AFTER successful native undo, before publishing that same child view.
 * No lazy cache tips/coins/payloads are filled by this preflight in native views. */
inline std::optional<IssuanceHistoryStep> PrepareLocalIssuanceRewind(
    const CCoinsViewCache& view, const CBlockIndex& block_index)
{
    const auto current = view.GetPgnIssuanceState();
    if (!current) return {};
    if (!block_index.phashBlock || !block_index.pprev || !block_index.pprev->phashBlock ||
        current->tip != block_index.GetBlockHash())
        throw std::logic_error("Local issuance rewind requires the exact active block index");
    const auto forward = view.GetPgnIssuanceForward(block_index.GetBlockHash());
    if (!forward || !forward->before || forward->after != *current ||
        forward->before->tip != block_index.pprev->GetBlockHash() ||
        !InvertIssuanceCommit(*forward, current))
        throw std::logic_error("Local issuance rewind requires complete history at the exact native parent");
    const auto anchor = view.GetPgnIssuanceHistoryAnchor();
    if (!anchor || !anchor->Valid())
        throw std::logic_error("Local issuance rewind requires a complete known history anchor");
    std::optional<IssuanceSnapshot> known_parent;
    if (forward->before->tip == anchor->tip) {
        known_parent = anchor;
    } else {
        const auto parent_forward = view.GetPgnIssuanceForward(forward->before->tip);
        if (parent_forward && parent_forward->Valid() && parent_forward->before)
            known_parent = parent_forward->after;
    }
    if (known_parent != forward->before)
        throw std::logic_error("Local issuance rewind parent snapshot is missing or contradictory");
    return IssuanceHistoryStep{IssuanceDirection::Rewind, *forward};
}
} // namespace pgn::core

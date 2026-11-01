// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <chain.h>
#include <coins.h>

namespace pgn::core {
// cs_main held; caller keeps view private. This binds local retained history to
// actual native block/parent and current claims, NOT to forged whole databases.
// Stage returned step only AFTER native undo succeeds, in that SAME child view.
inline std::optional<FactHistoryStep> PrepareLocalFactRewind(
    const CCoinsViewCache& view, const CBlockIndex& index)
{
    const auto current = view.GetPgnFactHead();
    if (!current) return {};
    if (!index.phashBlock || !index.pprev || !index.pprev->phashBlock || current->tip != index.GetBlockHash())
        throw std::logic_error("Fact rewind requires exact active native block index");
    const auto forward = view.GetPgnFactForward(index.GetBlockHash());
    if (!forward || !forward->before || forward->after != *current ||
        forward->before->tip != index.pprev->GetBlockHash() || !InvertFactCommit(*forward,*current))
        throw std::logic_error("Fact rewind requires full history at exact native parent");
    const auto anchor = view.GetPgnFactHistoryAnchor();
    if (!anchor) throw std::logic_error("Fact rewind missing enrolled anchor");
    std::optional<FactHead> known_parent;
    if (forward->before->tip == anchor->tip) known_parent = anchor;
    else if (const auto parent = view.GetPgnFactForward(forward->before->tip)) known_parent = parent->after;
    if (known_parent != forward->before) throw std::logic_error("Fact rewind parent snapshot missing or contradictory");
    for (const auto& c : forward->changes)
        if (view.GetPgnFact(c.fact) != c.after) throw std::logic_error("Fact rewind current claim differs from retained forward");
    return FactHistoryStep{FactDirection::Rewind,*forward};
}
} // namespace pgn::core

// Copyright (c) 2026 The PGN developers. MIT. RPC projection only.
#pragma once
#include <chain.h>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
namespace pgn::core {
struct LeaderboardUTCWindow {
    std::optional<int64_t> utc_start; // absent = existing Total semantics
    static LeaderboardUTCWindow FromStart(int64_t start){
        if(start<0||start%86400||start>std::numeric_limits<int64_t>::max()-86400)
            throw std::invalid_argument("Canonical UTC midnight required");
        return {start};
    }
    static LeaderboardUTCWindow TodayAt(int64_t now){
        if(now<0)throw std::invalid_argument("UTC request clock unavailable");
        return FromStart(now-now%86400);
    }
    bool Contains(int64_t timestamp)const{return !utc_start||(timestamp>=*utc_start&&timestamp<*utc_start+86400);}
    bool operator==(const LeaderboardUTCWindow&)const=default;
};
inline int64_t LeaderboardEventTime(const CBlockIndex& tip,int32_t height){
    if(height<0||height>tip.nHeight)throw std::runtime_error("Native event date unavailable");
    const auto* block=tip.GetAncestor(height);
    if(!block||!block->phashBlock||!block->IsValid(BLOCK_VALID_SCRIPTS))
        throw std::runtime_error("Native event date provenance unavailable");
    return block->GetBlockTime(); // canonical header clock, never physical runtime
}
} // namespace pgn::core

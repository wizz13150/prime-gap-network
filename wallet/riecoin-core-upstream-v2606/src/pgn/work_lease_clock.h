// Copyright (c) 2026 The PGN developers. MIT.
#pragma once
#include <cstdint>
#include <limits>
#include <optional>
namespace pgn::core {
// The clock is part of a work contract. A height lease must never silently
// become a time lease when software or network throughput changes.
struct WorkLease {
    enum class Clock { LedgerHeight, MedianChainTime };
    Clock clock{Clock::MedianChainTime};
    std::int64_t issued{},deadline{};
    bool expired(std::int64_t height,std::int64_t median_time)const {
        const auto now=clock==Clock::LedgerHeight?height:median_time;
        return (clock!=Clock::LedgerHeight&&clock!=Clock::MedianChainTime)||
            issued<0||deadline<=issued||now<0||now>=deadline;
    }
};
inline std::optional<WorkLease> workLease(WorkLease::Clock clock,std::int64_t issued,std::int64_t duration){
    if((clock!=WorkLease::Clock::LedgerHeight&&clock!=WorkLease::Clock::MedianChainTime)||
       issued<0||duration<1||issued>std::numeric_limits<std::int64_t>::max()-duration)return {};
    return WorkLease{clock,issued,issued+duration};
}
inline std::optional<WorkLease> workTimeLease(std::int64_t issued,std::int64_t seconds){
    return workLease(WorkLease::Clock::MedianChainTime,issued,seconds);
}
}

// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <pgn/issuance_chainstate.h>
#include <map>
#include <vector>

namespace pgn::core {
/** Local provisional metadata, NOT block/undo/consensus format or authenticity. */
enum class IssuanceDirection : uint8_t { Forward = 1, Rewind = 2 };
struct IssuanceHistoryStep {
    IssuanceDirection direction{};
    IssuanceCommit forward;
    bool Valid() const
    {
        return (direction == IssuanceDirection::Forward || direction == IssuanceDirection::Rewind) &&
            forward.Valid() && forward.before && !forward.IsIdentity();
    }
    std::optional<IssuanceCommit> Applied() const
    {
        if (!Valid()) return {};
        return direction == IssuanceDirection::Forward ? std::optional{forward}
            : InvertIssuanceCommit(forward, forward.after);
    }
    bool operator==(const IssuanceHistoryStep&) const = default;
    template <typename Stream> void Serialize(Stream& stream) const
    {
        if (!Valid()) throw std::ios_base::failure("Invalid local issuance history step");
        stream << uint8_t{1} << static_cast<uint8_t>(direction) << forward;
    }
    template <typename Stream> void Unserialize(Stream& stream)
    {
        uint8_t version{}, code{};
        IssuanceHistoryStep next;
        stream >> version >> code >> next.forward;
        if (code == 1) next.direction = IssuanceDirection::Forward;
        else if (code == 2) next.direction = IssuanceDirection::Rewind;
        if (version != 1 || !next.Valid()) throw std::ios_base::failure("Invalid local issuance history step");
        *this = std::move(next);
    }
};
struct IssuanceHistoryTrail {
    std::vector<IssuanceHistoryStep> steps;
    std::optional<IssuanceCommit> Net() const
    {
        if (steps.empty()) return {};
        std::optional<IssuanceSnapshot> first, current;
        std::map<uint256, IssuanceCommit> unique;
        for (const auto& step : steps) {
            const auto applied = step.Applied();
            if (!applied || (current && applied->before != current)) return {};
            const auto [it, inserted] = unique.emplace(step.forward.after.tip, step.forward);
            if (!inserted && it->second != step.forward) return {};
            if (!first) first = applied->before;
            current = applied->after;
        }
        const IssuanceCommit net{first, *current};
        return net.Valid() ? std::optional{net} : std::nullopt;
    }
    bool ValidFor(const IssuanceCommit& net) const { return Net() == net; }
    bool operator==(const IssuanceHistoryTrail&) const = default;
    template <typename Stream> void Serialize(Stream& stream) const
    {
        if (!Net()) throw std::ios_base::failure("Invalid local issuance history trail");
        stream << uint8_t{1} << steps;
    }
    template <typename Stream> void Unserialize(Stream& stream)
    {
        uint8_t version{};
        IssuanceHistoryTrail next;
        stream >> version >> next.steps;
        if (version != 1 || !next.Net()) throw std::ios_base::failure("Invalid local issuance history trail");
        *this = std::move(next);
    }
};
} // namespace pgn::core

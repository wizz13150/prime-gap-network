// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <pgn/fact_chainstate.h>

namespace pgn::core {
// Local storage history, NOT block authenticity or scientific admission.
enum class FactDirection : uint8_t { Forward = 1, Rewind = 2 };
struct FactHistoryStep {
    FactDirection direction{};
    FactCommit forward;
    bool Valid() const {
        return (direction == FactDirection::Forward || direction == FactDirection::Rewind) &&
            forward.Valid() && forward.before && !forward.IsIdentity();
    }
    std::optional<FactCommit> Applied() const {
        if (!Valid()) return {};
        return direction == FactDirection::Forward ? std::optional{forward}
            : InvertFactCommit(forward, forward.after);
    }
    bool operator==(const FactHistoryStep&) const = default;
    template <typename Stream> void Serialize(Stream& s) const {
        if (!Valid()) throw std::ios_base::failure("Invalid fact history step");
        s << uint8_t{1} << static_cast<uint8_t>(direction) << forward;
    }
    template <typename Stream> void Unserialize(Stream& s) {
        uint8_t version{}, code{}; FactHistoryStep next;
        s >> version >> code >> next.forward;
        if (code == 1) next.direction = FactDirection::Forward;
        else if (code == 2) next.direction = FactDirection::Rewind;
        if (version != 1 || !next.Valid()) throw std::ios_base::failure("Invalid fact history step");
        *this = std::move(next);
    }
};
struct FactHistoryTrail {
    std::vector<FactHistoryStep> steps;
    std::optional<FactCommit> Net() const {
        std::optional<FactCommit> net;
        std::map<uint256, FactCommit> unique;
        try {
            for (const auto& step : steps) {
                const auto applied = step.Applied();
                if (!applied) return {};
                const auto [it, inserted] = unique.emplace(step.forward.after.tip, step.forward);
                if (!inserted && it->second != step.forward) return {};
                net = net ? ComposeFactCommits(*net, *applied) : applied;
            }
        } catch (const std::logic_error&) { return {}; }
        return net;
    }
    bool ValidFor(const FactCommit& net) const { return Net() == net; }
    bool operator==(const FactHistoryTrail&) const = default;
    template <typename Stream> void Serialize(Stream& s) const {
        if (!Net()) throw std::ios_base::failure("Invalid fact history trail");
        s << uint8_t{1} << steps;
    }
    template <typename Stream> void Unserialize(Stream& s) {
        uint8_t version{}; FactHistoryTrail next;
        s >> version >> next.steps;
        if (version != 1 || !next.Net()) throw std::ios_base::failure("Invalid fact history trail");
        *this = std::move(next);
    }
};

// Unlike a scalar budget, head/tip equality does not commit the whole key map.
// Check even keys whose mutations CANCEL in Net(), against the actual base.
// Lookup must expose the committed pre-transition claim, including on replay.
template <typename Lookup>
void CheckFactTrailValues(const FactHistoryTrail& trail, Lookup&& lookup)
{
    std::map<uint256, std::optional<FactClaim>> overlay;
    for (const auto& step : trail.steps) {
        const auto applied = step.Applied();
        if (!applied) throw std::logic_error("Invalid fact history application");
        for (const auto& c : applied->changes) {
            auto it = overlay.find(c.fact);
            if (it == overlay.end()) it = overlay.emplace(c.fact, lookup(c.fact)).first;
            if (it->second != c.before) throw std::logic_error("Fact history intermediate prior claim mismatch");
            it->second = c.after;
        }
    }
}
} // namespace pgn::core

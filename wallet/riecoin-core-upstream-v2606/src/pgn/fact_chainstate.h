// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <serialize.h>
#include <uint256.h>
#include <ios>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pgn::core {
// Native local storage transport ONLY. The caller must derive the GLOBAL fact
// key from verified mathematics, not an owner/ticket/work alias, and authenticate
// block admission and undo. These structures grant no reward or eligibility.
struct FactClaim {
    uint256 work, block;
    bool Valid() const { return !work.IsNull() && !block.IsNull(); }
    bool operator==(const FactClaim&) const = default;
    SERIALIZE_METHODS(FactClaim, obj) {
        READWRITE(obj.work, obj.block);
        if (!obj.Valid()) throw std::ios_base::failure("Invalid PGN fact claim");
    }
};
struct FactHead {
    uint256 chain, tip;
    bool Valid() const { return !chain.IsNull() && !tip.IsNull(); }
    bool operator==(const FactHead&) const = default;
    SERIALIZE_METHODS(FactHead, obj) {
        READWRITE(obj.chain, obj.tip);
        if (!obj.Valid()) throw std::ios_base::failure("Invalid PGN fact head");
    }
};
struct FactChange {
    uint256 fact;
    std::optional<FactClaim> before, after;
    bool Valid() const {
        return !fact.IsNull() && before != after &&
            (!before || before->Valid()) && (!after || after->Valid());
    }
    bool operator==(const FactChange&) const = default;
    template <typename Stream> void Serialize(Stream& s) const {
        if (!Valid()) throw std::ios_base::failure("Invalid PGN fact delta");
        s << fact << uint8_t(before.has_value()); if (before) s << *before;
        s << uint8_t(after.has_value()); if (after) s << *after;
    }
    template <typename Stream> void Unserialize(Stream& s) {
        FactChange next; uint8_t present{};
        s >> next.fact >> present;
        if (present > 1) throw std::ios_base::failure("Invalid PGN fact presence");
        if (present) { FactClaim v; s >> v; next.before = v; }
        s >> present;
        if (present > 1) throw std::ios_base::failure("Invalid PGN fact presence");
        if (present) { FactClaim v; s >> v; next.after = v; }
        if (!next.Valid()) throw std::ios_base::failure("Invalid PGN fact delta");
        *this = std::move(next);
    }
};
struct FactCommit {
    std::optional<FactHead> before;
    FactHead after;
    // Strictly ordered unique GLOBAL keys. No per-owner namespace.
    std::vector<FactChange> changes;
    uint256 OldTip() const { return before ? before->tip : uint256{}; }
    bool IsIdentity() const { return before && *before == after && changes.empty(); }
    bool Valid() const {
        if (!after.Valid() || (before && (!before->Valid() || before->chain != after.chain))) return false;
        if (OldTip() == after.tip && !IsIdentity()) return false;
        std::optional<uint256> previous;
        for (const auto& c : changes) {
            if (!c.Valid() || (previous && !(*previous < c.fact)) || (!before && c.before)) return false;
            previous = c.fact;
        }
        return true;
    }
    bool operator==(const FactCommit&) const = default;
    template <typename Stream> void Serialize(Stream& s) const {
        if (!Valid()) throw std::ios_base::failure("Invalid PGN fact commit");
        s << uint8_t{1} << uint8_t(before.has_value()); if (before) s << *before;
        s << after << changes;
    }
    template <typename Stream> void Unserialize(Stream& s) {
        FactCommit next; uint8_t version{}, present{};
        s >> version >> present;
        if (version != 1 || present > 1) throw std::ios_base::failure("Invalid PGN fact format");
        if (present) { FactHead v; s >> v; next.before = v; }
        s >> next.after >> next.changes;
        if (!next.Valid()) throw std::ios_base::failure("Invalid PGN fact commit");
        *this = std::move(next);
    }
};
inline FactCommit ComposeFactCommits(const FactCommit& first, const FactCommit& next)
{
    if (!first.Valid() || !next.Valid() || next.before != std::optional{first.after})
        throw std::logic_error("PGN fact composition head mismatch");
    std::map<uint256, FactChange> net;
    for (const auto& c : first.changes) net.emplace(c.fact, c);
    for (const auto& c : next.changes) {
        auto [it, inserted] = net.emplace(c.fact, c);
        if (!inserted) {
            if (it->second.after != c.before) throw std::logic_error("PGN fact composition value mismatch");
            it->second.after = c.after;
            if (it->second.before == it->second.after) net.erase(it);
        }
    }
    FactCommit result{first.before, next.after, {}};
    for (const auto& [key, value] : net) result.changes.push_back(value);
    if (!result.Valid()) throw std::logic_error("PGN fact composition failed");
    return result;
}
// Algebraic inverse of retained full forward data, NOT authentication of undo.
inline std::optional<FactCommit> InvertFactCommit(const FactCommit& forward, const FactHead& current)
{
    if (!forward.Valid() || !forward.before || forward.after != current) return {};
    FactCommit inverse{forward.after, *forward.before, {}};
    for (const auto& c : forward.changes) inverse.changes.push_back({c.fact, c.after, c.before});
    return inverse.Valid() ? std::optional{inverse} : std::nullopt;
}
} // namespace pgn::core

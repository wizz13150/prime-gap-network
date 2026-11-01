// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <pgn/issuance_budget.h>
#include <serialize.h>
#include <uint256.h>
#include <ios>
#include <optional>

namespace pgn::core {
/** Total-budget storage snapshot, NOT authority to issue coins. Its caller must
 * derive transitions from validated blocks and real undo records. In particular
 * a valid tuple is not proof of subsidy/award/reward ownership or authenticity.
 * The approved total cap is fixed here; no ordinary/scientific split is chosen. */
struct IssuanceBudgetPartition {
    uint256 policy;CAmount ordinary_cap{},ordinary_minted{},ordinary_promised{},exceptional_minted{},exceptional_promised{};
    bool Valid(CAmount minted,CAmount promised)const{
        if(policy.IsNull()||ordinary_cap<0||ordinary_cap>PGN_MAX_SUPPLY_ATOMIC||
           !IssuanceBudgetState::Create(ordinary_cap,ordinary_minted,ordinary_promised)||
           !IssuanceBudgetState::Create(PGN_MAX_SUPPLY_ATOMIC-ordinary_cap,exceptional_minted,exceptional_promised))return false;
        return ordinary_minted+exceptional_minted==minted&&ordinary_promised+exceptional_promised==promised;
    }
    bool operator==(const IssuanceBudgetPartition&)const=default;
    SERIALIZE_METHODS(IssuanceBudgetPartition,obj){READWRITE(obj.policy,obj.ordinary_cap,obj.ordinary_minted,obj.ordinary_promised,obj.exceptional_minted,obj.exceptional_promised);}
};

struct OrdinaryRewardPools {
    CAmount near_miss{},audit{};
    bool Valid(CAmount ordinary_promised) const {
        return near_miss>=0&&audit>=0&&near_miss<=PGN_MAX_SUPPLY_ATOMIC&&audit<=PGN_MAX_SUPPLY_ATOMIC&&
            near_miss+audit==ordinary_promised;
    }
    bool operator==(const OrdinaryRewardPools&)const=default;
    SERIALIZE_METHODS(OrdinaryRewardPools,obj){READWRITE(obj.near_miss,obj.audit);}
};

struct GoldEpochBalances {
    uint64_t golds{};
    int32_t last_gold_height{},audit_paid_height{};
    CAmount silver{},bronze{},red{};
    bool Valid(CAmount near_pool)const {
        return last_gold_height>=0&&audit_paid_height>=0&&audit_paid_height<=last_gold_height&&
            silver>=0&&bronze>=0&&red>=0&&silver<=PGN_MAX_SUPPLY_ATOMIC&&bronze<=PGN_MAX_SUPPLY_ATOMIC&&red<=PGN_MAX_SUPPLY_ATOMIC&&
            silver+bronze+red==near_pool;
    }
    bool operator==(const GoldEpochBalances&)const=default;
    SERIALIZE_METHODS(GoldEpochBalances,obj){READWRITE(obj.golds,obj.last_gold_height,obj.audit_paid_height,obj.silver,obj.bronze,obj.red);}
};

struct IssuanceSnapshot {
    uint256 tip;
    CAmount cap{};
    CAmount minted{};
    CAmount promised{};
    // Optional/versioned: old snapshots retain ABSENCE, never inferred zero.
    // Matching this policy to authenticated genesis remains admission's duty.
    std::optional<IssuanceBudgetPartition> partition{};
    std::optional<OrdinaryRewardPools> pools{}; // Explicit v3 only; absence is never inferred zero.
    std::optional<GoldEpochBalances> epochs{}; // Explicit v4; authenticated Gold counters and separate tier obligations.

    bool Valid() const
    {
        return !tip.IsNull() && cap == PGN_MAX_SUPPLY_ATOMIC &&
            IssuanceBudgetState::Create(cap, minted, promised).has_value()&&(!partition||partition->Valid(minted,promised))&&
            (!pools||(partition&&pools->Valid(partition->ordinary_promised)))&&
            (!epochs||(pools&&epochs->Valid(pools->near_miss)));
    }
    bool operator==(const IssuanceSnapshot&) const = default;
    template <typename Stream> void Serialize(Stream& stream) const
    {
        if (!Valid()) throw std::ios_base::failure("Invalid PGN issuance snapshot");
        stream << uint8_t(epochs?4:pools?3:partition?2:1) << tip << cap << minted << promised;
        if(partition)stream<<*partition;
        if(pools)stream<<*pools;
        if(epochs)stream<<*epochs;
    }
    template <typename Stream> void Unserialize(Stream& stream)
    {
        uint8_t version{};
        IssuanceSnapshot next;
        stream >> version >> next.tip >> next.cap >> next.minted >> next.promised;
        if(version==2||version==3||version==4){IssuanceBudgetPartition value;stream>>value;next.partition=value;}
        if(version==3||version==4){OrdinaryRewardPools value;stream>>value;next.pools=value;}
        if(version==4){GoldEpochBalances value;stream>>value;next.epochs=value;}
        if ((version != 1&&version != 2&&version != 3&&version != 4) || !next.Valid())
            throw std::ios_base::failure("Invalid PGN issuance snapshot");
        *this = next;
    }
};

/** Compare-and-publish chainstate payload. An absent before is ONLY a fresh
 * database initialization, not an inferred zero for missing legacy metadata.
 * Reorganizations may restore a prior snapshot; this object does not prove that
 * the requested rollback is authorized. Persist with the native HEAD marker so
 * a partial UTXO flush cannot lose the exact intended ledger transition. */
struct IssuanceCommit {
    std::optional<IssuanceSnapshot> before;
    IssuanceSnapshot after;

    uint256 OldTip() const { return before ? before->tip : uint256{}; }
    bool IsIdentity() const { return before && *before == after; }
    bool Valid() const
    {
        return after.Valid() && (!before || (before->Valid() && before->cap == after.cap)) &&
            (OldTip() != after.tip || IsIdentity());
    }
    bool operator==(const IssuanceCommit&) const = default;
    template <typename Stream> void Serialize(Stream& stream) const
    {
        if (!Valid()) throw std::ios_base::failure("Invalid PGN issuance commit");
        stream << uint8_t{1} << uint8_t(before.has_value());
        if (before) stream << *before;
        stream << after;
    }
    template <typename Stream> void Unserialize(Stream& stream)
    {
        uint8_t version{}, present{};
        IssuanceCommit next;
        stream >> version >> present;
        if (version != 1 || present > 1)
            throw std::ios_base::failure("Invalid PGN issuance commit prefix");
        if (present) {
            IssuanceSnapshot prior;
            stream >> prior;
            next.before = prior;
        }
        stream >> next.after;
        if (!next.Valid()) throw std::ios_base::failure("Invalid PGN issuance commit");
        *this = next;
    }
};

/** Pure inverse of an explicitly retained transition, NOT undo authentication.
 * Requires a complete prior snapshot and the exact current after snapshot.
 * In particular, legacy absence/fresh enrollment cannot be inverted into an
 * invented zero budget. No value is reconstructed from UTXO or subtracted.
 * A caller must authenticate/persist the retained forward transition and check
 * this result BEFORE advancing the tip or modifying coins. */
inline std::optional<IssuanceCommit> InvertIssuanceCommit(
    const IssuanceCommit& forward, const std::optional<IssuanceSnapshot>& current)
{
    if (!current || !current->Valid() || !forward.before || !forward.Valid() ||
        *current != forward.after) return {};
    const IssuanceCommit inverse{*current, *forward.before};
    return inverse.Valid() ? std::optional{inverse} : std::nullopt;
}
} // namespace pgn::core

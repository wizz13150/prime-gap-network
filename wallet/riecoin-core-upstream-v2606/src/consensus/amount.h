// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Copyright (c) 2013-present The Riecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_AMOUNT_H
#define BITCOIN_CONSENSUS_AMOUNT_H

#include <cstdint>
#include <optional>
#include <pgn/currency_constants.h>

/** PGN atomic amount (signed for deltas; not a balance/provenance claim). */
typedef int64_t CAmount;

/** Approved immutable denomination:10decimal places per PGN. */
static constexpr CAmount COIN = pgn::core::PGN_ATOMIC_UNITS_PER_COIN;

/** Approved per-amount upper bound derived from100M PGN maximum supply.
 * This alone does NOT implement cumulative issuance, subsidy/distribution,
 * select parameters or approve the rest of the network consensus profile. */
static constexpr CAmount MAX_MONEY = pgn::core::PGN_MAX_SUPPLY_ATOMIC;
inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

namespace Consensus {
/** Validation domain in atomic units, independent of display precision and
 * wallet/RPC metadata. Construction is not approval of a PGN economy. */
class AmountRules {
    CAmount m_max_atomic;
    explicit constexpr AmountRules(CAmount max_atomic) : m_max_atomic{max_atomic} {}

public:
    [[nodiscard]] static constexpr std::optional<AmountRules> FromMaxAtomic(CAmount max_atomic)
    {
        if (max_atomic <= 0) return std::nullopt;
        return AmountRules{max_atomic};
    }
    /** Only for inherited standalone Riecoin compatibility checks. Never the
     * fallback for an absent PGN consensus domain. */
    [[nodiscard]] static constexpr AmountRules RiecoinReference() { return AmountRules{pgn::reference::RIC_MAX_ATOMIC}; }
    /** Pure approved PGN monetary bound, NOT chain identity/issuance approval. */
    [[nodiscard]] static constexpr AmountRules ApprovedPgn() { return AmountRules{pgn::core::PGN_MAX_SUPPLY_ATOMIC}; }
    [[nodiscard]] constexpr CAmount MaxAtomic() const { return m_max_atomic; }
    [[nodiscard]] constexpr bool Contains(CAmount value) const { return value >= 0 && value <= m_max_atomic; }
    [[nodiscard]] constexpr bool CanAdd(CAmount total, CAmount value) const
    {
        return Contains(total) && Contains(value) && value <= m_max_atomic - total;
    }
};
} // namespace Consensus

#endif // BITCOIN_CONSENSUS_AMOUNT_H

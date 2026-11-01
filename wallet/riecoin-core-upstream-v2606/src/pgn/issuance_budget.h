// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <consensus/amount.h>
#include <optional>

namespace pgn::core {
/** Exact budget value object. Not an issuance curve, consensus approval,
 * authenticated chainstate or replay/award-identity registry. The caller must
 * atomically persist authorized transitions with its real chain/undo records.
 * Cumulative minted is NEVER unspent balance: burning coins cannot refill it.
 * A subbudget may explicitly be zero; absence is represented by no state. */
class IssuanceBudgetState {
    CAmount m_cap;
    CAmount m_minted;
    CAmount m_promised;
    constexpr IssuanceBudgetState(CAmount cap, CAmount minted, CAmount promised)
        : m_cap(cap), m_minted(minted), m_promised(promised) {}

public:
    [[nodiscard]] static constexpr std::optional<IssuanceBudgetState> Create(
        CAmount cap, CAmount cumulative_minted, CAmount promised)
    {
        if (cap < 0 || cap > PGN_MAX_SUPPLY_ATOMIC || cumulative_minted < 0 ||
            cumulative_minted > cap || promised < 0 || promised > cap - cumulative_minted) return {};
        return IssuanceBudgetState{cap, cumulative_minted, promised};
    }
    constexpr CAmount Cap() const { return m_cap; }
    constexpr CAmount CumulativeMinted() const { return m_minted; }
    constexpr CAmount Promised() const { return m_promised; }
    constexpr CAmount Unallocated() const { return m_cap - m_minted - m_promised; }

    [[nodiscard]] constexpr std::optional<IssuanceBudgetState> Reserve(CAmount amount) const
    {
        if (amount < 0 || amount > Unallocated()) return {};
        return IssuanceBudgetState{m_cap, m_minted, m_promised + amount};
    }
    [[nodiscard]] constexpr std::optional<IssuanceBudgetState> Release(CAmount amount) const
    {
        if (amount < 0 || amount > m_promised) return {};
        return IssuanceBudgetState{m_cap, m_minted, m_promised - amount};
    }
    [[nodiscard]] constexpr std::optional<IssuanceBudgetState> MintReserved(CAmount amount) const
    {
        if (amount < 0 || amount > m_promised) return {};
        // Admission establishes minted+amount <= minted+promised <= cap.
        return IssuanceBudgetState{m_cap, m_minted + amount, m_promised - amount};
    }
    [[nodiscard]] constexpr std::optional<IssuanceBudgetState> MintUnreserved(CAmount amount) const
    {
        if (amount < 0 || amount > Unallocated()) return {};
        return IssuanceBudgetState{m_cap, m_minted + amount, m_promised};
    }
};
} // namespace pgn::core

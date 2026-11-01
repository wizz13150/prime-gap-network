// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <consensus/amount.h>
#include <pgn/balance_arithmetic.h>

namespace pgn::core {
/** A local wallet read requires its explicit immutable consensus domain.
 * Missing rules are unavailable, NEVER inherited RIC rules or economic policy.
 * The typed errors describe local admission failure here, not a
 * peer-invalidity verdict. Existing tryGetBalances handles this typed failure
 * without overwriting a prior complete observation. */
inline Consensus::AmountRules RequireWalletDomain(std::optional<Consensus::AmountRules> rules)
{
    if (!rules) throw WalletBalanceError{BalanceArithmeticError::MissingDomain};
    return *rules;
}

inline CAmount CheckedWalletDomainCredit(const Consensus::AmountRules& rules,
                                        CAmount total, CAmount credit)
{
    if (credit < 0) throw WalletBalanceError{BalanceArithmeticError::NegativeCredit};
    if (!rules.Contains(total)) throw WalletBalanceError{BalanceArithmeticError::InvalidBucket};
    if (!rules.CanAdd(total, credit))
        throw WalletBalanceError{BalanceArithmeticError::DomainExceeded};
    return total + credit;
}
} // namespace pgn::core

// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <consensus/amount.h>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace pgn::core {
enum class BalanceArithmeticError {
    NegativeCredit, InvalidBucket, InvalidNonmempoolDelta, CreditOverflow,
    NonmempoolUnderflow, AliasedBuckets, MissingDomain, DomainExceeded,
    SelectedInputsUnavailable
};
/** Local arithmetic/read failure, not an invalid transaction or consensus
 * decision. Synchronous balance callers must propagate it, never return zero. */
class WalletBalanceError : public std::runtime_error {
public:
    explicit WalletBalanceError(BalanceArithmeticError error)
        : std::runtime_error(error == BalanceArithmeticError::MissingDomain ?
              "wallet monetary domain unavailable" :
              error == BalanceArithmeticError::DomainExceeded ?
              "wallet transaction amount outside configured domain" :
              error == BalanceArithmeticError::SelectedInputsUnavailable ?
              "wallet selected inputs unavailable" :
              "wallet balance arithmetic unavailable"), m_error(error) {}
    BalanceArithmeticError Error() const noexcept { return m_error; }
private:
    BalanceArithmeticError m_error;
};

/** Representability only, NOT a money/supply/consensus ceiling. */
inline CAmount CheckedWalletCredit(CAmount bucket, CAmount credit)
{
    if (credit < 0) throw WalletBalanceError{BalanceArithmeticError::NegativeCredit};
    if (bucket < 0) throw WalletBalanceError{BalanceArithmeticError::InvalidBucket};
    if (credit > std::numeric_limits<CAmount>::max() - bucket)
        throw WalletBalanceError{BalanceArithmeticError::CreditOverflow};
    return bucket + credit;
}

/** The nonmempool field is a legitimately NONPOSITIVE delta, not a balance.
 * Validate both operations before publishing either. No negation of MIN and
 * no partial bucket update when subtraction is unrepresentable. */
inline void AccumulateWalletCredit(CAmount& bucket, CAmount& nonmempool,
                                   CAmount credit, bool spent_nonmempool)
{
    if (&bucket == &nonmempool)
        throw WalletBalanceError{BalanceArithmeticError::AliasedBuckets};
    const auto next_bucket = CheckedWalletCredit(bucket, credit);
    if (nonmempool > 0)
        throw WalletBalanceError{BalanceArithmeticError::InvalidNonmempoolDelta};
    auto next_nonmempool = nonmempool;
    if (spent_nonmempool) {
        if (nonmempool < std::numeric_limits<CAmount>::min() + credit)
            throw WalletBalanceError{BalanceArithmeticError::NonmempoolUnderflow};
        next_nonmempool = nonmempool - credit;
    }
    bucket = next_bucket;
    nonmempool = next_nonmempool;
}

/** Called only after the real wallet lock succeeds. The reader returns a
 * complete pair; false preserves BOTH old outputs. Non-arithmetic exceptions
 * are not swallowed. DTO/hash assignments must themselves be nonthrowing. */
template <typename Read, typename Balances, typename Hash>
bool TryPublishWalletBalance(Read&& read, Balances& balances, Hash& block_hash)
{
    static_assert(std::is_nothrow_copy_assignable_v<Balances>);
    static_assert(std::is_nothrow_copy_assignable_v<Hash>);
    try {
        const auto observation = std::forward<Read>(read)();
        balances = observation.first;
        block_hash = observation.second;
        return true;
    } catch (const WalletBalanceError&) {
        return false;
    }
}
} // namespace pgn::core

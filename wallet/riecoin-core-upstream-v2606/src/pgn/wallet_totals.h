// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <pgn/balance_arithmetic.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace pgn::core {
/** Absence is not a zero-valued coin set or an insufficient-funds observation. */
inline CAmount RequireWalletAmount(std::optional<CAmount> amount)
{
    if (!amount) throw std::runtime_error{"wallet amount unavailable"};
    return *amount;
}

inline CAmount CheckedWalletDifference(CAmount left, CAmount right)
{
    if ((right > 0 && left < std::numeric_limits<CAmount>::min() + right) ||
        (right < 0 && left > std::numeric_limits<CAmount>::max() + right))
        throw std::overflow_error{"wallet amount difference unavailable"};
    return left - right;
}

/** An ordered partial CAmount sum may overflow even when the exact final signed
 * sum fits (fees/deltas can cancel). Accumulate in a fixed wider representation
 * and check ONLY before conversion. With <=UINT64_MAX CAmount terms and one
 * CAmount adjustment, magnitude<2^127; Boost checked signed128 is sufficient.
 * This is wallet arithmetic only, not supply, consensus or mining. */
template <typename Range, typename Value>
std::optional<CAmount> ExactWalletSignedTotal(const Range& values, Value value,
                                             CAmount adjustment = 0)
{
    static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));
    static_assert(std::is_unsigned_v<decltype(values.size())>);
    static_assert(std::numeric_limits<decltype(values.size())>::digits <= 64);
    boost::multiprecision::checked_int128_t total{adjustment};
    for (const auto& item : values) {
        const auto term = value(item);
        static_assert(std::is_same_v<std::remove_cv_t<decltype(term)>, CAmount>);
        total += term;
    }
    if (total < std::numeric_limits<CAmount>::min() ||
        total > std::numeric_limits<CAmount>::max()) return std::nullopt;
    return total.template convert_to<CAmount>();
}

/** Raw UTXO amounts must be nonnegative. No speculative negative credit may
 * cancel an invalid positive aggregate. Refuse before any CAmount addition. */
template <typename Range, typename Value>
CAmount ExactWalletCreditTotal(const Range& values, Value value)
{
    CAmount total{0};
    for (const auto& item : values) {
        const auto term = value(item);
        static_assert(std::is_same_v<std::remove_cv_t<decltype(term)>, CAmount>);
        total = CheckedWalletCredit(total, term);
    }
    return total;
}
} // namespace pgn::core

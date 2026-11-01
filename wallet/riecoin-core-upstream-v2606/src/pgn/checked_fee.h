// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <consensus/amount.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace pgn::core {
enum class CheckedFeeError { None, InvalidRate, InvalidSize, InvalidBump, InvalidDiscount, InvalidCeiling, CeilingExceeded };
struct CheckedFeeResult {
    CheckedFeeError error;
    std::optional<CAmount> fee;
};

// Consume the discount BEFORE accumulating positive contributions. The relation
// sum(values)-discount is unchanged, but no overflowing gross sum is created.
// Net prefixes are monotone; a rejected prefix proves the final net exceeds cap.
template<class Range, class ValueOf>
CheckedFeeResult CheckedDiscountedFeeTotal(const Range& values, ValueOf value_of,
                                         CAmount discount, CAmount ceiling)
{
    const auto fail = [](CheckedFeeError error) { return CheckedFeeResult{error, std::nullopt}; };
    if (discount < 0) return fail(CheckedFeeError::InvalidDiscount);
    if (ceiling < 0) return fail(CheckedFeeError::InvalidCeiling);
    CAmount total{}, remaining_discount = discount;
    for (const auto& item : values) {
        const CAmount value = value_of(item);
        if (value < 0) return fail(CheckedFeeError::InvalidBump);
        const CAmount cancelled = std::min(value, remaining_discount);
        remaining_discount -= cancelled;
        const CAmount contribution = value - cancelled;
        if (contribution > ceiling - total) return fail(CheckedFeeError::CeilingExceeded);
        total += contribution;
    }
    if (remaining_discount) return fail(CheckedFeeError::InvalidDiscount);
    return {CheckedFeeError::None, total};
}

// Exact ceil(numerator*vsize/denominator)+bump <= ceiling. Never call FeeFrac
// division/rounding or signed addition before establishing representability.
// Quotient/remainder decomposition avoids any wide-integer/float dependency.
inline CheckedFeeResult CheckedFeeForSizeAndBump(CAmount numerator, int32_t denominator,
                                               int32_t vsize, CAmount bump, CAmount ceiling)
{
    const auto fail = [](CheckedFeeError error) { return CheckedFeeResult{error, std::nullopt}; };
    if (numerator < 0 || denominator <= 0) return fail(CheckedFeeError::InvalidRate);
    if (vsize < 0) return fail(CheckedFeeError::InvalidSize);
    if (bump < 0) return fail(CheckedFeeError::InvalidBump);
    if (ceiling < 0) return fail(CheckedFeeError::InvalidCeiling);
    if (bump > ceiling) return fail(CheckedFeeError::CeilingExceeded);
    const uint64_t remaining = static_cast<uint64_t>(ceiling - bump);
    const uint64_t size = static_cast<uint32_t>(vsize);
    const uint64_t divisor = static_cast<uint32_t>(denominator);
    const uint64_t quotient = static_cast<uint64_t>(numerator) / divisor;
    const uint64_t remainder = static_cast<uint64_t>(numerator) % divisor;
    if (size && quotient > remaining / size) return fail(CheckedFeeError::CeilingExceeded);
    const uint64_t base = quotient * size; // Prechecked <=remaining<=INT64_MAX.
    // remainder<INT32_MAX and size<=INT32_MAX: product<2^62, portable uint64.
    const uint64_t product = remainder * size;
    const uint64_t tail = product / divisor + (product % divisor != 0);
    if (tail > remaining - base) return fail(CheckedFeeError::CeilingExceeded);
    return {CheckedFeeError::None, static_cast<CAmount>(base + tail + static_cast<uint64_t>(bump))};
}

// Arithmetic-only admission limit for a naked CFeeRate(R/1000) over its whole
// GetFee(int32>=0) domain. NOT an economic rate cap or assumed transaction size.
inline constexpr CAmount MaxAtomicRateForInt32FeeSizes()
{
    constexpr CAmount limit = std::numeric_limits<CAmount>::max();
    constexpr CAmount size = std::numeric_limits<int32_t>::max();
    return (limit / size) * 1000 + ((limit % size) * 1000) / size;
}
} // namespace pgn::core

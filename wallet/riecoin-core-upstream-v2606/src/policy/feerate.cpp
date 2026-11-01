// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <policy/feerate.h>
#include <util/moneystr.h>

#include <limits>
#include <stdexcept>


CFeeRate::CFeeRate(const CAmount& nFeePaid, int32_t virtual_bytes)
{
    if (virtual_bytes > 0) {
        m_feerate = FeePerVSize(nFeePaid, virtual_bytes);
    } else {
        m_feerate = FeePerVSize();
    }
}

CAmount CFeeRate::GetFee(int32_t virtual_bytes) const
{
    Assume(virtual_bytes >= 0);
    if (m_feerate.IsEmpty()) { return CAmount(0);}
    CAmount nFee = CAmount(m_feerate.EvaluateFeeUp(virtual_bytes));
    if (nFee == 0 && virtual_bytes != 0 && m_feerate.fee < 0) return CAmount(-1);
    return nFee;
}

std::string CFeeRate::ToString(FeeRateFormat fee_rate_format) const
{
    // Preserve the established floor-to-atomic/kvB presentation, without
    // invoking FeeFrac's representability precondition on an unchecked rate.
    const CAmount feerate_per_kvb = [&]() -> CAmount {
        const auto ratio = GetFeePerVSize();
        if (ratio.fee == 0 && ratio.size == 0) return 0; // Documented known zero.
        if (ratio.size <= 0) throw std::invalid_argument("Invalid fee-rate denominator");
        const bool negative = ratio.fee < 0;
        const uint64_t magnitude = negative ? uint64_t(-(ratio.fee + 1)) + 1 : uint64_t(ratio.fee);
        const uint64_t divisor = uint32_t(ratio.size);
        const uint64_t limit = uint64_t(std::numeric_limits<CAmount>::max()) + negative;
        const uint64_t quotient = magnitude / divisor;
        if (quotient > limit / 1000) throw std::overflow_error("Fee rate exceeds atomic/kvB representation");
        const uint64_t base = quotient * 1000;
        const uint64_t product = (magnitude % divisor) * 1000; // <2^41.
        const uint64_t tail = product / divisor + (negative && product % divisor != 0);
        if (tail > limit - base) throw std::overflow_error("Fee rate exceeds atomic/kvB representation");
        const uint64_t total = base + tail;
        if (negative && total == limit) return std::numeric_limits<CAmount>::min();
        return negative ? -CAmount(total) : CAmount(total);
    }();
    switch (fee_rate_format) {
    case FeeRateFormat::BTC_KVB:
        return FormatMoneyFixed(feerate_per_kvb) + " " + CURRENCY_UNIT + "/kvB";
    case FeeRateFormat::SAT_VB: {
        const bool negative = feerate_per_kvb < 0;
        const uint64_t magnitude = negative ? uint64_t(-(feerate_per_kvb + 1)) + 1 : uint64_t(feerate_per_kvb);
        std::string fraction = std::to_string(magnitude % 1000);
        fraction.insert(0, 3 - fraction.size(), '0');
        return (negative ? "-" : "") + std::to_string(magnitude / 1000) + "." + fraction + " " + CURRENCY_ATOM + "/vB";
    }
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

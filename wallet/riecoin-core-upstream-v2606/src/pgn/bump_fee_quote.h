// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <pgn/fee_rate_admission.h>
#include <algorithm>

namespace pgn::core {
struct BumpFeeQuote {
    AtomicFeeAdmissionError error;
    std::optional<CAmount> total;
    std::optional<CAmount> minimum;
    std::optional<CAmount> incremental;
};

/** Exact rational rate quotes plus already-absolute bump/old fees. No additions
 * before admission, checked signed-to-size conversion and amount-domain bounds.
 * A failed quote publishes none of its intermediate values. */
inline BumpFeeQuote QuoteBumpFee(const CFeeRate& replacement, const CFeeRate& incremental,
    int64_t bytes, CAmount old_fee, CAmount combined_bump,
    const std::optional<AtomicFeeRatePolicy>& policy,
    const std::optional<Consensus::AmountRules>& domain)
{
    const auto fail = [](AtomicFeeAdmissionError error) { return BumpFeeQuote{error, {}, {}, {}}; };
    const auto metadata = ValidateAtomicFeeAdmissionPolicy(policy);
    if (metadata != AtomicFeeAdmissionError::None) return fail(metadata);
    if (!domain) return fail(AtomicFeeAdmissionError::MissingAmountDomain);
    if (bytes <= 0 || bytes > std::numeric_limits<int32_t>::max()) return fail(AtomicFeeAdmissionError::InvalidSize);
    if (!domain->Contains(old_fee) || !domain->Contains(combined_bump)) return fail(AtomicFeeAdmissionError::AmountAboveDomain);
    const auto base = QuoteAtomicFee(replacement, static_cast<uint32_t>(bytes), policy, domain);
    if (base.error != AtomicFeeAdmissionError::None) return fail(base.error);
    const auto relay = QuoteAtomicFee(incremental, static_cast<uint32_t>(bytes), policy, domain);
    if (relay.error != AtomicFeeAdmissionError::None) return fail(relay.error);
    if (!domain->CanAdd(*base.amount, combined_bump) || !domain->CanAdd(old_fee, *relay.amount))
        return fail(AtomicFeeAdmissionError::AmountAboveDomain);
    return {AtomicFeeAdmissionError::None, *base.amount + combined_bump,
            old_fee + *relay.amount, *relay.amount};
}

struct BumpRateEstimate {
    AtomicFeeAdmissionError error;
    std::optional<CFeeRate> rate;
};

/** Conservative integer-atomic/kvB estimate, not a chosen fee price/policy.
 * floor(old*1000)+1 is strictly above the original exact rational rate.
 * Add ceil(incremental*1000), so a fractional relay rate is never rounded down.
 * Explicit user rates are NOT passed through this estimator/repriced. */
inline BumpRateEstimate EstimateConservativeBumpRate(const CFeeRate& original,
    const CFeeRate& incremental, const CFeeRate& minimum,
    const std::optional<AtomicFeeRatePolicy>& policy)
{
    const auto fail = [](AtomicFeeAdmissionError error) { return BumpRateEstimate{error, {}}; };
    for (const auto* rate : {&original, &incremental, &minimum}) {
        const auto admitted = AdmitAtomicFeeRate(*rate, policy);
        if (admitted != AtomicFeeAdmissionError::None) return fail(admitted);
    }
    const auto ceil_per_k = [&](const CFeeRate& rate) -> CAmount {
        const auto ratio = rate.GetFeePerVSize();
        if (ratio.fee == 0 && ratio.size == 0) return 0; // Documented known zero.
        return *CheckedFeeForSizeAndBump(ratio.fee, ratio.size, 1000, 0,
                                       policy->max_atomic_per_kvbyte).fee;
    };
    const auto ratio = original.GetFeePerVSize();
    const bool fractional = ratio.size > 0 &&
        ((uint64_t(ratio.fee) % uint32_t(ratio.size)) * 1000) % uint32_t(ratio.size) != 0;
    CAmount strict = ceil_per_k(original);
    if (!fractional) {
        if (strict == policy->max_atomic_per_kvbyte) return fail(AtomicFeeAdmissionError::RateAbovePolicy);
        ++strict;
    }
    const CAmount added = ceil_per_k(incremental);
    if (added > policy->max_atomic_per_kvbyte - strict) return fail(AtomicFeeAdmissionError::RateAbovePolicy);
    const CFeeRate candidate{strict + added};
    const auto admitted = AdmitAtomicFeeRate(candidate, policy);
    if (admitted != AtomicFeeAdmissionError::None) return fail(admitted);
    return {AtomicFeeAdmissionError::None, std::max(candidate, minimum)};
}
} // namespace pgn::core

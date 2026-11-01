// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <pgn/checked_fee.h>
#include <pgn/monetary_context.h>
#include <policy/feerate.h>

namespace pgn::core {
enum class AtomicFeeAdmissionError {
    None, MissingPolicy, InvalidPolicy, InvalidRate, RateAbovePolicy,
    UnrepresentableRate, MissingAmountDomain, InvalidSize, AmountAboveDomain
};
inline AtomicFeeAdmissionError ValidateAtomicFeeAdmissionPolicy(
    const std::optional<AtomicFeeRatePolicy>& policy)
{
    if (!policy) return AtomicFeeAdmissionError::MissingPolicy;
    // Three is the atomic/vB to atomic/kvB unit relation, not currency decimals.
    return validateExactAmountPolicy(ExactAmountPolicy{3, policy->max_atomic_per_kvbyte},
                                    policy->text_budget) == ExactAmountError::None
        ? AtomicFeeAdmissionError::None : AtomicFeeAdmissionError::InvalidPolicy;
}

/** Validate a genuine rational CFeeRate WITHOUT rounding/changing it. Never
 * invoke FeeFrac evaluation until its whole public int32 query domain is paid.
 * Rates and per-transaction absolute amount caps are different quantities. */
inline AtomicFeeAdmissionError AdmitAtomicFeeRate(const CFeeRate& rate,
    const std::optional<AtomicFeeRatePolicy>& policy)
{
    const auto metadata = ValidateAtomicFeeAdmissionPolicy(policy);
    if (metadata != AtomicFeeAdmissionError::None) return metadata;
    const auto ratio = rate.GetFeePerVSize(); // Raw numerator/denominator only.
    // CFeeRate documents its default0/0 as zero; absent policy is NOT zero.
    if (ratio.fee == 0 && ratio.size == 0) return AtomicFeeAdmissionError::None;
    if (ratio.fee < 0 || ratio.size <= 0) return AtomicFeeAdmissionError::InvalidRate;
    if (!CheckedFeeForSizeAndBump(ratio.fee, ratio.size, 1000, 0,
                                 policy->max_atomic_per_kvbyte).fee)
        return AtomicFeeAdmissionError::RateAbovePolicy;
    if (!CheckedFeeForSizeAndBump(ratio.fee, ratio.size,
            std::numeric_limits<int32_t>::max(), 0, std::numeric_limits<CAmount>::max()).fee)
        return AtomicFeeAdmissionError::UnrepresentableRate;
    return AtomicFeeAdmissionError::None;
}

struct AtomicFeeQuote {
    AtomicFeeAdmissionError error;
    std::optional<CAmount> amount;
};
inline AtomicFeeQuote QuoteAtomicFee(const CFeeRate& rate, uint32_t bytes,
    const std::optional<AtomicFeeRatePolicy>& policy,
    const std::optional<Consensus::AmountRules>& amount_domain)
{
    const auto admitted = AdmitAtomicFeeRate(rate, policy);
    if (admitted != AtomicFeeAdmissionError::None) return {admitted, {}};
    if (!amount_domain) return {AtomicFeeAdmissionError::MissingAmountDomain, {}};
    if (bytes > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
        return {AtomicFeeAdmissionError::InvalidSize, {}};
    const auto ratio = rate.GetFeePerVSize();
    if (ratio.fee == 0 && ratio.size == 0) return {AtomicFeeAdmissionError::None, CAmount{0}};
    const auto result = CheckedFeeForSizeAndBump(ratio.fee, ratio.size,
        static_cast<int32_t>(bytes), 0, amount_domain->MaxAtomic());
    if (!result.fee) return {AtomicFeeAdmissionError::AmountAboveDomain, {}};
    return {AtomicFeeAdmissionError::None, result.fee};
}
} // namespace pgn::core

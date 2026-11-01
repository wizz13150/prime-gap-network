// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgncurrencyfeerate.h>
#include <qt/pgntransactionamount.h>
#include <pgn/checked_fee.h>
#include <rpc/pgn_amount.h>
#include <univalue.h>
#include <algorithm>
#include <exception>
#include <limits>

namespace pgn::qt {
std::optional<CurrencyFeeRatePreview> PreviewCurrencyFeeRatePolicy(
    const std::optional<core::MonetaryContext>& context,
    const std::optional<core::AtomicFeeRatePolicy>& rate)
{
    if (!context || !rate || core::ValidateMonetaryContext(*context) != core::ExactAmountError::None) return std::nullopt;
    // Three here is1000vB/kvB, never the network currency precision.
    if (core::validateExactAmountPolicy(core::ExactAmountPolicy{3, rate->max_atomic_per_kvbyte}, rate->text_budget) != core::ExactAmountError::None) return std::nullopt;
    const core::ExactAmountTextBudget budget{std::min(context->text_budget.max_characters, rate->text_budget.max_characters)};
    const core::ExactAmountPolicy numeric{context->transfer.decimals,
        std::min(rate->max_atomic_per_kvbyte, core::MaxAtomicRateForInt32FeeSizes())};
    if (budget.max_characters > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
        core::validateExactAmountPolicy(numeric, budget) != core::ExactAmountError::None) return std::nullopt;
    return CurrencyFeeRatePreview{numeric, budget};
}
bool SameCurrencyFeeRateMetadata(const std::optional<core::MonetaryContext>& a,
    const std::optional<core::AtomicFeeRatePolicy>& ar,
    const std::optional<core::MonetaryContext>& b,
    const std::optional<core::AtomicFeeRatePolicy>& br)
{
    return SameTransactionMonetaryContext(a,b) && ar.has_value() == br.has_value() &&
        (!ar || (ar->max_atomic_per_kvbyte == br->max_atomic_per_kvbyte && ar->text_budget.max_characters == br->text_budget.max_characters));
}
CurrencyFeeRateResult AdmitCurrencyFeeRate(const QString& text,
    const std::optional<core::MonetaryContext>& context,
    const std::optional<core::AtomicFeeRatePolicy>& rate_policy)
{
    using S = CurrencyFeeRateStatus;
    const auto preview = PreviewCurrencyFeeRatePolicy(context, rate_policy);
    if (!preview) return {S::Unavailable,{},{}}; // Even malformed text cannot mask bad metadata.
    if (text.isEmpty() || static_cast<uint64_t>(text.size()) > preview->budget.max_characters) return {S::InvalidText,{},{}};
    for (const QChar ch : text) if (ch.unicode() > 127 || ch.isNull()) return {S::InvalidText,{},{}};
    try {
        // This is the sole positive admission authority; no endpoint dispatch,
        // Params/Node/wallet, float, or independently constructed CFeeRate.
        const CFeeRate rate = pgn::rpc::FeeRateFromCurrencyPerKVByte(
            UniValue{UniValue::VSTR, text.toLatin1().toStdString()}, context, rate_policy);
        // Native converter guarantees every int32 query;1000 is exact here.
        return {S::ParsedNative,rate,rate.GetFeePerK()};
    } catch (const UniValue&) {
        return {S::NativeRefusal,{},{}};
    } catch (const std::exception&) {
        return {S::NativeRefusal,{},{}};
    }
}
} // namespace pgn::qt

// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <rpc/pgn_amount.h>
#include <pgn/checked_fee.h>
#include <chainparams.h>
#include <policy/feerate.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <univalue.h>
#include <algorithm>
#include <limits>
#include <utility>

namespace pgn::rpc {
namespace {
[[noreturn]] void ThrowAmountError(core::ExactAmountError error)
{
    switch (error) {
    case core::ExactAmountError::MissingPolicy:
        throw JSONRPCError(RPC_MISC_ERROR, "PGN monetary policy is unavailable");
    case core::ExactAmountError::InvalidPolicy:
    case core::ExactAmountError::InvalidBudget:
    case core::ExactAmountError::InvalidFormat:
        throw JSONRPCError(RPC_MISC_ERROR, "Invalid PGN monetary conversion context");
    case core::ExactAmountError::BudgetExceeded:
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount text budget exceeded");
    case core::ExactAmountError::ExcessPrecision:
        throw JSONRPCError(RPC_TYPE_ERROR, "Amount has excess precision");
    case core::ExactAmountError::CAmountOverflow:
    case core::ExactAmountError::CeilingExceeded:
        throw JSONRPCError(RPC_TYPE_ERROR, "Amount is outside the explicit monetary range");
    case core::ExactAmountError::NegativeTransfer:
    case core::ExactAmountError::InvalidText:
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid unsigned decimal transfer amount");
    case core::ExactAmountError::None:
        break;
    }
    throw JSONRPCError(RPC_MISC_ERROR, "Inconsistent PGN monetary conversion result");
}
void RequireConversionContext(core::ExactAmountError error)
{
    // Validate metadata before inspecting user text. In particular, a budget
    // too small for its configured range is a local configuration failure;
    // only overlong actual input is RPC_INVALID_PARAMETER.
    if (error != core::ExactAmountError::None) {
        throw JSONRPCError(RPC_MISC_ERROR, "PGN decimal conversion context is unavailable or incompatible");
    }
}
} // namespace

core::MonetaryContext RequireSelectedMonetaryContext()
{
    const auto context = SelectedPgnMonetaryContext();
    if (!context) ThrowAmountError(core::ExactAmountError::MissingPolicy);
    const auto error = core::ValidateMonetaryContext(*context);
    RequireConversionContext(error);
    return *context;
}

core::AtomicFeeRatePolicy RequireSelectedAtomicFeeRatePolicy()
{
    const auto policy = SelectedPgnAtomicFeeRatePolicy();
    if (!policy) throw JSONRPCError(RPC_MISC_ERROR, "PGN atomic fee rate policy is unavailable");
    const auto error = core::validateExactAmountPolicy(
        core::ExactAmountPolicy{3, policy->max_atomic_per_kvbyte}, policy->text_budget);
    RequireConversionContext(error);
    return *policy;
}

CFeeRate FeeRateFromAtomicPerVByte(const UniValue& value,
                                 const std::optional<core::AtomicFeeRatePolicy>& policy)
{
    if (!policy) throw JSONRPCError(RPC_MISC_ERROR, "PGN atomic fee rate policy is unavailable");
    //1000 virtualbytes per kvB: three decimal places are this unit relation,
    // never the network currency precision. CFeeRate stores an exact FeeFrac.
    const auto atomic_per_kvbyte = AmountFromValue(value,
        core::ExactAmountPolicy{3, policy->max_atomic_per_kvbyte}, policy->text_budget);
    if (atomic_per_kvbyte > core::MaxAtomicRateForInt32FeeSizes()) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Fee rate exceeds the exact CFeeRate size-domain representation");
    }
    return CFeeRate{atomic_per_kvbyte};
}

CFeeRate FeeRateFromCurrencyPerKVByte(const UniValue& value,
    const std::optional<core::MonetaryContext>& context,
    const std::optional<core::AtomicFeeRatePolicy>& rate_policy)
{
    if (!context) ThrowAmountError(core::ExactAmountError::MissingPolicy);
    const auto context_error = core::ValidateMonetaryContext(*context);
    RequireConversionContext(context_error);
    if (!rate_policy) throw JSONRPCError(RPC_MISC_ERROR, "PGN atomic fee rate policy is unavailable");
    const auto rate_error = core::validateExactAmountPolicy(
        core::ExactAmountPolicy{3, rate_policy->max_atomic_per_kvbyte}, rate_policy->text_budget);
    RequireConversionContext(rate_error);

    // Neither transfer.max_atomic nor display.max_atomic is a rate limit.
    // The additional bound is purely the representability of every GetFee
    // query on a naked CFeeRate, not a new economic/network policy.
    const auto ceiling = std::min(rate_policy->max_atomic_per_kvbyte,
                                  core::MaxAtomicRateForInt32FeeSizes());
    const core::ExactAmountTextBudget budget{
        std::min(context->text_budget.max_characters, rate_policy->text_budget.max_characters)};
    RequireConversionContext(core::validateExactAmountPolicy(
        core::ExactAmountPolicy{context->transfer.decimals, ceiling}, budget));
    const CAmount atomic_per_kvbyte = AmountFromValue(value,
        core::ExactAmountPolicy{context->transfer.decimals, ceiling}, budget);
    return CFeeRate{atomic_per_kvbyte};
}

UniValue ValueFromAtomicRatePerKVByte(CAmount atomic_per_kvbyte,
    AmountJsonEncoding encoding, const std::optional<core::MonetaryContext>& context)
{
    if (!context) ThrowAmountError(core::ExactAmountError::MissingPolicy);
    const auto context_error = core::ValidateMonetaryContext(*context);
    RequireConversionContext(context_error);
    // Unsigned decimal codec reused only for its representation. An observed
    // rate can exceed admission or currency-amount caps and remain reportable.
    return ValueFromAmount(atomic_per_kvbyte, AmountDomain::Transfer, encoding,
        core::ExactAmountPolicy{context->display.decimals, std::numeric_limits<CAmount>::max()},
        context->text_budget);
}

CAmount AmountFromValue(const UniValue& value,
                        const std::optional<core::ExactAmountPolicy>& policy,
                        core::ExactAmountTextBudget budget)
{
    const auto context = core::validateExactAmountPolicy(policy, budget);
    RequireConversionContext(context);
    if (!value.isNum() && !value.isStr()) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Amount must be an explicit number or string");
    }
    const auto result = core::parseExactAmount(value.getValStr(), policy, budget);
    if (result.error != core::ExactAmountError::None || !result.amount) {
        ThrowAmountError(result.error);
    }
    return *result.amount;
}

UniValue ValueFromAmount(CAmount amount, AmountDomain domain,
                        AmountJsonEncoding encoding,
                        const std::optional<core::ExactAmountPolicy>& policy,
                        core::ExactAmountTextBudget budget)
{
    core::ExactAmountFormat format;
    switch (domain) {
    case AmountDomain::Transfer: format = core::ExactAmountFormat::Transfer; break;
    case AmountDomain::Delta: format = core::ExactAmountFormat::Delta; break;
    default: ThrowAmountError(core::ExactAmountError::InvalidFormat);
    }
    UniValue::VType type;
    switch (encoding) {
    case AmountJsonEncoding::Number: type = UniValue::VNUM; break;
    case AmountJsonEncoding::String: type = UniValue::VSTR; break;
    default: ThrowAmountError(core::ExactAmountError::InvalidFormat);
    }
    auto result = core::formatExactAmount(amount, format, policy, budget);
    if (result.error != core::ExactAmountError::None || !result.text) {
        // Output failure describes internal state/context, not bad RPC input.
        throw JSONRPCError(RPC_MISC_ERROR, "PGN amount output cannot be represented in its explicit context");
    }
    return UniValue(type, std::move(*result.text));
}
} // namespace pgn::rpc

// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <pgn/exact_amount.h>

namespace pgn::core {
// Bound to immutable chain parameters, never populated from RPC/CLI or guessed
// from the inherited Riecoin constants. This is NOT a consensus approval token.
// A transfer ceiling need not be the supply or a signed aggregate display cap.
struct MonetaryContext {
    ExactAmountPolicy transfer;
    ExactAmountPolicy display;
    ExactAmountTextBudget text_budget;
};
// Local wallet/RPC rate admission, NOT currency precision/supply/consensus.
// An explicit ceiling in atomic units per1000vbytes avoids applying a monetary
// transfer ceiling to a physically different quantity.
struct AtomicFeeRatePolicy {
    CAmount max_atomic_per_kvbyte;
    ExactAmountTextBudget text_budget;
};
inline ExactAmountError ValidateMonetaryContext(const MonetaryContext& context)
{
    if (context.transfer.decimals != context.display.decimals ||
        context.display.max_atomic < context.transfer.max_atomic) {
        return ExactAmountError::InvalidPolicy;
    }
    const auto input = validateExactAmountPolicy(context.transfer, context.text_budget);
    if (input != ExactAmountError::None) return input;
    return validateExactAmountPolicy(context.display, context.text_budget);
}
} // namespace pgn::core

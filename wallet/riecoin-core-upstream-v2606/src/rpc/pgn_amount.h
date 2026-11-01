// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include <pgn/exact_amount.h>
#include <pgn/monetary_context.h>

class UniValue;
class CFeeRate;

namespace pgn::rpc {
// JSON numeric tokens preserve exact lexical text in UniValue, but a client
// converting them to binary floating point can lose precision. String mode is
// explicit; neither mode silently changes an existing RPC schema.
enum class AmountJsonEncoding { Number, String };
enum class AmountDomain { Transfer, Delta };

// Read-only real Core chain context. Missing/invalid metadata refuses with a
// genuine RPC error; returning this data never authorizes startup or consensus.
core::MonetaryContext RequireSelectedMonetaryContext();
core::AtomicFeeRatePolicy RequireSelectedAtomicFeeRatePolicy();
CFeeRate FeeRateFromAtomicPerVByte(const UniValue&,
                                 const std::optional<core::AtomicFeeRatePolicy>&);
// Preserve the currency/kvB RPC unit without treating a rate as a transferable
// amount. Currency context supplies precision/text capacity; rate policy alone
// supplies admission. The informational output has no relay/admission cap.
CFeeRate FeeRateFromCurrencyPerKVByte(const UniValue&,
    const std::optional<core::MonetaryContext>&,
    const std::optional<core::AtomicFeeRatePolicy>&);
UniValue ValueFromAtomicRatePerKVByte(CAmount, AmountJsonEncoding,
    const std::optional<core::MonetaryContext>&);

// Candidate native RPC boundary. No default policy, consensus selection,
// MoneyRange/COIN fallback, endpoint registration or wallet initialization.
// Transfer input is strict unsigned ASCII decimal, even for JSON number tokens.
// Omitted/null amounts are errors, not zero. Errors are genuine JSONRPCError.
CAmount AmountFromValue(const UniValue&,
                        const std::optional<core::ExactAmountPolicy>&,
                        core::ExactAmountTextBudget);
UniValue ValueFromAmount(CAmount, AmountDomain, AmountJsonEncoding,
                        const std::optional<core::ExactAmountPolicy>&,
                        core::ExactAmountTextBudget);
} // namespace pgn::rpc

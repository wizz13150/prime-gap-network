// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_CURRENCYFEERATE_H
#define PGN_QT_CURRENCYFEERATE_H
#include <pgn/monetary_context.h>
#include <policy/feerate.h>
#include <QString>

namespace pgn::qt {
struct CurrencyFeeRatePreview {
    core::ExactAmountPolicy numeric;
    core::ExactAmountTextBudget budget;
};
/** Ergonomic validator metadata only, NEVER rate admission/approval. A result
 * from the real native converter remains mandatory before using any rate. */
std::optional<CurrencyFeeRatePreview> PreviewCurrencyFeeRatePolicy(
    const std::optional<core::MonetaryContext>&,
    const std::optional<core::AtomicFeeRatePolicy>&);
bool SameCurrencyFeeRateMetadata(const std::optional<core::MonetaryContext>&,
    const std::optional<core::AtomicFeeRatePolicy>&,
    const std::optional<core::MonetaryContext>&,
    const std::optional<core::AtomicFeeRatePolicy>&);
enum class CurrencyFeeRateStatus { Unavailable=0, ParsedNative, InvalidText,
    NativeRefusal, WrongThread, OwnerUnavailable, ContextChanged };
struct CurrencyFeeRateResult {
    CurrencyFeeRateStatus status{CurrencyFeeRateStatus::Unavailable};
    std::optional<CFeeRate> rate;
    std::optional<CAmount> atomic_per_kvbyte;
    explicit operator bool() const { return status == CurrencyFeeRateStatus::ParsedNative && rate && atomic_per_kvbyte; }
};
/** Native rpc helper called as a C++ conversion function, NO endpoint/node.
 * No COIN/default/transfer ceiling. Metadata rejected BEFORE input inspection;
 * every accepted result comes from FeeRateFromCurrencyPerKVByte itself. This is
 * numeric/policy admission, not relay, wallet/profile approval or fee quote. */
CurrencyFeeRateResult AdmitCurrencyFeeRate(const QString&,
    const std::optional<core::MonetaryContext>&,
    const std::optional<core::AtomicFeeRatePolicy>&);
} // namespace pgn::qt
#endif

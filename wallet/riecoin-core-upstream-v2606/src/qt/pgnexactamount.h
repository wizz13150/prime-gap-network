// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.

#ifndef PGN_QT_EXACTAMOUNT_H
#define PGN_QT_EXACTAMOUNT_H

#include <consensus/amount.h>

#include <QString>

#include <cstdint>
#include <optional>

namespace pgn::qt {

/** Supplied by a future approved owner; no PGN/RIC policy is provided here.
 * max_atomic is a nonnegative CAmount ceiling, NOT a total-supply declaration.
 * symbol is plain text; an HTML consumer must escape it separately.
 */
struct ExactAmountPolicy {
    uint32_t decimals;
    CAmount max_atomic;
    QString symbol;
};

/** An explicit UI resource budget, not a consensus precision/amount limit.
 * It must fit the policy's longest output, including sign and plain-text unit.
 */
struct ExactAmountTextBudget {
    qsizetype max_characters;
};

enum class ExactAmountError {
    None,
    MissingPolicy,
    InvalidPolicy,
    InvalidBudget,
    BudgetExceeded,
    InvalidText,
    ExcessPrecision,
    CAmountOverflow,
    CeilingExceeded,
    NegativeTransfer,
    InvalidFormat,
};

struct ExactAmountParseResult {
    ExactAmountError error;
    std::optional<CAmount> amount;
    explicit operator bool() const { return error == ExactAmountError::None && amount.has_value(); }
};

struct ExactAmountFormatResult {
    ExactAmountError error;
    std::optional<QString> text;
    explicit operator bool() const { return error == ExactAmountError::None && text.has_value(); }
};

enum class ExactAmountFormat {
    Transfer,       //!< Nonnegative, parseable amount (URI/CSV/input), no sign.
    Delta,          //!< Signed transaction delta, negative sign only.
    DeltaWithPlus,  //!< Positive nonzero transaction deltas also get '+'.
};

/** Qt presentation adapter for the single pgn::core exact numeric primitive;
 * no QApplication, Node, wallet, locale or network.
 * Parse grammar: ASCII [0-9]+(\.[0-9]+)?; no trimming, sign, exponent, grouping,
 * Unicode digits or separator rewriting. Leading zeroes mean decimal only.
 * Excess fractional digits are rejected even when those digits are zero.
 * Formatting always emits all policy decimals, never grouping or rounding.
 * Failed results contain NO payload: failure must not become a fake zero.
 */
enum class ExactAmountUnitMode { RequiredSymbol, NumericOnly };
class ExactAmountCodec final {
public:
    ExactAmountCodec(std::optional<ExactAmountPolicy> policy, ExactAmountTextBudget budget,
        ExactAmountUnitMode mode = ExactAmountUnitMode::RequiredSymbol);
    ExactAmountError policyStatus() const { return m_status; }
    ExactAmountParseResult parse(const QString& text) const;
    ExactAmountFormatResult format(CAmount amount, ExactAmountFormat mode, bool with_symbol) const;

private:
    const std::optional<ExactAmountPolicy> m_policy;
    const ExactAmountTextBudget m_budget;
    const ExactAmountUnitMode m_unit_mode;
    const ExactAmountError m_status;
};

} // namespace pgn::qt

#endif // PGN_QT_EXACTAMOUNT_H

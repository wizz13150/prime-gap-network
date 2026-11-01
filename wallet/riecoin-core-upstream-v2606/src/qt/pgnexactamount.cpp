// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.

#include <qt/pgnexactamount.h>

#include <pgn/exact_amount.h>

#include <string>
#include <utility>

namespace pgn::qt {
namespace {

// Keep the UI and native contracts decoupled: an enum insertion must not silently
// reinterpret an error or display mode through an ordinal cast.
ExactAmountError FromNativeError(core::ExactAmountError error)
{
    switch (error) {
    case core::ExactAmountError::None: return ExactAmountError::None;
    case core::ExactAmountError::MissingPolicy: return ExactAmountError::MissingPolicy;
    case core::ExactAmountError::InvalidPolicy: return ExactAmountError::InvalidPolicy;
    case core::ExactAmountError::InvalidBudget: return ExactAmountError::InvalidBudget;
    case core::ExactAmountError::BudgetExceeded: return ExactAmountError::BudgetExceeded;
    case core::ExactAmountError::InvalidText: return ExactAmountError::InvalidText;
    case core::ExactAmountError::ExcessPrecision: return ExactAmountError::ExcessPrecision;
    case core::ExactAmountError::CAmountOverflow: return ExactAmountError::CAmountOverflow;
    case core::ExactAmountError::CeilingExceeded: return ExactAmountError::CeilingExceeded;
    case core::ExactAmountError::NegativeTransfer: return ExactAmountError::NegativeTransfer;
    case core::ExactAmountError::InvalidFormat: return ExactAmountError::InvalidFormat;
    }
    return ExactAmountError::InvalidPolicy; // Unknown native error fails closed.
}

std::optional<core::ExactAmountPolicy> NativePolicy(const std::optional<ExactAmountPolicy>& policy)
{
    if (!policy) return std::nullopt;
    return core::ExactAmountPolicy{policy->decimals, policy->max_atomic};
}

std::optional<core::ExactAmountFormat> NativeFormat(ExactAmountFormat mode)
{
    switch (mode) {
    case ExactAmountFormat::Transfer: return core::ExactAmountFormat::Transfer;
    case ExactAmountFormat::Delta: return core::ExactAmountFormat::Delta;
    case ExactAmountFormat::DeltaWithPlus: return core::ExactAmountFormat::DeltaWithPlus;
    }
    return std::nullopt;
}

core::ExactAmountTextBudget NumericOutputBudget(const ExactAmountPolicy& policy, ExactAmountTextBudget budget, ExactAmountUnitMode mode)
{
    if (mode == ExactAmountUnitMode::NumericOnly) return {static_cast<uint64_t>(budget.max_characters)};
    // Called only after Qt policy validation proves this subtraction positive.
    return {static_cast<uint64_t>(budget.max_characters - policy.symbol.size() - 1)};
}

ExactAmountError CheckPolicy(const std::optional<ExactAmountPolicy>& policy, ExactAmountTextBudget budget, ExactAmountUnitMode mode)
{
    if (!policy) return ExactAmountError::MissingPolicy;
    if (budget.max_characters <= 0) return ExactAmountError::InvalidBudget;
    if (mode == ExactAmountUnitMode::NumericOnly) {
        if (!policy->symbol.isEmpty()) return ExactAmountError::InvalidPolicy;
        return FromNativeError(core::validateExactAmountPolicy(NativePolicy(policy), {static_cast<uint64_t>(budget.max_characters)}));
    }
    if (mode != ExactAmountUnitMode::RequiredSymbol || policy->symbol.isEmpty() || policy->symbol.trimmed().isEmpty()) return ExactAmountError::InvalidPolicy;
    const auto native_policy = NativePolicy(policy);
    const auto status = core::validateExactAmountPolicy(native_policy, {static_cast<uint64_t>(budget.max_characters)});
    if (status != core::ExactAmountError::None) return FromNativeError(status);
    if (policy->symbol.size() >= budget.max_characters - 1) return ExactAmountError::BudgetExceeded;
    // Core owns all numeric domain/width validation. Qt reserves the unit and
    // space; Core already accounts for the possible transaction-delta sign.
    return FromNativeError(core::validateExactAmountPolicy(native_policy, NumericOutputBudget(*policy, budget, mode)));
}

} // namespace

ExactAmountCodec::ExactAmountCodec(std::optional<ExactAmountPolicy> policy, ExactAmountTextBudget budget, ExactAmountUnitMode mode)
    : m_policy(std::move(policy)), m_budget(budget), m_unit_mode(mode), m_status(CheckPolicy(m_policy, m_budget, mode))
{
}

ExactAmountParseResult ExactAmountCodec::parse(const QString& text) const
{
    const auto fail = [](ExactAmountError error) { return ExactAmountParseResult{error, std::nullopt}; };
    if (m_status != ExactAmountError::None) return fail(m_status);
    if (text.size() > m_budget.max_characters) return fail(ExactAmountError::BudgetExceeded);
    // Do not use lossy Latin1 replacement or UTF8 byte expansion: the Qt text
    // budget is in UTF16 characters, and non-ASCII is an InvalidText result.
    std::string ascii;
    ascii.reserve(static_cast<size_t>(text.size()));
    for (const QChar ch : text) {
        if (ch.unicode() > 127) return fail(ExactAmountError::InvalidText);
        ascii.push_back(static_cast<char>(ch.unicode()));
    }
    const auto parsed = core::parseExactAmount(ascii, NativePolicy(m_policy), {static_cast<uint64_t>(m_budget.max_characters)});
    if (parsed.error != core::ExactAmountError::None) return fail(FromNativeError(parsed.error));
    if (!parsed.amount) return fail(ExactAmountError::InvalidText);
    return {ExactAmountError::None, parsed.amount};
}

ExactAmountFormatResult ExactAmountCodec::format(CAmount amount, ExactAmountFormat mode, bool with_symbol) const
{
    const auto fail = [](ExactAmountError error) { return ExactAmountFormatResult{error, std::nullopt}; };
    if (m_status != ExactAmountError::None) return fail(m_status);
    const auto native_mode = NativeFormat(mode);
    if (!native_mode) return fail(ExactAmountError::InvalidFormat);
    if (with_symbol && m_unit_mode == ExactAmountUnitMode::NumericOnly) return fail(ExactAmountError::InvalidFormat);
    const auto formatted = core::formatExactAmount(amount, *native_mode, NativePolicy(m_policy), NumericOutputBudget(*m_policy, m_budget, m_unit_mode));
    if (formatted.error != core::ExactAmountError::None) return fail(FromNativeError(formatted.error));
    if (!formatted.text) return fail(ExactAmountError::InvalidFormat);
    QString text = QString::fromLatin1(formatted.text->data(), static_cast<qsizetype>(formatted.text->size()));
    if (with_symbol) {
        text.append(QLatin1Char(' '));
        text.append(m_policy->symbol);
    }
    return {ExactAmountError::None, std::move(text)};
}

} // namespace pgn::qt

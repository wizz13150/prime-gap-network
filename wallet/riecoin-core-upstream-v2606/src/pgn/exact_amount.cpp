// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING
// file.
#include "exact_amount.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace pgn::core {
ExactAmountError
validateExactAmountPolicy(const std::optional<ExactAmountPolicy> &policy,
                          ExactAmountTextBudget budget) {
  if (!policy)
    return ExactAmountError::MissingPolicy;
  if (!budget.max_characters)
    return ExactAmountError::InvalidBudget;
  if (policy->max_atomic < 0)
    return ExactAmountError::InvalidPolicy;
  const std::uint64_t digits = std::to_string(policy->max_atomic).size();
  const std::uint64_t decimals = policy->decimals;
  const auto width = decimals ? std::max(digits, decimals + 1) + 1 : digits;
  // Include a possible delta sign. No allocation proportional to decimals.
  return width + 1 > budget.max_characters ? ExactAmountError::BudgetExceeded
                                           : ExactAmountError::None;
}
ExactAmountParseResult
parseExactAmount(std::string_view text,
                 const std::optional<ExactAmountPolicy> &policy,
                 ExactAmountTextBudget budget) {
  const auto fail = [](ExactAmountError error) {
    return ExactAmountParseResult{error, std::nullopt};
  };
  const auto status = validateExactAmountPolicy(policy, budget);
  if (status != ExactAmountError::None)
    return fail(status);
  if (text.size() > budget.max_characters)
    return fail(ExactAmountError::BudgetExceeded);
  if (text.empty())
    return fail(ExactAmountError::InvalidText);
  auto dot = std::string_view::npos;
  for (std::size_t index = 0; index < text.size(); ++index) {
    const char ch = text[index];
    if (ch == '.') {
      if (dot != std::string_view::npos || !index || index + 1 == text.size())
        return fail(ExactAmountError::InvalidText);
      dot = index;
    } else if (ch < '0' || ch > '9')
      return fail(ExactAmountError::InvalidText);
  }
  const auto fractional =
      dot == std::string_view::npos ? 0 : text.size() - dot - 1;
  if (fractional > policy->decimals)
    return fail(ExactAmountError::ExcessPrecision);
  CAmount atomic{};
  constexpr auto limit = std::numeric_limits<CAmount>::max();
  const auto fold = [&](unsigned digit) {
    if (atomic > (limit - digit) / 10)
      return false;
    atomic = atomic * 10 + digit;
    return true;
  };
  for (const char ch : text)
    if (ch != '.' && !fold(static_cast<unsigned>(ch - '0')))
      return fail(ExactAmountError::CAmountOverflow);
  // A zero needs no padding. Positive values overflow within at most19 folds,
  // preserving configurable precision without a billion-iteration zero loop.
  if (atomic)
    for (std::uint64_t index = fractional; index < policy->decimals; ++index)
      if (!fold(0))
        return fail(ExactAmountError::CAmountOverflow);
  if (atomic > policy->max_atomic)
    return fail(ExactAmountError::CeilingExceeded);
  return {ExactAmountError::None, atomic};
}
ExactAmountFormatResult
formatExactAmount(CAmount amount, ExactAmountFormat mode,
                  const std::optional<ExactAmountPolicy> &policy,
                  ExactAmountTextBudget budget) {
  const auto fail = [](ExactAmountError error) {
    return ExactAmountFormatResult{error, std::nullopt};
  };
  const auto status = validateExactAmountPolicy(policy, budget);
  if (status != ExactAmountError::None)
    return fail(status);
  switch (mode) {
  case ExactAmountFormat::Transfer:
    if (amount < 0)
      return fail(ExactAmountError::NegativeTransfer);
    break;
  case ExactAmountFormat::Delta:
  case ExactAmountFormat::DeltaWithPlus:
    break;
  default:
    return fail(ExactAmountError::InvalidFormat);
  }
  const auto magnitude = amount < 0
                             ? static_cast<std::uint64_t>(-(amount + 1)) + 1
                             : static_cast<std::uint64_t>(amount);
  if (magnitude > static_cast<std::uint64_t>(policy->max_atomic))
    return fail(ExactAmountError::CeilingExceeded);
  std::string text = std::to_string(magnitude);
  const std::uint64_t decimals = policy->decimals;
  const auto width =
      decimals ? std::max<std::uint64_t>(text.size(), decimals + 1) + 1
               : text.size();
  if (width + 1 > text.max_size())
    return fail(ExactAmountError::BudgetExceeded);
  if (decimals) {
    if (text.size() <= decimals)
      text.insert(0, static_cast<std::size_t>(decimals + 1 - text.size()), '0');
    text.insert(text.size() - static_cast<std::size_t>(decimals), 1, '.');
  }
  if (amount < 0)
    text.insert(0, 1, '-');
  else if (amount > 0 && mode == ExactAmountFormat::DeltaWithPlus)
    text.insert(0, 1, '+');
  return {ExactAmountError::None, std::move(text)};
}
} // namespace pgn::core

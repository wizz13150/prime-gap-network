// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING
// file.
#pragma once
#include <consensus/amount.h>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pgn::core {
struct ExactAmountPolicy {
  std::uint32_t decimals;
  CAmount max_atomic; // Explicit per-value ceiling, not a supply declaration.
};
struct ExactAmountTextBudget {
  std::uint64_t max_characters; // ASCII text budget, not consensus precision.
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
  InvalidFormat
};
enum class ExactAmountFormat { Transfer, Delta, DeltaWithPlus };
struct ExactAmountParseResult {
  ExactAmountError error;
  std::optional<CAmount> amount;
};
struct ExactAmountFormatResult {
  ExactAmountError error;
  std::optional<std::string> text;
};
// No Qt, locale, floating point, COIN/MAX_MONEY, node, wallet or defaults.
// Strict ASCII decimal transfer text; no sign/exponent/separator rewriting.
// These are number codecs only, not consensus approval or wallet admission.
ExactAmountError
validateExactAmountPolicy(const std::optional<ExactAmountPolicy> &,
                          ExactAmountTextBudget);
ExactAmountParseResult
parseExactAmount(std::string_view, const std::optional<ExactAmountPolicy> &,
                 ExactAmountTextBudget);
ExactAmountFormatResult
formatExactAmount(CAmount, ExactAmountFormat,
                  const std::optional<ExactAmountPolicy> &,
                  ExactAmountTextBudget);
} // namespace pgn::core

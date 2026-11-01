#pragma once
#include "range_commitment.h"

namespace pgn::core {
struct ArithmeticLimits {
  std::uint32_t max_input_bits;
  std::uint32_t max_fractional_bits;
  std::uint32_t max_series_terms;
};
struct LogAccuracy {
  std::uint32_t fractional_bits;
  std::uint32_t series_terms;
};
struct LogBounds {
  Integer lower;
  Integer upper;
  std::uint32_t fractional_bits;
};
enum class MeritDecision { Below, Meets, Indeterminate };

// Deterministic directed integer arithmetic, not a floating-point consensus
// rule. The caller supplies the exact rational threshold and resource policy.
// These are candidate primitives: no target, precision or mainnet is activated.
LogBounds naturalLogBounds(const Integer &, LogAccuracy, ArithmeticLimits);
MeritDecision compareMerit(const Integer &lower_prime, const Integer &gap,
                           const Integer &target_numerator,
                           const Integer &target_denominator, LogAccuracy,
                           ArithmeticLimits);
} // namespace pgn::core

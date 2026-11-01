#pragma once
#include "range_commitment.h"

namespace pgn::core {
enum class PrimeVerdict { Prime, Composite, UnsupportedDomain };
enum class GapVerdict { Verified, Invalid, UnsupportedDomain, BudgetExceeded };
struct GapCheck {
  GapVerdict verdict;
  std::uint64_t interior_positions_checked;
};

// First twelve PRIME bases only, with the STRICT published bound from
// Sorenson/Webster, arXiv:1509.00864, Theorem1.1. Not a 96/256-bit test.
// No unsupported input is accepted as prime. Larger certificate engines may
// implement the same verdict interface without changing scientific ownership.
const Integer &mr12ExclusiveBound();
PrimeVerdict deterministicPrimeBelowMr12Bound(const Integer &);
// True means a valid compositeness witness, not that a passing test is prime.
// The caller must bound number/base sizes before modular arithmetic.
bool verifyStrongCompositeWitness(const Integer &number, const Integer &base);
GapCheck verifyConsecutiveGap(const Integer &lower, const Integer &gap,
                              std::uint64_t max_interior_positions);
} // namespace pgn::core

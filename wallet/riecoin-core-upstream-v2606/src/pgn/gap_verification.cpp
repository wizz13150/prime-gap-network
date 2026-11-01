#include "gap_verification.h"
#include <array>

namespace pgn::core {
namespace {
constexpr std::array<unsigned, 12> bases{2,  3,  5,  7,  11, 13,
                                         17, 19, 23, 29, 31, 37};
bool strongProbablePrime(const Integer &number, const Integer &base,
                         const Integer &odd, unsigned twos) {
  Integer residue = boost::multiprecision::powm(base, odd, number);
  if (residue == 1 || residue == number - 1)
    return true;
  for (unsigned i = 1; i < twos; ++i) {
    residue = residue * residue % number;
    if (residue == number - 1)
      return true;
    if (residue == 1)
      return false;
  }
  return false;
}
} // namespace
bool verifyStrongCompositeWitness(const Integer &number, const Integer &base) {
  // A strong-test failure is a compositeness proof at ANY magnitude. A pass
  // is not a prime proof here. Excluding base>=n prevents false prime claims
  // produced by a trivial multiple-of-n base. Caller admits integer/cost size.
  if (number < 3 || base < 2 || base >= number)
    return false;
  if ((number & 1) == 0)
    return true;
  Integer odd = number - 1;
  unsigned twos = 0;
  while ((odd & 1) == 0) {
    odd >>= 1;
    ++twos;
  }
  return !strongProbablePrime(number, base, odd, twos);
}
const Integer &mr12ExclusiveBound() {
  // Exact literal assembled from native-width chunks: no dynamic decimal
  // parser and no GCC16/Boost string-parser overread diagnostic in this path.
  static const Integer bound =
      Integer(318665857834031151ULL) * 1000000 + 167461;
  return bound;
}
PrimeVerdict deterministicPrimeBelowMr12Bound(const Integer &number) {
  if (number < 2)
    return PrimeVerdict::Composite;
  if (number >= mr12ExclusiveBound())
    return PrimeVerdict::UnsupportedDomain;
  for (auto base : bases) {
    if (number == base)
      return PrimeVerdict::Prime;
    if (number % base == 0)
      return PrimeVerdict::Composite;
  }
  Integer odd = number - 1;
  unsigned twos = 0;
  while ((odd & 1) == 0) {
    odd >>= 1;
    ++twos;
  }
  for (auto base : bases)
    if (!strongProbablePrime(number, base, odd, twos))
      return PrimeVerdict::Composite;
  return PrimeVerdict::Prime;
}
GapCheck verifyConsecutiveGap(const Integer &lower, const Integer &gap,
                              std::uint64_t budget) {
  if (lower < 2 || gap <= 0)
    return {GapVerdict::Invalid, 0};
  const Integer upper = lower + gap;
  if (upper >= mr12ExclusiveBound())
    return {GapVerdict::UnsupportedDomain, 0};
  // Check all input-dependent cost before exponentiation or conversion.
  if (gap - 1 > budget)
    return {GapVerdict::BudgetExceeded, 0};
  if (deterministicPrimeBelowMr12Bound(lower) != PrimeVerdict::Prime ||
      deterministicPrimeBelowMr12Bound(upper) != PrimeVerdict::Prime)
    return {GapVerdict::Invalid, 0};
  const auto positions = (gap - 1).convert_to<std::uint64_t>();
  for (std::uint64_t index = 0; index < positions; ++index)
    if (deterministicPrimeBelowMr12Bound(lower + Integer(index) + 1) !=
        PrimeVerdict::Composite)
      return {GapVerdict::Invalid, index + 1};
  return {GapVerdict::Verified, positions};
}
} // namespace pgn::core

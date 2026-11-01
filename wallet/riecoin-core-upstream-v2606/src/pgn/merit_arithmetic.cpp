#include "merit_arithmetic.h"
#include <stdexcept>

namespace pgn::core {
namespace {
void require(bool valid, const char *message) {
  if (!valid)
    throw std::invalid_argument(message);
}
Integer ceilDiv(const Integer &numerator, const Integer &denominator) {
  return (numerator + denominator - 1) / denominator;
}
bool admitted(const Integer &value, std::uint32_t bits) {
  return value >= 0 && (value == 0 || boost::multiprecision::msb(value) < bits);
}
std::pair<Integer, Integer> logRatio(const Integer &numerator,
                                     const Integer &denominator,
                                     const Integer &scale,
                                     std::uint32_t terms) {
  // ln(y) = 2 atanh(z), z=(y-1)/(y+1), 0<=z<=1/3.
  // Every multiply/divide rounds outwards. After N terms the positive tail
  // is <= 2*z^(2N+1)/((2N+1)*(1-z^2)). No cancellation or float occurs.
  if (numerator == 0)
    return {0, 0};
  const Integer zLow = numerator * scale / denominator;
  const Integer zHigh = ceilDiv(numerator * scale, denominator);
  const Integer squareLow = zLow * zLow / scale;
  const Integer squareHigh = ceilDiv(zHigh * zHigh, scale);
  require(squareHigh < scale,
          "Insufficient fixed-point precision for log tail");
  Integer powerLow = zLow, powerHigh = zHigh, low = 0, high = 0;
  for (std::uint64_t i = 0; i < terms; ++i) {
    const Integer odd = 2 * i + 1;
    low += 2 * powerLow / odd;
    high += ceilDiv(2 * powerHigh, odd);
    powerLow = powerLow * squareLow / scale;
    powerHigh = ceilDiv(powerHigh * squareHigh, scale);
  }
  const Integer tail = ceilDiv(2 * powerHigh * scale,
                               (Integer(2) * terms + 1) * (scale - squareHigh));
  return {low, high + tail};
}
} // namespace
LogBounds naturalLogBounds(const Integer &number, LogAccuracy accuracy,
                           ArithmeticLimits limits) {
  require(limits.max_input_bits > 0 && limits.max_fractional_bits >= 4 &&
              limits.max_series_terms > 0,
          "Explicit arithmetic resource limits required");
  require(number >= 2 && admitted(number, limits.max_input_bits),
          "Log input outside explicit admission");
  require(accuracy.fractional_bits >= 4 &&
              accuracy.fractional_bits <= limits.max_fractional_bits &&
              accuracy.series_terms > 0 &&
              accuracy.series_terms <= limits.max_series_terms,
          "Log accuracy outside explicit admission");
  const auto exponent = boost::multiprecision::msb(number);
  const Integer powerOfTwo = Integer(1) << exponent;
  const Integer scale = Integer(1) << accuracy.fractional_bits;
  const auto [twoLow, twoHigh] = logRatio(1, 3, scale, accuracy.series_terms);
  const auto [remainderLow, remainderHigh] = logRatio(
      number - powerOfTwo, number + powerOfTwo, scale, accuracy.series_terms);
  return {twoLow * exponent + remainderLow, twoHigh * exponent + remainderHigh,
          accuracy.fractional_bits};
}
MeritDecision compareMerit(const Integer &lowerPrime, const Integer &gap,
                           const Integer &numerator, const Integer &denominator,
                           LogAccuracy accuracy, ArithmeticLimits limits) {
  require(gap > 0 && numerator > 0 && denominator > 0 &&
              admitted(gap, limits.max_input_bits) &&
              admitted(numerator, limits.max_input_bits) &&
              admitted(denominator, limits.max_input_bits),
          "Invalid gap, threshold or explicit input admission");
  const auto bounds = naturalLogBounds(lowerPrime, accuracy, limits);
  const Integer scaledGap = (gap * denominator) << bounds.fractional_bits;
  if (scaledGap >= numerator * bounds.upper)
    return MeritDecision::Meets;
  if (scaledGap < numerator * bounds.lower)
    return MeritDecision::Below;
  return MeritDecision::Indeterminate;
}
} // namespace pgn::core

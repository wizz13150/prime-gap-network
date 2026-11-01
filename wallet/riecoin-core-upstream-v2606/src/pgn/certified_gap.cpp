#include "certified_gap.h"

namespace pgn::core {
namespace {
bool admitted(const Integer &n, std::uint32_t bits) {
  return n >= 0 && (n == 0 || boost::multiprecision::msb(n) < bits);
}
bool provesComposite(const Integer &number, const CompositeWitness &witness) {
  if (witness.value < 2 || witness.value >= number)
    return false;
  switch (witness.kind) {
  case CompositeWitnessKind::Divisor:
    return number % witness.value == 0;
  case CompositeWitnessKind::StrongTestFailure:
    return verifyStrongCompositeWitness(number, witness.value);
  }
  return false;
}
} // namespace
CertifiedGapCheck verifyCertifiedGap(const CertifiedGap &evidence,
                                     CertifiedGapLimits limits) {
  if (evidence.lower < 2 || evidence.gap <= 0)
    return {CertifiedGapVerdict::InvalidEvidence, 0};
  const auto bits = limits.endpoint_limits.max_integer_bits;
  if (!bits || !admitted(evidence.lower, bits) || !admitted(evidence.gap, bits))
    return {CertifiedGapVerdict::BudgetExceeded, 0};
  const Integer upper = evidence.lower + evidence.gap;
  if (!admitted(upper, bits) ||
      evidence.gap - 1 > limits.max_interior_positions)
    return {CertifiedGapVerdict::BudgetExceeded, 0};
  if (evidence.gap - 1 != evidence.interior.size())
    return {CertifiedGapVerdict::InvalidEvidence, 0};
  for (const auto &witness : evidence.interior) {
    if (witness.kind != CompositeWitnessKind::Divisor &&
        witness.kind != CompositeWitnessKind::StrongTestFailure)
      return {CertifiedGapVerdict::InvalidEvidence, 0};
    if (witness.value < 2)
      return {CertifiedGapVerdict::InvalidEvidence, 0};
    if (!admitted(witness.value, bits))
      return {CertifiedGapVerdict::BudgetExceeded, 0};
  }
  for (const auto *certificate :
       {&evidence.lower_certificate, &evidence.upper_certificate}) {
    const auto &expected =
        certificate == &evidence.lower_certificate ? evidence.lower : upper;
    const auto verdict =
        verifyPrimeCertificate(expected, *certificate, limits.endpoint_limits);
    if (verdict == CertificateVerdict::BudgetExceeded)
      return {CertifiedGapVerdict::BudgetExceeded, 0};
    if (verdict != CertificateVerdict::VerifiedPrime)
      return {CertifiedGapVerdict::InvalidEvidence, 0};
  }
  for (std::size_t index = 0; index < evidence.interior.size(); ++index)
    if (!provesComposite(evidence.lower + Integer(index) + 1,
                         evidence.interior[index]))
      return {CertifiedGapVerdict::InvalidEvidence,
              static_cast<std::uint64_t>(index) + 1};
  return {CertifiedGapVerdict::VerifiedConsecutiveGap,
          static_cast<std::uint64_t>(evidence.interior.size())};
}
} // namespace pgn::core

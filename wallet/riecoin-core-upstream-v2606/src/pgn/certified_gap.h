#pragma once
#include "prime_certificate.h"

namespace pgn::core {
enum class CompositeWitnessKind : std::uint8_t { Divisor, StrongTestFailure };
struct CompositeWitness {
  CompositeWitnessKind kind;
  Integer value;
};
struct CertifiedGap {
  Integer lower;
  Integer gap;
  std::vector<PrimeCertificateNode> lower_certificate;
  std::vector<PrimeCertificateNode> upper_certificate;
  // Entry i proves lower+i+1 composite. Implicit positions prevent omission,
  // duplication and mismatched claimed number fields.
  std::vector<CompositeWitness> interior;
};
struct CertifiedGapLimits {
  CertificateLimits endpoint_limits;
  std::uint64_t max_interior_positions;
};
enum class CertifiedGapVerdict {
  VerifiedConsecutiveGap,
  InvalidEvidence,
  BudgetExceeded
};
struct CertifiedGapCheck {
  CertifiedGapVerdict verdict;
  std::uint64_t interior_positions_examined;
};
// Exact local gap evidence only. No resource defaults, search, mainnet
// activation, ticket approval, result-list completeness or range coverage.
CertifiedGapCheck verifyCertifiedGap(const CertifiedGap &, CertifiedGapLimits);
} // namespace pgn::core

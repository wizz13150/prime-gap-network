#pragma once
#include "gap_verification.h"
#include <optional>

namespace pgn::core {
struct PrimeFactorWitness {
  Integer prime;
  std::uint32_t exponent;
  Integer witness;
  std::optional<std::uint32_t> prior_certificate;
};
struct PrimeCertificateNode {
  Integer number;
  std::vector<PrimeFactorWitness> factors;
};
struct CertificateLimits {
  std::uint32_t max_integer_bits;
  std::uint32_t max_nodes;
  std::uint32_t max_factors_per_node;
};
enum class CertificateVerdict { VerifiedPrime, Invalid, BudgetExceeded };

// A flat, increasing certificate DAG avoids recursion/cycles. Each reference
// must point backwards to an already verified factor with identical value.
// No factoring or witness search occurs in this verifier. Candidate interface,
// not an approved wire format, resource policy or network activation.
CertificateVerdict
verifyPrimeCertificate(const Integer &expected,
                       const std::vector<PrimeCertificateNode> &,
                       CertificateLimits);
} // namespace pgn::core

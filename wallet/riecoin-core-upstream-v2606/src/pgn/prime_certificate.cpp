#include "prime_certificate.h"

namespace pgn::core {
namespace {
Integer gcd(Integer a, Integer b) {
  if (a < 0)
    a = -a;
  while (b != 0) {
    Integer rest = a % b;
    a = b;
    b = rest;
  }
  return a;
}
bool admitted(const Integer &value, std::uint32_t bits) {
  return value >= 0 && (value == 0 || boost::multiprecision::msb(value) < bits);
}
} // namespace
CertificateVerdict
verifyPrimeCertificate(const Integer &expected,
                       const std::vector<PrimeCertificateNode> &nodes,
                       CertificateLimits limits) {
  if (!limits.max_integer_bits || !limits.max_nodes ||
      !limits.max_factors_per_node || nodes.size() > limits.max_nodes ||
      !admitted(expected, limits.max_integer_bits))
    return CertificateVerdict::BudgetExceeded;
  if (expected < 2 || nodes.empty() || nodes.back().number != expected)
    return CertificateVerdict::Invalid;
  // Admission is checked for the ENTIRE supplied graph before modular powers.
  for (const auto &node : nodes) {
    if (!admitted(node.number, limits.max_integer_bits) ||
        node.factors.size() > limits.max_factors_per_node)
      return CertificateVerdict::BudgetExceeded;
    for (const auto &factor : node.factors)
      if (!admitted(factor.prime, limits.max_integer_bits) ||
          !admitted(factor.witness, limits.max_integer_bits) ||
          factor.exponent > limits.max_integer_bits)
        return CertificateVerdict::BudgetExceeded;
  }
  std::vector<bool> referenced(nodes.size(), false);
  referenced.back() = true;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const auto &node = nodes[i];
    const Integer &n = node.number;
    if (n < 2 || (i && n <= nodes[i - 1].number))
      return CertificateVerdict::Invalid;
    if (node.factors.empty()) {
      if (deterministicPrimeBelowMr12Bound(n) != PrimeVerdict::Prime)
        return CertificateVerdict::Invalid;
      continue;
    }
    if ((n & 1) == 0)
      return CertificateVerdict::Invalid;
    const Integer predecessor = n - 1;
    Integer factored = 1, previous = 0;
    for (const auto &factor : node.factors) {
      const auto &q = factor.prime;
      if (q < 2 || q >= n || q <= previous || factor.exponent == 0 ||
          factor.witness < 2 || factor.witness >= n)
        return CertificateVerdict::Invalid;
      previous = q;
      if (factor.prior_certificate) {
        const auto prior = *factor.prior_certificate;
        if (prior >= i || nodes[prior].number != q)
          return CertificateVerdict::Invalid;
        referenced[prior] = true;
      } else if (deterministicPrimeBelowMr12Bound(q) != PrimeVerdict::Prime) {
        return CertificateVerdict::Invalid;
      }
      for (std::uint32_t exponent = 0; exponent < factor.exponent; ++exponent) {
        // The product is never allowed to grow beyond the input n-1.
        if (factored > predecessor / q)
          return CertificateVerdict::Invalid;
        factored *= q;
      }
    }
    if (factored <= 1 || predecessor % factored != 0 ||
        factored * factored <= n)
      return CertificateVerdict::Invalid;
    for (const auto &factor : node.factors) {
      const Integer fermat =
          boost::multiprecision::powm(factor.witness, predecessor, n);
      if (fermat != 1)
        return CertificateVerdict::Invalid;
      const Integer reduced = boost::multiprecision::powm(
          factor.witness, Integer(predecessor / factor.prime), n);
      if (gcd(reduced - 1, n) != 1)
        return CertificateVerdict::Invalid;
    }
    // Pocklington: every prime divisor r of n satisfies F | r-1.
    // With F² > n, a composite n would have r <= sqrt(n) < F,
    // contradicting r >= F+1. This certifies n, not a searched range.
  }
  for (bool used : referenced)
    if (!used)
      return CertificateVerdict::Invalid;
  return CertificateVerdict::VerifiedPrime;
}
} // namespace pgn::core

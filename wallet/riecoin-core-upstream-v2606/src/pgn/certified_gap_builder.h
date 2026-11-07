// MIT. Local evidence production for an already supplied pair, NOT mining.
#pragma once
#include "certified_gap.h"
#include <optional>

namespace pgn::core {
enum class GapBuildVerdict { Built, InvalidPair, UnsupportedDomain, BudgetExceeded, Cancelled };
struct GapBuildResult {
  GapBuildVerdict verdict;
  std::optional<CertifiedGap> evidence;
  std::uint64_t interior_positions_built;
};
// Does not search for endpoints, change GPU work, sign, submit or grant credit.
// Uses existing exact bounded MR12 endpoint certificates and explicit interior
// witnesses. Above the STRICT MR12 bound, return UnsupportedDomain, never PRP.
// All cost-dependent admission precedes exponentiation/allocation. Limits are
// the caller's explicit local resource budget, NOT network defaults/policy.
// Cancellation is cooperative between bounded endpoint/interior operations;
// non-Built results NEVER expose a partially populated positive certificate.
// Consumers must independently verify evidence with verifyCertifiedGap.
GapBuildResult buildBoundedCertifiedGap(const Integer& lower, const Integer& gap,
    CertifiedGapLimits limits, const std::function<bool()>& cancelled = {});
// Larger supplied pair with independently verifiable endpoint certificates.
// No factor/endpoint search. Missing small-base interior witnesses above the
// deterministic domain mean unresolved/BudgetExceeded, never a claimed prime
// or a fabricated gap. The existing MR12-only entry point/policy is unchanged.
GapBuildResult buildCertifiedGapFromPrimeCertificates(const Integer& lower,const Integer& gap,
    const std::vector<PrimeCertificateNode>& lower_certificate,const std::vector<PrimeCertificateNode>& upper_certificate,
    CertifiedGapLimits,const std::function<bool()>& cancelled={});
// Optional untrusted witnesses for sparse interiors the local small-base
// producer cannot resolve. Offset is one-based from lower, strictly increasing.
// Existing certificate/encoding/verifier semantics do not change. Hints cannot
// omit an interior: the resulting proof still contains every gap-1 position.
struct SuppliedCompositeWitness {
  std::uint64_t offset;
  CompositeWitness witness;
};
GapBuildResult buildCertifiedGapFromPrimeCertificatesAndWitnesses(const Integer& lower,const Integer& gap,
    const std::vector<PrimeCertificateNode>& lower_certificate,const std::vector<PrimeCertificateNode>& upper_certificate,
    std::span<const SuppliedCompositeWitness> witnesses,CertifiedGapLimits,
    const std::function<bool()>& cancelled={});
} // namespace pgn::core

// MIT. Producer for existing certificate semantics, NOT a new primality rule.
#pragma once
#include "prime_certificate.h"
#include <functional>
namespace pgn::core {
enum class PrimeBuildVerdict { Built,Composite,InvalidInput,BudgetExceeded,Cancelled };
struct PrimeBuildBudget {
    CertificateLimits certificate;
    std::uint64_t trial_divisions;
    std::uint32_t witness_candidates_per_factor;
};
struct PrimeBuildResult {
    PrimeBuildVerdict verdict;
    std::optional<std::vector<PrimeCertificateNode>> evidence;
    std::uint64_t trial_divisions_used{};
    std::uint64_t witness_candidates_used{};
};
// ONE supplied number only, no endpoint/prime/range search. Bounded trial
// factoring of n-1 and existing exact MR12 leaves; Pocklington F²>n covers
// larger parent numbers. This intentionally simple producer may fail on a
// hard factorization even for a prime. BudgetExceeded is NOT compositeness or
// an integer-domain/network ceiling. No partial positive escapes failure.
// Inputs below strict MR12 retain their existing one-leaf certificate bytes.
PrimeBuildResult buildSuppliedPrimeCertificate(const Integer&,PrimeBuildBudget,
    const std::function<bool()>& cancelled={});
struct PrimeFactorPower {Integer prime;std::uint32_t exponent;};
// Untrusted externally supplied partial factorization of n-1, strictly
// increasing. Validate every leaf/divisibility/product and generate witnesses;
// no tool output or factor hint is itself a prime proof. No external process
// dependency in this library. Larger factor-certificate DAGs remain separate.
PrimeBuildResult buildSuppliedPrimeCertificateFromFactors(const Integer&,std::span<const PrimeFactorPower>,
    PrimeBuildBudget,const std::function<bool()>& cancelled={});
}

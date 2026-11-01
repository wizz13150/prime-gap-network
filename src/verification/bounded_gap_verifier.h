#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace pgn::verification {
// Sorenson/Webster, Theorem1.1, arXiv:1509.00864. Strict bound: the value at
// the boundary is composite and passes all twelve bases. Never extend this
// domain by treating a probable-prime answer as an exact positive result.
inline constexpr std::string_view PrimeBound="318665857834031151167461";
inline constexpr std::string_view VerificationMethod="MR12_STRICT_SW2015_BOUND";
enum class PrimeVerdict { PrimeWithinBound, Composite, Unsupported, InvalidInput };
struct PrimeResult {
    PrimeVerdict verdict{PrimeVerdict::InvalidInput};
    unsigned divisor{},witnessBase{};
};
bool isCanonicalDecimal(std::string_view decimal);
PrimeResult verifyPrime(std::string_view decimal);
enum class GapVerdict { VerifiedWithinBound, CompositeEndpoint, InteriorPrime,
                         Unsupported, InvalidInput, BudgetExceeded, Interrupted };
struct GapResult {
    GapVerdict verdict{GapVerdict::InvalidInput};
    std::uint64_t checkedInterior{}; // actual checks, never extrapolated coverage
    std::string firstInteriorPrime;
};
// Verification of supplied endpoints only; no search, miner, network credit,
// emission rule or source-journal mutation. Explicit resource budget is not a
// consensus limit: exhausting it cannot produce a verified/partial-positive gap.
GapResult verifyGap(std::string_view lower,std::string_view upper,
                    std::uint64_t oddInteriorBudget,const std::atomic_bool* cancelled=nullptr);
}

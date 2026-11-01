#pragma once
#include <QString>
namespace pgn::protocol {
enum class MeritDecision {InvalidInput,Unresolved,Below,AtLeast};
struct MeritWorkBudget {int maximumDecimalDigits{160};int maximumPrecisionBits{1024};};
struct MeritComparison {
 MeritDecision decision{MeritDecision::InvalidInput};
 int precisionBits{};
 // Certificate: lower/scale <= ln(p) <= upper/scale, all decimal integers.
 QString logarithmLower,logarithmUpper,scale,error;
};
// Numeric rule ONLY: consecutive primality, ticket ownership, block contents
// and admitted primality certificates are separate unpaid protocol contracts.
// No floating point, CPU search, reward curve or denomination is selected here.
// A caller work budget may yield Unresolved; it never turns uncertainty into a
// winning gap. Exact endpoints and threshold n/d survive unchanged for retry.
MeritComparison compareMerit(const QString& p,const QString& q,
                            const QString& thresholdNumerator,
                            const QString& thresholdDenominator,
                            MeritWorkBudget={});
}

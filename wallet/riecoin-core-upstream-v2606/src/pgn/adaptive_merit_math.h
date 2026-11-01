// MIT. Exact candidate controller/work arithmetic; NOT chain inputs or activation.
#pragma once
#include <arith_uint256.h>
#include <uint256.h>
#include <pgn/range_commitment.h>
#include <optional>
namespace pgn::core {
struct MeritControllerDefinition {
    uint32_t initial_q16,minimum_q16,maximum_q16,spacing_seconds,half_life_seconds;
};
inline bool MeritControllerValid(const MeritControllerDefinition& d){
    return d.minimum_q16&&d.minimum_q16<=d.initial_q16&&d.initial_q16<=d.maximum_q16&&
        d.maximum_q16<=64U*65536U&&d.spacing_seconds&&d.half_life_seconds>=d.spacing_seconds;
}
// Inputs are untrusted arithmetic DTOs. A future native reader must derive
// counts/times from WHOLE actual ancestry and immutable genesis, not a sender.
// LN2_Q16=45426 is a DEFINED rational coefficient, no floating point.
inline std::optional<uint32_t> QuoteAnchoredMeritTarget(
    const MeritControllerDefinition& d,uint32_t adjusted_scientific_count,int64_t observed_elapsed_seconds){
    if(!MeritControllerValid(d))return {};
    const Integer ideal=Integer(adjusted_scientific_count)*d.spacing_seconds;
    Integer numerator=Integer(d.initial_q16)*d.half_life_seconds+(ideal-Integer(observed_elapsed_seconds))*45426;
    const Integer low=Integer(d.minimum_q16)*d.half_life_seconds,high=Integer(d.maximum_q16)*d.half_life_seconds;
    if(numerator<=low)return d.minimum_q16;if(numerator>=high)return d.maximum_q16;
    const Integer value=numerator/d.half_life_seconds;return value.convert_to<uint32_t>();
}
inline Integer WorkInteger(const arith_uint256& n){
    const auto bytes=ArithToUint256(n);Integer out{};
    for(int i=31;i>=0;--i){out<<=8;out+=bytes.begin()[i];}return out;
}
inline std::optional<arith_uint256> WorkUint256(Integer n){
    if(n<0||(n>>256)!=0)return {};
    uint256 bytes;
    for(unsigned i=0;i<32;++i){const Integer byte=n&255;bytes.begin()[i]=byte.convert_to<unsigned char>();n>>=8;}
    return UintToArith256(bytes);
}
// Exact definition, NOT an asserted exact floor(exp(x)):
// U_q16=floor(65536*sum(k=0..256,(delta_q16/65536)^k/k!)).
// Positive coefficients give monotonicity. Domain x in[0,64], finite exact
// integers (denominator <6000bits), no data-dependent search or unpaid ambiguity.
inline std::optional<arith_uint256> QuoteScientificWorkUnits(uint32_t delta_q16){
    if(delta_q16>64U*65536U)return {};
    Integer numerator=1,denominator=1,power=1;
    for(uint32_t n=1;n<=256;++n){
        const uint32_t factor=65536U*n;power*=delta_q16;denominator*=factor;numerator*=factor;numerator+=power;
    }
    const Integer value=(numerator<<16)/denominator;return WorkUint256(value);
}
inline std::optional<arith_uint256> QuoteAdaptiveScientificWork(const arith_uint256& base,uint32_t delta_q16){
    if(base<(arith_uint256{1}<<32)||base>(arith_uint256{1}<<96))return {};
    const auto units=QuoteScientificWorkUnits(delta_q16);if(!units)return {};
    const Integer result=(WorkInteger(base)*WorkInteger(*units))>>16;return WorkUint256(result);
}
}


// MIT. Presentation/ancestry arithmetic for immutable native parameters.
#pragma once
#include <consensus/params.h>
#include <pgn/native_retarget_policy.h>
namespace pgn::core {
inline MeritControllerDefinition NativeParameterController(const Consensus::NativeProofParameters& p){
    return {p.initial_q16,p.minimum_q16,p.maximum_q16,p.spacing_seconds,p.half_life_seconds};
}
inline std::optional<arith_uint256> NativeParameterBlockWork(const CBlockIndex& index,const Consensus::NativeProofParameters& p){
    const auto kind=ClassifyAdaptiveHeader(index,NativeParameterController(p));
    if(kind==AdaptiveHeaderKind::Scientific)return QuoteAdaptiveScientificWork(UintToArith256(p.scientific_unit),index.nBits-p.minimum_q16);
    if(kind==AdaptiveHeaderKind::Bootstrap&&index.nBits==p.bootstrap_bits)return arith_uint256{1};
    return {};
}
inline std::optional<double> NativeParameterDifficulty(const CBlockIndex& index,const Consensus::NativeProofParameters& p){
    const auto kind=ClassifyAdaptiveHeader(index,NativeParameterController(p));
    if(kind==AdaptiveHeaderKind::Scientific)return double(index.nBits)/65536.;
    if(kind!=AdaptiveHeaderKind::Bootstrap||index.nBits!=p.bootstrap_bits)return {};
    const auto target=CanonicalBootstrapHashTarget(index.nBits),reference=CanonicalBootstrapHashTarget(0x1d00ffff);
    return target&&reference?std::optional{reference->getdouble()/target->getdouble()}:std::nullopt;
}
}

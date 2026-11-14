// Encode the exact authenticated proof threshold once for native headers.
#pragma once
#include <pgn/chain_science_policy.h>
#include <optional>
#include <cstdint>
namespace pgn::core {
inline std::optional<uint32_t> EncodeNativeScienceTarget(const CommittedGapPolicy& evidence) {
    if(evidence.target_numerator<=0||evidence.target_denominator<=0)return {};
    const Integer q=(evidence.target_numerator*65536+evidence.target_denominator-1)/evidence.target_denominator;
    if(q<1||q>64U*65536U)return {};
    return q.convert_to<uint32_t>();
}
inline std::optional<uint32_t> NativeScientificHeaderTarget(const ChainSciencePolicy& policy) {
    return policy.EffectiveParent().IsNull()?std::optional<uint32_t>{0}:EncodeNativeScienceTarget(policy.Definition().evidence);
}
}

// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <pgn/vanity_key_completion.h>
#include <secp256k1.h>
#include <support/allocators/secure.h>

#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace pgn::core {
CKey CompleteVanityPublicMatch(const CKey& seed,const NativeAddressPolicy& policy,
    std::string_view prefix,const VanityPublicMatch& match) {
    ValidateVanityPrefix(policy,prefix);
    if(!seed.IsValid()||!seed.IsCompressed())throw std::invalid_argument("A valid compressed wallet-local seed is required");
    bool verified=false;
    const auto observed=SearchVanityPublic(seed.GetPubKey(),policy,prefix,match.step,1,{},[&](const auto& expected){
        verified=expected.step==match.step&&expected.public_key==match.public_key&&expected.address==match.address;
        return false;
    });
    if(!verified||observed.matches!=1)throw std::invalid_argument("Untrusted vanity match does not belong to this seed, policy and prefix");
    // No secret is copied until the public worker's entire assertion passes.
    // secure_allocator cleanses on free; CKey also uses Core secure allocation.
    std::vector<unsigned char,secure_allocator<unsigned char>> bytes(32);
    std::memcpy(bytes.data(),seed.data(),bytes.size());
    std::array<unsigned char,32> tweak{};auto step=match.step;
    for(size_t i=32;i-->24;){tweak[i]=uint8_t(step);step>>=8;}
    if(!secp256k1_ec_seckey_tweak_add(secp256k1_context_static,bytes.data(),tweak.data()))
        throw std::logic_error("Verified public match has no corresponding private key");
    CKey completed;completed.Set(bytes.begin(),bytes.end(),true);
    if(!completed.IsValid()||completed.GetPubKey()!=match.public_key)
        throw std::logic_error("Core private/public vanity completion mismatch");
    return completed;
}
} // namespace pgn::core

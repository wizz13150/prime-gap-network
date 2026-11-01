// Copyright (c) 2026 The PGN developers. MIT.
#include <pgn/vanity_orbit_completion.h>
#include <pgn/vanity_public_search.h>
#include <addresstype.h>
#include <key_io.h>
#include <secp256k1.h>
#include <support/allocators/secure.h>
#include <array>
#include <cstring>
#include <stdexcept>
namespace pgn::core {
namespace {
// Public secp256k1 eigenvalue, also present in pinned BitCrack cudaMath. Scalar
// multiplication/negation is performed by actual Core libsecp256k1, never by a
// new home-made big-integer secret implementation.
constexpr std::array<unsigned char,32> LAMBDA={
    0x53,0x63,0xad,0x4c,0xc0,0x5c,0x30,0xe0,0xa5,0x26,0x1c,0x02,0x88,0x12,0x64,0x5a,
    0x12,0x2e,0x22,0xea,0x20,0x81,0x66,0x78,0xdf,0x02,0x96,0x7c,0x1b,0x23,0xbd,0x72};
std::array<unsigned char,32> Step(uint64_t value){std::array<unsigned char,32>b{};for(size_t i=32;i-->24;){b[i]=uint8_t(value);value>>=8;}return b;}
void CheckOwner(const CKey& seed,const VanityOrbitMatch& match) {
    if(!seed.IsValid()||!seed.IsCompressed())throw std::invalid_argument("Valid compressed Core wallet seed required");
    const auto expected=DeriveVanityOrbitPublic(seed.GetPubKey(),match.step,match.endomorphism,match.negated);
    if(!expected||*expected!=match.public_key)throw std::invalid_argument("Untrusted orbit point/step/branch does not match this owner");
}
CKey CompleteChecked(const CKey& seed,const VanityOrbitMatch& match) {
    // Called only after owner AND actual full address/prefix checks. Core secure
    // allocations cleanse temporary bytes and CKey on free; no secret strings.
    std::vector<unsigned char,secure_allocator<unsigned char>> bytes(32);
    std::memcpy(bytes.data(),seed.data(),32);const auto tweak=Step(match.step);
    if(!secp256k1_ec_seckey_tweak_add(secp256k1_context_static,bytes.data(),tweak.data()))
        throw std::logic_error("Verified orbit unexpectedly has infinite base");
    for(unsigned j=0;j<match.endomorphism;++j)
        if(!secp256k1_ec_seckey_tweak_mul(secp256k1_context_static,bytes.data(),LAMBDA.data()))throw std::logic_error("Core orbit scalar multiply failed");
    if(match.negated&&!secp256k1_ec_seckey_negate(secp256k1_context_static,bytes.data()))throw std::logic_error("Core orbit scalar negate failed");
    CKey completed;completed.Set(bytes.begin(),bytes.end(),true);
    if(!completed.IsValid()||completed.GetPubKey()!=match.public_key)throw std::logic_error("Core orbit private/public completion mismatch");
    return completed;
}
}
std::optional<CPubKey> DeriveVanityOrbitPublic(const CPubKey& seed,uint64_t step,unsigned endomorphism,bool negated) {
    if(endomorphism>2||seed.size()!=33||!seed.IsFullyValid())throw std::invalid_argument("Invalid orbit branch or public seed");
    secp256k1_pubkey point{};
    if(!secp256k1_ec_pubkey_parse(secp256k1_context_static,&point,seed.begin(),seed.size()))throw std::logic_error("Core public parse failed");
    const auto tweak=Step(step);
    if(!secp256k1_ec_pubkey_tweak_add(secp256k1_context_static,&point,tweak.data()))return std::nullopt;
    for(unsigned j=0;j<endomorphism;++j)
        if(!secp256k1_ec_pubkey_tweak_mul(secp256k1_context_static,&point,LAMBDA.data()))throw std::logic_error("Core orbit public multiply failed");
    if(negated&&!secp256k1_ec_pubkey_negate(secp256k1_context_static,&point))throw std::logic_error("Core public negate failed");
    std::array<unsigned char,33>b{};size_t size=b.size();
    if(!secp256k1_ec_pubkey_serialize(secp256k1_context_static,b.data(),&size,&point,SECP256K1_EC_COMPRESSED)||size!=b.size())throw std::logic_error("Core public serialize failed");
    return CPubKey(b.begin(),b.end());
}
CKey CompleteVanityOrbitMatch(const CKey& seed,const NativeAddressPolicy& policy,std::string_view prefix,const VanityOrbitMatch& match) {
    ValidateVanityPrefix(policy,prefix);CheckOwner(seed,match);
    const auto expected=EncodeDestination(WitnessV0KeyHash(match.public_key),policy);
    std::string error;const auto destination=DecodeDestination(match.address,policy,error);
    if(expected!=match.address||!match.address.starts_with(prefix)||!error.empty()||
       GetScriptForDestination(destination)!=GetScriptForDestination(WitnessV0KeyHash(match.public_key)))
        throw std::invalid_argument("Orbit full witness address/prefix failed Core validation");
    return CompleteChecked(seed,match);
}
CKey CompleteVanityOrbitMatch(const CKey& seed,const LegacyVanityPlan& plan,const VanityOrbitMatch& match) {
    // Recompile untrusted derived intervals: only the explicit caller-bound
    // policy/prefix matters for final actual Core address validation.
    const auto checked_plan=CompileLegacyVanityPrefix(plan.policy,plan.prefix);CheckOwner(seed,match);
    if(!VerifyLegacyVanityCandidate(match.public_key,checked_plan,match.address))throw std::invalid_argument("Orbit full Base58 address/prefix failed Core validation");
    return CompleteChecked(seed,match);
}
}

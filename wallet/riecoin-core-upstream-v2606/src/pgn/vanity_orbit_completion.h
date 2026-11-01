// Copyright (c) 2026 The PGN developers. MIT.
#pragma once
#include <pgn/vanity_prefix_plan.h>
#include <key.h>
#include <optional>
namespace pgn::core {
// Describes one of six secp256k1 symmetry/endomorphism candidates. Metadata is
// untrusted: k' = (-1)^negated * lambda^endomorphism * (seed+step) mod n.
// No entropy reduction, GPU secrets or custom curve arithmetic is introduced.
struct VanityOrbitMatch {
    uint64_t step;
    unsigned endomorphism;
    bool negated;
    CPubKey public_key;
    std::string address;
};
std::optional<CPubKey> DeriveVanityOrbitPublic(const CPubKey& seed,uint64_t step,unsigned endomorphism,bool negated);
CKey CompleteVanityOrbitMatch(const CKey& seed,const NativeAddressPolicy& policy,
    std::string_view prefix,const VanityOrbitMatch& match);
CKey CompleteVanityOrbitMatch(const CKey& seed,const LegacyVanityPlan& plan,const VanityOrbitMatch& match);
}

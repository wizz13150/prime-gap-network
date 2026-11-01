// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license.
#pragma once
#include <pgn/nativeaddresspolicy.h>
#include <pubkey.h>
#include <array>
#include <string>
#include <string_view>
#include <vector>
namespace pgn::core {
struct VanityHashRange { std::array<uint32_t,5> low,high; };
// Pure presentation candidate, never a selected network/wallet policy. Required
// fields deliberately have no defaults. Reuse Core Base58Check, not new crypto.
struct LegacyVanityPolicy { std::vector<unsigned char> keyhash_version; std::string required_namespace; };
struct LegacyVanityPlan { LegacyVanityPolicy policy; std::string prefix; std::vector<VanityHashRange> ranges; };
// GPU intervals conservatively include checksum boundary hashes. Every hit
// MUST pass the actual codec/full prefix afterwards. No possible match is lost.
LegacyVanityPlan CompileLegacyVanityPrefix(const LegacyVanityPolicy&,std::string_view prefix);
// General version-family construction, NOT a name whitelist or deployed
// decoder. Enumerates all matching version metadata within explicit admission.
// A boundary version may also encode non-namespace hashes; those are rejected
// by the candidate encoder/validator. Wallet type/canonical alias selection is
// still a separate network policy decision, never performed by this function.
std::vector<LegacyVanityPlan> PlanLegacyNamespacePrefix(std::string_view required_namespace,
    size_t version_bytes,std::string_view prefix,size_t max_version_candidates);
std::vector<VanityHashRange> CompileWitnessVanityPrefix(const NativeAddressPolicy&,std::string_view prefix);
std::string EncodeLegacyVanityCandidate(const CPubKey&,const LegacyVanityPolicy&);
bool VerifyLegacyVanityCandidate(const CPubKey&,const LegacyVanityPlan&,std::string_view address);
}

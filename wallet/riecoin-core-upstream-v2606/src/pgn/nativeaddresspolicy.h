// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pgn::core {
enum class NativeWitnessChecksum { Bip173And350 };
enum class NativeWitnessCase { Lower, Upper };
/** Explicit witness-only presentation metadata. No network, approval, key
 * ownership or wallet-receivability token; no legacy/raw-script fallback.
 * All fields required. The selected policy is absent in every stock factory. */
struct NativeKeyHashAlias { std::vector<unsigned char> version; std::string required_namespace; };
struct NativeAddressPolicy {
    std::string hrp;
    NativeWitnessChecksum checksum;
    NativeWitnessCase presentation;
    std::size_t max_characters;
    std::optional<NativeKeyHashAlias> keyhash_alias;
    NativeAddressPolicy(std::string h, NativeWitnessChecksum c, NativeWitnessCase p, std::size_t budget, std::optional<NativeKeyHashAlias> alias = std::nullopt)
        : hrp{std::move(h)}, checksum{c}, presentation{p}, max_characters{budget}, keyhash_alias{std::move(alias)} {}
};
enum class NativeAddressPolicyStatus { Ready, MissingPolicy, InvalidPolicy, UnsupportedWalletType, InsufficientBudget };
class NativeAddressPolicyError : public std::runtime_error {
public:
    explicit NativeAddressPolicyError() : std::runtime_error("PGN native witness address policy is unavailable or invalid") {}
};
inline NativeAddressPolicyStatus ValidateNativeAddressPolicy(const NativeAddressPolicy& policy)
{
    if (policy.hrp.empty() || policy.hrp.size() > 83 || policy.max_characters > 90 ||
        policy.max_characters == 0 || policy.checksum != NativeWitnessChecksum::Bip173And350 ||
        (policy.presentation != NativeWitnessCase::Lower && policy.presentation != NativeWitnessCase::Upper))
        return NativeAddressPolicyStatus::InvalidPolicy;
    for (unsigned char c : policy.hrp)
        if (c < 33 || c > 126 || (c >= 'A' && c <= 'Z')) return NativeAddressPolicyStatus::InvalidPolicy;
    if (policy.keyhash_alias) {
        const auto& a=*policy.keyhash_alias;
        if (a.version.empty() || a.version.size()>4 || a.required_namespace.empty() || a.required_namespace.size()>16)
            return NativeAddressPolicyStatus::InvalidPolicy;
        for (char c:a.required_namespace)
            if (std::string{"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"}.find(c)==std::string::npos)
                return NativeAddressPolicyStatus::InvalidPolicy;
    }
    return NativeAddressPolicyStatus::Ready;
}
inline NativeAddressPolicyStatus ValidateNativeAddressPolicy(const std::optional<NativeAddressPolicy>& policy)
{
    return policy ? ValidateNativeAddressPolicy(*policy) : NativeAddressPolicyStatus::MissingPolicy;
}
/** Including HRP, separator, version and6checksum symbols. Validated HRP and
 * program2..40bytes keep the bounded length calculation overflow-free. */
inline bool NativeWitnessProgramFits(const NativeAddressPolicy& policy, std::size_t bytes)
{
    return ValidateNativeAddressPolicy(policy) == NativeAddressPolicyStatus::Ready && bytes >= 2 && bytes <= 40 &&
        policy.hrp.size() + 8 + (bytes * 8 + 4) / 5 <= policy.max_characters;
}
// Explicit BASE presentation choice; one network version for every name.
inline NativeAddressPolicy BasePgnAddressPolicy() {
    return {"pgn",NativeWitnessChecksum::Bip173And350,NativeWitnessCase::Lower,90,
            NativeKeyHashAlias{{0x0c,0x89},"PGN"}};
}
} // namespace pgn::core

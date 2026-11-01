// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <pgn/nativeaddresspolicy.h>
#include <pubkey.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace pgn::core {
/** PUBLIC split-key search primitive, not wallet key creation or import.
 * Examines P_i = seed + i*G using the Core's own libsecp256k1 and witness codec.
 * The wallet must retain its seed secret, independently reconstruct and verify
 * a winning private key before its own atomic import. No secret enters this API.
 * Explicit witness-v0 keyhash policy/prefix only; no network or policy defaults.
 */
struct VanityPublicMatch {
    uint64_t step;
    CPubKey public_key;
    std::string address;
};
struct VanityPublicResult {
    uint64_t visited_slots{};
    uint64_t hashed_public_keys{};
    uint64_t encoded_addresses{};
    uint64_t matches{};
    uint64_t infinity_slots{};
    bool cancelled{};
    bool callback_stopped{};
};

/** Reject impossible fixed HRP/version/case/alphabet/length and known payload-
 * checksum contradictions BEFORE curve work. Does not predict search duration.
 * Throws invalid_argument on invalid prefix/policy. No files, keys or GPU use.
 */
void ValidateVanityPrefix(const NativeAddressPolicy& policy, std::string_view prefix);

/** Exact half-open NUMBER OF SLOTS starting at first_step, not a wrapping range.
 * Cancellation is sampled before each slot. Return false from on_match to stop.
 * Infinity is one skipped slot, never an address or duplicate of its successor.
 * Disjoint ranges for parallel workers are the caller's responsibility.
 * No wallet/provider/global policy selection, private scalars, I/O or threads.
 */
VanityPublicResult SearchVanityPublic(const CPubKey& seed, const NativeAddressPolicy& policy,
    std::string_view prefix, uint64_t first_step, uint64_t slots,
    const std::function<bool()>& cancelled,
    const std::function<bool(const VanityPublicMatch&)>& on_match);
} // namespace pgn::core

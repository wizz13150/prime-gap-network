// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <pgn/vanity_public_search.h>
#include <key.h>

namespace pgn::core {
/** WALLET-LOCAL completion of an untrusted public worker result.
 * Recompute against this exact seed/policy/prefix BEFORE handling secret bytes.
 * Use libsecp256k1 scalar addition and Core CKey's secure allocation, never a
 * private-key string. No entropy source, key export, wallet import or I/O here.
 * A returned key proves the public relation, NOT wallet/network authorization;
 * normal Core policy, descriptor, unlock and atomic import gates still apply.
 */
CKey CompleteVanityPublicMatch(const CKey& seed,const NativeAddressPolicy& policy,
    std::string_view prefix,const VanityPublicMatch& match);
} // namespace pgn::core

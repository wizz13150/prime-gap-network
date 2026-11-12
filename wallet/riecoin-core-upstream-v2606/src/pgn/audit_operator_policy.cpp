// Copyright (c) 2026 The PGN developers. MIT.
#include <pgn/audit_operator_policy.h>
namespace pgn::core {
std::optional<NativeAuditOperatorRules> ConfiguredNativeAuditOperatorRules(const uint256& chain)
{
    // Only the current PGN public testnet. Historical candidate heights keep
    // their original assignment and distinct-operator validation rules.
    static const auto genesis=*uint256::FromHex("0ccd1f2349ed75f9cf4d0279871a204e107683e5baa1dd6641c4e6f08a25a17d");
    if(chain!=genesis)return std::nullopt;
    return NativeAuditOperatorRules{genesis,16640};
}
}

// Copyright (c) 2026 The PGN developers. MIT.
#pragma once
#include <uint256.h>
#include <optional>
namespace pgn::core {
// A network rule, not a caller-supplied claim that the other miners are absent.
struct NativeAuditOperatorRules {
    uint256 chain;
    int32_t activation_height{}; // First candidate-block height using the rule.
    bool operator==(const NativeAuditOperatorRules&) const = default;
};
// No CLI/environment override. An absent activation preserves legacy rules.
std::optional<NativeAuditOperatorRules> ConfiguredNativeAuditOperatorRules(const uint256& chain);
}

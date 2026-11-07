// Copyright (c) 2026 The PGN developers. MIT.
// Network consensus parameter table. No CLI/config/environment activation.
#pragma once
#include <pgn/audit_settlement_activation.h>
#include <pgn/testnet_service_activation.h>
namespace pgn::core {
inline std::optional<AuditSettlementActivation> ConfiguredAuditSettlementActivation(const uint256& chain){
    const auto pin=PgnTestnetServiceActivation(chain);
    if(!pin)return {};
    return AuditSettlementActivation{chain,pin->genesis,pin->monetary_policy,pin->genesis,
        0,pin->height,1,1,1,pin->page_outputs,true};
}
}

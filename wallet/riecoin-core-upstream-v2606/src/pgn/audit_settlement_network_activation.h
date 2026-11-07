// Copyright (c) 2026 The PGN developers. MIT.
// Network consensus parameter table. No CLI/config/environment activation.
#pragma once
#include <pgn/audit_settlement_activation.h>
namespace pgn::core {
inline std::optional<AuditSettlementActivation> ConfiguredAuditSettlementActivation(){
    // Production deliberately disabled: no H/A or policy approval is invented.
    return {};
}
}

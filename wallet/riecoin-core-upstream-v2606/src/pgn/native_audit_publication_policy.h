// Copyright (c) 2026 The PGN developers. MIT.
// Candidate policy: deliberately inactive until explicit network review.
#pragma once
#include <uint256.h>
#include <pgn/native_audit_qa_identity.h>
#include <optional>
namespace pgn::core {
// Proposed response window after both commitments, not a calculation lease.
// This value is a review candidate, not an activated economic expiry.
inline constexpr int64_t NativeAuditOwnerWaitSeconds=600;
inline std::optional<int32_t> ConfiguredNativeAuditPublicationHeight(const uint256& chain){
    if(chain==NativeAuditQaGenesis())return 1; // Synthetic QA only, fixed by genesis.
    return {}; // No activation is authorized by a local runtime or caller.
}
}

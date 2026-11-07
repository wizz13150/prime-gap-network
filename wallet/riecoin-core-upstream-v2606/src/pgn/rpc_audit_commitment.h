// Copyright (c) 2026 The PGN developers. MIT.
#pragma once
#include <pgn/native_report_audit.h>
#include <optional>
#include <string_view>

namespace pgn::core {
// The digest was already published by AuditCommit. Expected counts, reveal
// bodies and nonces remain private until the existing disclosure rules permit.
inline std::optional<std::string_view> RpcAuditCommitment(const NativeAuditJob& job)
{
    if (job.stage == NativeAuditJob::Assigned || job.commitment.empty()) return std::nullopt;
    return job.commitment;
}
}

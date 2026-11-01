// Copyright (c) 2026 The PGN developers. MIT.
#pragma once
#include <pgn/work_lease_clock.h>
namespace pgn::core {
// Only the assignment expires. The report/audit request stays in the ledger.
// Legacy height leases remain interpretable; newly versioned leases can use
// median chain time so coordination-record throughput cannot shorten work.
using AuditAssignmentLease=WorkLease;
inline std::optional<AuditAssignmentLease> auditTimeLease(std::int64_t issued,std::int64_t seconds) {
    return workTimeLease(issued,seconds);
}
}

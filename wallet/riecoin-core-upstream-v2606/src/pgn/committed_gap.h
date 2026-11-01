#pragma once
#include "certified_gap.h"
#include "certified_gap_encoding.h"
#include "merit_arithmetic.h"

namespace pgn::core {
struct CommittedGapEvidence {
  SegmentResult segment;
  SegmentProof inclusion;
  std::uint64_t record_index;
  CertifiedGap scientific;
};
struct CommittedGapPolicy {
  EncodingLimits encoding;
  CertifiedGapLimits scientific;
  ArithmeticLimits arithmetic;
  LogAccuracy accuracy;
  Integer target_numerator;
  Integer target_denominator;
};
enum class CommittedGapVerdict {
  VerifiedEvidenceAtTarget,
  InvalidCommitment,
  InvalidGapEvidence,
  BelowTarget,
  IndeterminateMerit,
  BudgetExceeded,
  InvalidPolicy,
  InvalidEncoding
};
struct CommittedGapCheck {
  CommittedGapVerdict verdict;
  std::uint64_t interior_positions_examined;
};
// Candidate composition, NOT chain admission. Caller must obtain the actual
// ticket/range, committed report root and explicit policy from trusted chain
// state. This does not establish ticket eligibility, report completeness,
// coverage, declared merit_class validity, reward, difficulty or fork weight.
CommittedGapCheck verifyCommittedGap(const RangeContract &,
                                     const Digest &expected_report_root,
                                     const CommittedGapEvidence &,
                                     const CommittedGapPolicy &,
                                     const Hasher &);
struct EncodedCommittedGapEvidence {
  Bytes segment_bytes;
  SegmentProof inclusion;
  std::uint64_t record_index;
  Bytes scientific_bytes;
};
// First peer/disk-byte boundary. Report and scientific proof have separately
// explicit encoding budgets. Canonical parsing then invokes the same scientific
// and merit verifier; no parse-only or trusted-sender acceptance path exists.
CommittedGapCheck verifyEncodedCommittedGap(
    const RangeContract &, const Digest &expected_report_root,
    const EncodedCommittedGapEvidence &, const CommittedGapPolicy &,
    const EncodingLimits &proof_encoding, const Hasher &);
} // namespace pgn::core

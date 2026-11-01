#include "committed_gap.h"
#include <boost/multiprecision/integer.hpp>
#include <stdexcept>
#include <utility>

namespace pgn::core {
namespace {
bool admitted(const Integer &value, std::uint32_t bits) {
  return value > 0 && bits && boost::multiprecision::msb(value) < bits;
}
bool validPolicy(const CommittedGapPolicy &policy) {
  const auto &limits = policy.arithmetic;
  const auto &accuracy = policy.accuracy;
  return limits.max_input_bits && limits.max_fractional_bits >= 4 &&
         limits.max_series_terms && accuracy.fractional_bits >= 4 &&
         accuracy.fractional_bits <= limits.max_fractional_bits &&
         accuracy.series_terms &&
         accuracy.series_terms <= limits.max_series_terms &&
         admitted(policy.target_numerator, limits.max_input_bits) &&
         admitted(policy.target_denominator, limits.max_input_bits);
}
} // namespace
CommittedGapCheck verifyCommittedGap(const RangeContract &range,
                                     const Digest &expected_report_root,
                                     const CommittedGapEvidence &evidence,
                                     const CommittedGapPolicy &policy,
                                     const Hasher &hash) {
  if (!validPolicy(policy))
    return {CommittedGapVerdict::InvalidPolicy, 0};
  if (evidence.record_index >= evidence.segment.records.size() ||
      !verifySegment(range, evidence.segment, evidence.inclusion,
                     expected_report_root, policy.encoding, hash))
    return {CommittedGapVerdict::InvalidCommitment, 0};
  const auto &record = evidence.segment.records[evidence.record_index];
  if (record.lower != evidence.scientific.lower ||
      record.gap != evidence.scientific.gap)
    return {CommittedGapVerdict::InvalidGapEvidence, 0};
  if (!admitted(record.lower, policy.arithmetic.max_input_bits) ||
      !admitted(record.gap, policy.arithmetic.max_input_bits))
    return {CommittedGapVerdict::BudgetExceeded, 0};
  const auto scientific =
      verifyCertifiedGap(evidence.scientific, policy.scientific);
  if (scientific.verdict == CertifiedGapVerdict::BudgetExceeded)
    return {CommittedGapVerdict::BudgetExceeded, 0};
  if (scientific.verdict != CertifiedGapVerdict::VerifiedConsecutiveGap)
    return {CommittedGapVerdict::InvalidGapEvidence,
            scientific.interior_positions_examined};
  const auto merit = compareMerit(
      record.lower, record.gap, policy.target_numerator,
      policy.target_denominator, policy.accuracy, policy.arithmetic);
  const auto verdict = merit == MeritDecision::Meets
                           ? CommittedGapVerdict::VerifiedEvidenceAtTarget
                       : merit == MeritDecision::Below
                           ? CommittedGapVerdict::BelowTarget
                           : CommittedGapVerdict::IndeterminateMerit;
  return {verdict, scientific.interior_positions_examined};
}
CommittedGapCheck verifyEncodedCommittedGap(
    const RangeContract &range, const Digest &expected_report_root,
    const EncodedCommittedGapEvidence &evidence,
    const CommittedGapPolicy &policy, const EncodingLimits &proof_encoding,
    const Hasher &hash) {
  if (!validPolicy(policy))
    return {CommittedGapVerdict::InvalidPolicy, 0};
  try {
    auto segment =
        decodeSegment(range, evidence.segment_bytes, policy.encoding);
    auto scientific = decodeCertifiedGap(evidence.scientific_bytes,
                                         {proof_encoding, policy.scientific});
    return verifyCommittedGap(range, expected_report_root,
                              {std::move(segment), evidence.inclusion,
                               evidence.record_index, std::move(scientific)},
                              policy, hash);
  } catch (const std::invalid_argument &) {
    return {CommittedGapVerdict::InvalidEncoding, 0};
  }
}
} // namespace pgn::core

#pragma once
#include <array>
#include <boost/multiprecision/cpp_int.hpp>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace pgn::core {
using Integer = boost::multiprecision::cpp_int;
using Bytes = std::vector<std::uint8_t>;
using Digest = std::array<std::uint8_t, 32>;
using Hasher = std::function<Digest(std::span<const std::uint8_t>)>;

// These are explicit admission limits, not inherited network defaults.
struct EncodingLimits {
  std::uint64_t max_records;
  std::uint64_t max_encoded_bytes;
  std::uint32_t max_integer_bytes;
  std::uint32_t max_segments;
};
struct RangeContract {
  Digest ticket_id;
  Integer start;
  Integer length;
  std::uint32_t segments;
};
struct GapDeclaration {
  Integer lower;
  Integer gap;
  std::uint32_t merit_class;
  bool operator==(const GapDeclaration &) const = default;
};
struct SegmentResult {
  std::uint32_t index;
  std::vector<GapDeclaration> records;
};
struct RangeCommitment {
  Digest root;
  std::uint64_t records;
  std::vector<Digest> leaves;
};
struct SegmentProof {
  std::uint32_t index;
  std::vector<Digest> siblings;
};

// Candidate encoding V1 for exact closed tests. This is NOT an approved
// mainnet schema, a primality/consecutiveness proof, or a coverage certificate.
// Merit classification and admission of a protocol ticket remain independent
// consensus obligations. Hash selection is explicit; no hash-PoW is imported.
std::pair<Integer, Integer> segmentBounds(const RangeContract &, std::uint32_t);
Bytes encodeSegment(const RangeContract &, const SegmentResult &,
                    const EncodingLimits &);
SegmentResult decodeSegment(const RangeContract &,
                            std::span<const std::uint8_t>,
                            const EncodingLimits &);
RangeCommitment commitRange(const RangeContract &,
                            const std::vector<SegmentResult> &,
                            const EncodingLimits &, const Hasher &);
// Generation is O(N); verification is O(encoded segment bytes + log N).
// Inclusion proves committed bytes, not scientific validity or completeness.
SegmentProof proveSegment(const RangeContract &,
                          const std::vector<SegmentResult> &, std::uint32_t,
                          const EncodingLimits &, const Hasher &);
bool verifySegment(const RangeContract &, const SegmentResult &,
                   const SegmentProof &, const Digest &expected_root,
                   const EncodingLimits &, const Hasher &);
} // namespace pgn::core

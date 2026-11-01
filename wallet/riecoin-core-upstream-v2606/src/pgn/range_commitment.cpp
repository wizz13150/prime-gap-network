#include "range_commitment.h"
#include "canonical_encoding.h"
#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace pgn::core {
namespace {
constexpr std::string_view segmentDomain{"PGN segment result V1"};
constexpr std::string_view nodeDomain{"PGN result node V1"};
constexpr std::string_view rootDomain{"PGN range root V1"};
void require(bool valid, const char *reason) {
  if (!valid)
    throw std::invalid_argument(reason);
}
void validate(const RangeContract &range, const EncodingLimits &limits) {
  require(range.start >= 2 && range.length > 0 && range.segments > 0 &&
              range.length >= range.segments,
          "Invalid half-open range or empty segment");
  require(limits.max_records > 0 && limits.max_encoded_bytes > 0 &&
              limits.max_integer_bytes > 0 && limits.max_segments > 0 &&
              range.segments <= limits.max_segments,
          "Explicit encoding limits required");
}
using detail::Reader;
using detail::Writer;
void identity(Writer &writer, const RangeContract &range) {
  writer.raw(range.ticket_id);
  writer.integer(range.start);
  writer.integer(range.length);
  writer.count(range.segments);
}
Digest parentHash(const Digest &left, const Digest &right,
                  const EncodingLimits &limits, const Hasher &hash) {
  Writer writer{{}, limits};
  writer.domain(nodeDomain);
  writer.raw(left);
  writer.raw(right);
  return hash(writer.data);
}
Digest rootHash(const RangeContract &range, const Digest &tree,
                const EncodingLimits &limits, const Hasher &hash) {
  Writer root{{}, limits};
  root.domain(rootDomain);
  identity(root, range);
  root.raw(tree);
  return hash(root.data);
}
std::vector<Digest> parentLevel(const std::vector<Digest> &level,
                                const EncodingLimits &limits,
                                const Hasher &hash) {
  std::vector<Digest> next;
  next.reserve(level.size() / 2 + level.size() % 2);
  for (std::size_t i = 0; i < level.size(); i += 2)
    next.push_back(parentHash(
        level[i], level[std::min(i + 1, level.size() - 1)], limits, hash));
  return next;
}
} // namespace
std::pair<Integer, Integer> segmentBounds(const RangeContract &range,
                                          std::uint32_t index) {
  require(range.start >= 2 && range.length > 0 && range.segments > 0 &&
              range.length >= range.segments && index < range.segments,
          "Invalid segment identity");
  // Integer partitions cover the entire range, including non-divisible
  // lengths, without an arbitrary fixed ticket size or lost final residue.
  return {range.start + range.length * index / range.segments,
          range.start + range.length * (Integer(index) + 1) / range.segments};
}
Bytes encodeSegment(const RangeContract &range, const SegmentResult &segment,
                    const EncodingLimits &limits) {
  validate(range, limits);
  const auto [first, end] = segmentBounds(range, segment.index);
  require(segment.records.size() <= limits.max_records,
          "Too many segment records");
  Writer writer{{}, limits};
  writer.domain(segmentDomain);
  identity(writer, range);
  writer.count(segment.index);
  writer.count(segment.records.size());
  Integer previous = first;
  bool initial = true;
  for (const auto &record : segment.records) {
    require(record.lower >= first && record.lower < end && record.gap > 0,
            "Record not owned by half-open segment");
    require(initial ? record.lower >= previous : record.lower > previous,
            "Records must be strictly ordered and unique");
    writer.integer(record.lower - previous);
    writer.integer(record.gap);
    writer.count(record.merit_class);
    // Ownership is decided ONLY by the lower endpoint. Its upper endpoint
    // may cross a segment or range boundary and must not be dropped.
    previous = record.lower;
    initial = false;
  }
  return std::move(writer.data);
}
SegmentResult decodeSegment(const RangeContract &range,
                            std::span<const std::uint8_t> bytes,
                            const EncodingLimits &limits) {
  validate(range, limits);
  require(bytes.size() <= limits.max_encoded_bytes,
          "Encoded segment exceeds byte limit");
  Reader reader{bytes, limits};
  reader.domain(segmentDomain);
  require(reader.digest() == range.ticket_id, "Wrong protocol ticket");
  const auto start = reader.integer(), length = reader.integer();
  require(start == range.start && length == range.length &&
              reader.count() == range.segments,
          "Wrong range identity");
  const auto index = reader.count();
  require(index < range.segments, "Invalid segment index");
  const auto count = reader.count();
  require(count <= limits.max_records &&
              count <= (bytes.size() - reader.position) / 3,
          "Record count exceeds input or limit");
  SegmentResult result{static_cast<std::uint32_t>(index), {}};
  result.records.reserve(static_cast<std::size_t>(count));
  const auto [first, end] = segmentBounds(range, result.index);
  Integer previous = first;
  for (std::uint64_t i = 0; i < count; ++i) {
    const auto delta = reader.integer(), gap = reader.integer();
    const auto merit = reader.count();
    require(i == 0 || delta > 0, "Duplicate or unordered lower endpoint");
    Integer lower = previous + delta;
    require(lower >= first && lower < end && gap > 0 &&
                merit <= std::numeric_limits<std::uint32_t>::max(),
            "Invalid gap record");
    result.records.push_back({lower, gap, static_cast<std::uint32_t>(merit)});
    previous = lower;
  }
  require(reader.position == bytes.size(), "Trailing segment bytes");
  return result;
}
RangeCommitment commitRange(const RangeContract &range,
                            const std::vector<SegmentResult> &segments,
                            const EncodingLimits &limits, const Hasher &hash) {
  validate(range, limits);
  require(static_cast<bool>(hash), "Explicit hash provider required");
  require(segments.size() == range.segments,
          "Every segment, including empty ones, must be committed");
  RangeCommitment result{{}, 0, {}};
  result.leaves.reserve(segments.size());
  for (std::size_t i = 0; i < segments.size(); ++i) {
    require(segments[i].index == i, "Missing, duplicate or unordered segment");
    require(segments[i].records.size() <=
                std::numeric_limits<std::uint64_t>::max() - result.records,
            "Record count overflow");
    result.records += segments[i].records.size();
    const auto bytes = encodeSegment(range, segments[i], limits);
    result.leaves.push_back(hash(bytes));
  }
  auto level = result.leaves;
  while (level.size() > 1)
    level = parentLevel(level, limits, hash);
  // Bind ticket, range and leaf count again at the root. Odd-leaf duplication
  // cannot turn a different segment count or range into the same commitment.
  result.root = rootHash(range, level.front(), limits, hash);
  return result;
}
SegmentProof proveSegment(const RangeContract &range,
                          const std::vector<SegmentResult> &segments,
                          std::uint32_t index, const EncodingLimits &limits,
                          const Hasher &hash) {
  require(index < range.segments, "Invalid proof index");
  auto level = commitRange(range, segments, limits, hash).leaves;
  SegmentProof proof{index, {}};
  std::size_t position = index;
  while (level.size() > 1) {
    proof.siblings.push_back(
        level[std::min(position ^ std::size_t(1), level.size() - 1)]);
    level = parentLevel(level, limits, hash);
    position /= 2;
  }
  return proof;
}
bool verifySegment(const RangeContract &range, const SegmentResult &segment,
                   const SegmentProof &proof, const Digest &expected_root,
                   const EncodingLimits &limits, const Hasher &hash) {
  try {
    validate(range, limits);
    require(static_cast<bool>(hash), "Explicit hash provider required");
    require(proof.index == segment.index && proof.index < range.segments,
            "Wrong proof index");
    std::size_t height{};
    for (std::uint64_t width = range.segments; width > 1;
         width = width / 2 + width % 2)
      ++height;
    require(proof.siblings.size() == height, "Wrong proof height");
    auto current = hash(encodeSegment(range, segment, limits));
    std::uint64_t width = range.segments, position = proof.index;
    for (const auto &sibling : proof.siblings) {
      if (position % 2 == 0 && position + 1 == width)
        require(sibling == current, "Noncanonical odd-leaf duplication");
      current = position % 2 ? parentHash(sibling, current, limits, hash)
                             : parentHash(current, sibling, limits, hash);
      position /= 2;
      width = width / 2 + width % 2;
    }
    return rootHash(range, current, limits, hash) == expected_root;
  } catch (const std::invalid_argument &) {
    return false;
  }
}
} // namespace pgn::core

#include "certified_gap_encoding.h"
#include "canonical_encoding.h"
#include <boost/multiprecision/integer.hpp>
#include <limits>

namespace pgn::core {
namespace {
using detail::encodingRequire;
using detail::Reader;
using detail::Writer;
constexpr std::string_view domain{"PGN certified gap V1"};
void validateLimits(const CertifiedGapEncodingLimits &limits) {
  encodingRequire(limits.encoding.max_encoded_bytes &&
                      limits.encoding.max_integer_bytes &&
                      limits.evidence.endpoint_limits.max_integer_bits &&
                      limits.evidence.endpoint_limits.max_nodes &&
                      limits.evidence.endpoint_limits.max_factors_per_node,
                  "Explicit proof encoding limits required");
}
void admitInteger(const Integer &value,
                  const CertifiedGapEncodingLimits &limits) {
  encodingRequire(value >= 2 &&
                      boost::multiprecision::msb(value) <
                          limits.evidence.endpoint_limits.max_integer_bits &&
                      boost::multiprecision::msb(value) / 8 <
                          limits.encoding.max_integer_bytes,
                  "Proof integer outside explicit admission");
}
Integer readInteger(Reader &reader, const CertifiedGapEncodingLimits &limits) {
  auto value = reader.integer();
  admitInteger(value, limits);
  return value;
}
void admitHeader(const Integer &lower, const Integer &gap, std::uint64_t count,
                 const CertifiedGapEncodingLimits &limits) {
  admitInteger(lower, limits);
  encodingRequire(gap > 0 &&
                      boost::multiprecision::msb(gap) <
                          limits.evidence.endpoint_limits.max_integer_bits &&
                      boost::multiprecision::msb(gap) / 8 <
                          limits.encoding.max_integer_bytes &&
                      gap - 1 == count &&
                      count <= limits.evidence.max_interior_positions,
                  "Invalid or unadmitted interior position count");
  admitInteger(lower + gap, limits);
}
void admitNodes(const std::vector<PrimeCertificateNode> &nodes,
                const CertifiedGapEncodingLimits &limits) {
  encodingRequire(nodes.size() <= limits.evidence.endpoint_limits.max_nodes,
                  "Certificate node count exceeds limit");
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const auto &node = nodes[index];
    admitInteger(node.number, limits);
    encodingRequire(node.factors.size() <=
                        limits.evidence.endpoint_limits.max_factors_per_node,
                    "Certificate factor count exceeds limit");
    for (const auto &factor : node.factors) {
      admitInteger(factor.prime, limits);
      admitInteger(factor.witness, limits);
      encodingRequire(factor.exponent && (!factor.prior_certificate ||
                                          *factor.prior_certificate < index),
                      "Invalid exponent or non-backward certificate reference");
    }
  }
}
void writeNodes(Writer &writer,
                const std::vector<PrimeCertificateNode> &nodes) {
  writer.count(nodes.size());
  for (const auto &node : nodes) {
    writer.integer(node.number);
    writer.count(node.factors.size());
    for (const auto &factor : node.factors) {
      writer.integer(factor.prime);
      writer.count(factor.exponent);
      writer.integer(factor.witness);
      writer.byte(factor.prior_certificate ? 1 : 0);
      if (factor.prior_certificate)
        writer.count(*factor.prior_certificate);
    }
  }
}
std::vector<PrimeCertificateNode>
readNodes(Reader &reader, const CertifiedGapEncodingLimits &limits) {
  const auto count = reader.count();
  encodingRequire(count <= limits.evidence.endpoint_limits.max_nodes &&
                      count <= (reader.data.size() - reader.position) / 3,
                  "Forged or unadmitted certificate node count");
  std::vector<PrimeCertificateNode> nodes;
  nodes.reserve(static_cast<std::size_t>(count));
  for (std::uint64_t index = 0; index < count; ++index) {
    PrimeCertificateNode node{readInteger(reader, limits), {}};
    const auto factors = reader.count();
    encodingRequire(
        factors <= limits.evidence.endpoint_limits.max_factors_per_node &&
            factors <= (reader.data.size() - reader.position) / 6,
        "Forged or unadmitted certificate factor count");
    node.factors.reserve(static_cast<std::size_t>(factors));
    for (std::uint64_t factorIndex = 0; factorIndex < factors; ++factorIndex) {
      const auto prime = readInteger(reader, limits);
      const auto exponent = reader.count();
      encodingRequire(exponent > 0 &&
                          exponent <= std::numeric_limits<std::uint32_t>::max(),
                      "Invalid or overflowing factor exponent");
      const auto witness = readInteger(reader, limits);
      const auto hasPrior = reader.byte();
      encodingRequire(hasPrior <= 1, "Unknown certificate reference flag");
      std::optional<std::uint32_t> prior;
      if (hasPrior) {
        const auto reference = reader.count();
        encodingRequire(reference < index &&
                            reference <=
                                std::numeric_limits<std::uint32_t>::max(),
                        "Certificate reference must point backwards");
        prior = static_cast<std::uint32_t>(reference);
      }
      node.factors.push_back(
          {prime, static_cast<std::uint32_t>(exponent), witness, prior});
    }
    nodes.push_back(std::move(node));
  }
  return nodes;
}
bool knownKind(CompositeWitnessKind kind) {
  return kind == CompositeWitnessKind::Divisor ||
         kind == CompositeWitnessKind::StrongTestFailure;
}
} // namespace
Bytes encodeCertifiedGap(const CertifiedGap &evidence,
                         const CertifiedGapEncodingLimits &limits) {
  validateLimits(limits);
  admitHeader(evidence.lower, evidence.gap, evidence.interior.size(), limits);
  admitNodes(evidence.lower_certificate, limits);
  admitNodes(evidence.upper_certificate, limits);
  for (const auto &witness : evidence.interior) {
    encodingRequire(knownKind(witness.kind), "Unknown composite witness kind");
    admitInteger(witness.value, limits);
  }
  Writer writer{{}, limits.encoding};
  writer.domain(domain);
  writer.integer(evidence.lower);
  writer.integer(evidence.gap);
  writeNodes(writer, evidence.lower_certificate);
  writeNodes(writer, evidence.upper_certificate);
  writer.count(evidence.interior.size());
  for (const auto &witness : evidence.interior) {
    writer.byte(static_cast<std::uint8_t>(witness.kind));
    writer.integer(witness.value);
  }
  return writer.data;
}
CertifiedGap decodeCertifiedGap(std::span<const std::uint8_t> bytes,
                                const CertifiedGapEncodingLimits &limits) {
  validateLimits(limits);
  encodingRequire(bytes.size() <= limits.encoding.max_encoded_bytes,
                  "Proof exceeds explicit encoded byte limit");
  Reader reader{bytes, limits.encoding};
  reader.domain(domain);
  const auto lower = readInteger(reader, limits);
  const auto gap = reader.integer();
  encodingRequire(gap > 0 && gap - 1 <= limits.evidence.max_interior_positions,
                  "Invalid gap before certificate allocation");
  admitHeader(lower, gap, (gap - 1).convert_to<std::uint64_t>(), limits);
  auto lowerNodes = readNodes(reader, limits);
  auto upperNodes = readNodes(reader, limits);
  const auto count = reader.count();
  admitHeader(lower, gap, count, limits);
  encodingRequire(count <= (reader.data.size() - reader.position) / 3,
                  "Forged interior witness count before allocation");
  CertifiedGap evidence{
      lower, gap, std::move(lowerNodes), std::move(upperNodes), {}};
  evidence.interior.reserve(static_cast<std::size_t>(count));
  for (std::uint64_t index = 0; index < count; ++index) {
    const auto kind = static_cast<CompositeWitnessKind>(reader.byte());
    encodingRequire(knownKind(kind), "Unknown composite witness kind");
    evidence.interior.push_back({kind, readInteger(reader, limits)});
  }
  encodingRequire(reader.position == reader.data.size(),
                  "Trailing proof bytes");
  return evidence;
}
} // namespace pgn::core

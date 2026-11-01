#pragma once
#include "certified_gap.h"

namespace pgn::core {
struct CertifiedGapEncodingLimits {
  EncodingLimits encoding;
  CertifiedGapLimits evidence;
};
// Candidate canonical format, not an approved network version. Decoding admits
// structure and size, NOT primality, consecutiveness, merit or chain
// eligibility. Malformed or unadmitted input throws invalid_argument, never
// partial evidence.
Bytes encodeCertifiedGap(const CertifiedGap &,
                         const CertifiedGapEncodingLimits &);
CertifiedGap decodeCertifiedGap(std::span<const std::uint8_t>,
                                const CertifiedGapEncodingLimits &);
} // namespace pgn::core

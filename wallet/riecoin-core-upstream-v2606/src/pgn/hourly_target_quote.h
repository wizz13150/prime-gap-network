// Private task13 adapter: retain the exact decimal proof threshold separately
// from its legacy header identifier. No activation or verifier hook here.
#pragma once
#include <cstdint>
#include <limits>
#include <optional>
namespace pgn::discussion {
struct HourlyTargetQuote {
    uint32_t target_tenths,header_q16;
    uint32_t proof_numerator,proof_denominator;
};
inline std::optional<HourlyTargetQuote> QuoteHourlyTarget(unsigned tenths,uint32_t maximum_header_q16){
    if(tenths<275)return {};
    const auto encoded=(uint64_t(tenths)*65536+9)/10;
    if(encoded>maximum_header_q16||encoded>std::numeric_limits<uint32_t>::max())return {};
    return HourlyTargetQuote{tenths,uint32_t(encoded),tenths,10};
}
// Only for explicitly activated decimal-target policy. Historical arbitrary
// Q16 targets retain their existing interpretation and must not pass here.
inline std::optional<HourlyTargetQuote> DecodeHourlyTarget(uint32_t encoded,uint32_t maximum_header_q16){
    const auto tenths=uint64_t(encoded)*10/65536;
    if(tenths>std::numeric_limits<unsigned>::max())return {};
    auto q=QuoteHourlyTarget(unsigned(tenths),maximum_header_q16);
    return q&&q->header_q16==encoded?q:std::nullopt;
}
}

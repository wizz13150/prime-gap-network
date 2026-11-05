#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pgn { namespace source {
struct BatchPublicationTime { std::array<char,128> fields{}; };
// Windows FILETIME counts100ns since1601-01-01 UTC. Its system-time domain
// excludes the high bit. This is a platform representation, not a PGN policy.
inline BatchPublicationTime publicationTimeFromFileTime(std::uint64_t ticks){
    constexpr std::uint64_t epoch=116444736000000000ULL,perSecond=10000000ULL;
    BatchPublicationTime stamp;
    if(ticks>epoch&&ticks<0x8000000000000000ULL){
        const auto seconds=(ticks-epoch)/perSecond;
        if(seconds){
            std::snprintf(stamp.fields.data(),stamp.fields.size(),
                " unix_ts=%llu time_basis=source_batch_publication",static_cast<unsigned long long>(seconds));
            return stamp;
        }
    }
    std::snprintf(stamp.fields.data(),stamp.fields.size(),
        " time_basis=source_batch_publication time_status=unavailable");
    return stamp; // No fake0/fallback date; the GAP is still published.
}
template<class FileTimeReader>
inline BatchPublicationTime captureBatchPublicationTime(FileTimeReader clock){
    const auto ticks=clock(); // Exactly ONE clock read, independent of row count.
    return publicationTimeFromFileTime(ticks);
}
#ifdef _WIN32
inline BatchPublicationTime captureBatchPublicationTime(){
    return captureBatchPublicationTime([]()->std::uint64_t{
        FILETIME utc{};GetSystemTimeAsFileTime(&utc);
        return (std::uint64_t(utc.dwHighDateTime)<<32)|utc.dwLowDateTime;
    });
}
#endif
// The actual production GAP formatter: old arguments/widths/proof unchanged.
inline int formatSourceGap(char* line,std::size_t capacity,double merit,unsigned gap,
    const char* lower,const char* upper,const BatchPublicationTime& publication){
    return std::snprintf(line,capacity,
        "[phase0-gpu] GAP merit=%.4f gap=%u lower=%s upper=%s verified=1 proof=GPU_MR12_EULER_COMPOSITE record=off table=unknown%s\n",
        merit,gap,lower,upper,publication.fields.data());
}
}}

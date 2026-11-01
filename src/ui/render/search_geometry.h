#pragma once

#include <array>
#include <cstddef>
#include <optional>

struct FrontierShiftBand {
    int from;
    int to;
    const char* label;
};

inline constexpr std::array<FrontierShiftBand, 5> FrontierShiftBands{{
    {25, 511, "SHIFT 25 TO SHIFT 511"},
    {512, 1023, "SHIFT 512 TO SHIFT 1023"},
    {1024, 2047, "SHIFT 1024 TO SHIFT 2047"},
    {2048, 4095, "SHIFT 2048 TO SHIFT 4095"},
    {4096, 16384, "SHIFT 4096 TO SHIFT 16384"},
}};

inline std::optional<std::size_t> FrontierShiftBandIndex(int computeShift)
{
    for (std::size_t index = 0; index < FrontierShiftBands.size(); ++index) {
        const FrontierShiftBand& band = FrontierShiftBands[index];
        if (computeShift >= band.from && computeShift <= band.to) return index;
    }
    return std::nullopt;
}

inline bool FrontierHasExactLiveGeometry(const std::optional<int>& numberBits,
                                         const std::optional<int>& computeShift)
{
    return numberBits.has_value() || computeShift.has_value();
}

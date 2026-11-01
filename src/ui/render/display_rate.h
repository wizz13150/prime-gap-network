#pragma once

#include <optional>

// An admitted zero is data, never permission to fall back to a positive rate.
// Absent measurements are handled by source ownership/age at the admission
// boundary; this presentation helper cannot conceal a measured trough.
inline std::optional<double> FrontierDisplayedRate(
    const std::optional<double>& windowRate,
    const std::optional<double>& emittedRate)
{
    if (windowRate) return windowRate;
    if (emittedRate) return emittedRate;
    return windowRate;
}

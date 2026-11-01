#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// The frame belongs to the visible viewport, including after arbitrary pan.
// Keep exact endpoint coordinates; discard only interior labels that would
// collide with them. This must never expand/reposition the underlying data.
inline std::vector<double> FrontierBorderTicks(double minimum, double maximum,
    std::vector<double> interior, double pixels, double labelSeparation)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum ||
        !std::isfinite(pixels) || pixels <= 0 || !std::isfinite(labelSeparation)) return {};
    const double span = maximum - minimum;
    if (!std::isfinite(span)) return {};
    const double gap = std::max(0., labelSeparation);
    std::sort(interior.begin(), interior.end());
    std::vector<double> result{minimum};
    double previous = 0.;
    for (const double value : interior) {
        if (!std::isfinite(value) || value <= minimum || value >= maximum) continue;
        const double position = (value - minimum) / span * pixels;
        if (position - previous < gap || pixels - position < gap) continue;
        if (value == result.back()) continue;
        result.push_back(value);
        previous = position;
    }
    result.push_back(maximum);
    return result;
}

#pragma once

#include <algorithm>
#include <vector>

inline double FrontierDeviceStripTotalWidth(int cardCount, double cardWidth, double gap)
{
    return cardCount <= 0 ? 0.0 : cardCount * cardWidth + (cardCount - 1) * gap;
}

inline double FrontierDeviceStripMixedTotalWidth(const std::vector<double>& cardWidths,
                                                 double gap)
{
    if (cardWidths.empty()) return 0.0;
    double total = gap * (cardWidths.size() - 1);
    for (double width : cardWidths) total += std::max(0.0, width);
    return total;
}

inline double FrontierDeviceStripMaximumScroll(double totalWidth, double viewportWidth)
{
    return std::max(0.0, totalWidth - viewportWidth);
}

inline double FrontierDeviceStripStartX(double viewportLeft,
                                        double viewportWidth,
                                        double totalWidth,
                                        double scrollX)
{
    if (totalWidth < viewportWidth) return viewportLeft + (viewportWidth - totalWidth) / 2.0;
    return viewportLeft - std::clamp(scrollX, 0.0,
                                     FrontierDeviceStripMaximumScroll(totalWidth, viewportWidth));
}

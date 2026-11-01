#pragma once

#include <algorithm>
#include <vector>

struct FrontierMinerSelection {
    int index{0};
    bool explicitNavigation{false};
    bool selectedWasLive{false};
    long long explicitUntilMs{0};
};

inline FrontierMinerSelection FrontierResolveMinerSelection(
    FrontierMinerSelection selection,
    const std::vector<bool>& live,
    long long nowMs)
{
    if (live.empty()) return {};
    selection.index = std::clamp(selection.index, 0, static_cast<int>(live.size()) - 1);
    const bool currentLive = live[selection.index];
    const auto firstLive = std::find(live.cbegin(), live.cend(), true);
    const bool explicitRecent = selection.explicitNavigation &&
        nowMs <= selection.explicitUntilMs;
    if (!explicitRecent) selection.explicitNavigation = false;
    const bool shouldRecenter = firstLive != live.cend() && !currentLive && !explicitRecent;
    if (shouldRecenter) {
        selection.index = static_cast<int>(std::distance(live.cbegin(), firstLive));
        selection.explicitNavigation = false;
    }
    selection.selectedWasLive = live[selection.index];
    return selection;
}

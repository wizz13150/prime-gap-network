#pragma once

#include <algorithm>

struct FrontierProofTimelineTimes {
    long long discoveredUtcMs{-1};
    long long verifiedUtcMs{-1};
};

inline FrontierProofTimelineTimes FrontierMergeProofTimelineTimes(
    FrontierProofTimelineTimes current,
    FrontierProofTimelineTimes incoming)
{
    if (incoming.discoveredUtcMs >= 0 &&
        (current.discoveredUtcMs < 0 || incoming.discoveredUtcMs < current.discoveredUtcMs)) {
        current.discoveredUtcMs = incoming.discoveredUtcMs;
    }
    if (incoming.verifiedUtcMs >= 0) {
        current.verifiedUtcMs = std::max(current.verifiedUtcMs, incoming.verifiedUtcMs);
    }
    return current;
}


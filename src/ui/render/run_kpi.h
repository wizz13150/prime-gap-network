#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

struct FrontierPairCheckLane {
    bool active{false};
    std::optional<double> pairChecksPerSecond;
    std::string partitionStart;
    std::string partitionEnd;
};

inline unsigned long long FrontierSessionCounterDelta(unsigned long long current,
                                                      unsigned long long sessionBaseline)
{
    // A cumulative producer counter predating the UI session is history, not
    // CURRENT. Counter rollback is fail-closed until a new run/baseline.
    return current >= sessionBaseline ? current - sessionBaseline : 0ULL;
}

inline bool FrontierCanonicalDecimal(const std::string& value)
{
    if (value.empty() || (value.size() > 1 && value.front() == '0')) return false;
    return std::all_of(value.cbegin(), value.cend(), [](unsigned char c) {
        return c >= '0' && c <= '9';
    });
}

inline int FrontierCompareDecimal(const std::string& left, const std::string& right)
{
    if (left.size() != right.size()) return left.size() < right.size() ? -1 : 1;
    if (left == right) return 0;
    return left < right ? -1 : 1;
}

inline std::optional<double> FrontierDisjointPairChecksPerSecond(
    const std::vector<FrontierPairCheckLane>& lanes)
{
    std::vector<FrontierPairCheckLane> active;
    for (const FrontierPairCheckLane& lane : lanes) {
        if (!lane.active) continue;
        if (!lane.pairChecksPerSecond || !std::isfinite(*lane.pairChecksPerSecond) ||
            *lane.pairChecksPerSecond < 0.0) return std::nullopt;
        active.push_back(lane);
    }
    if (active.empty()) return std::nullopt;
    if (active.size() > 1) {
        for (const FrontierPairCheckLane& lane : active) {
            if (!FrontierCanonicalDecimal(lane.partitionStart) ||
                !FrontierCanonicalDecimal(lane.partitionEnd) ||
                FrontierCompareDecimal(lane.partitionStart, lane.partitionEnd) >= 0) {
                return std::nullopt;
            }
        }
        std::sort(active.begin(), active.end(), [](const auto& left, const auto& right) {
            return FrontierCompareDecimal(left.partitionStart, right.partitionStart) < 0;
        });
        for (std::size_t index = 1; index < active.size(); ++index) {
            if (FrontierCompareDecimal(active[index - 1].partitionEnd,
                                       active[index].partitionStart) > 0) return std::nullopt;
        }
    }
    double total = 0.0;
    for (const FrontierPairCheckLane& lane : active) total += *lane.pairChecksPerSecond;
    return std::isfinite(total) ? std::optional<double>(total) : std::nullopt;
}

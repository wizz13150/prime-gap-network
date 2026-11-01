#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

struct FrontierRunLaneState {
    std::string runId;
    std::string state;
    bool authoritative{false};
    bool stale{false};
    bool terminalSealed{false};
};

inline bool FrontierRunLaneIsActive(const FrontierRunLaneState& lane)
{
    if (!lane.authoritative || lane.stale || lane.terminalSealed || lane.runId.empty()) return false;
    std::string state = lane.state;
    std::transform(state.begin(), state.end(), state.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return state != "complete" && state != "failed" && state != "disconnected" &&
        state != "stopped";
}

inline std::vector<std::string> FrontierActiveRunIds(const std::vector<FrontierRunLaneState>& lanes)
{
    std::vector<std::string> runs;
    for (const FrontierRunLaneState& lane : lanes) {
        if (!FrontierRunLaneIsActive(lane)) continue;
        if (std::find(runs.cbegin(), runs.cend(), lane.runId) == runs.cend()) runs.push_back(lane.runId);
    }
    return runs;
}

inline std::vector<std::string> FrontierCurrentRunIds(
    const std::vector<FrontierRunLaneState>& lanes)
{
    std::vector<std::string> runs;
    for (const FrontierRunLaneState& lane : lanes) {
        // CURRENT means the latest ON session represented by each physical
        // lane, not merely a process that happens to be alive this frame.  A
        // clean stop or a finite-band terminal record must therefore retain
        // its points until that lane starts a newer run.
        if (lane.runId.empty()) continue;
        if (std::find(runs.cbegin(), runs.cend(), lane.runId) == runs.cend()) {
            runs.push_back(lane.runId);
        }
    }
    return runs;
}

inline bool FrontierIncludeGapEvent(std::string_view runId,
                                    const std::vector<std::string>& activeRunIds,
                                    bool fullHistory,
                                    long long eventObservedUtcMs,
                                    long long uiSessionStartedUtcMs)
{
    if (fullHistory) return true;
    // CURRENT is deliberately session-local.  The reader bootstraps existing
    // JSONL/receipts at startup; even an old event carrying the still-active
    // run id belongs to FULL HISTORY until a newly observed event arrives.
    if (eventObservedUtcMs < uiSessionStartedUtcMs) return false;
    return std::find(activeRunIds.cbegin(), activeRunIds.cend(), runId) != activeRunIds.cend();
}

inline bool FrontierTelemetryRecordAdvancesRun(std::string_view currentRunId,
                                               long long currentSequence,
                                               std::string_view incomingRunId,
                                               std::string_view incomingState,
                                               long long incomingSequence)
{
    if (incomingRunId.empty() || incomingSequence < 0) return false;
    if (currentRunId.empty()) return true;
    if (incomingRunId == currentRunId) return incomingSequence > currentSequence;
    std::string state{incomingState};
    std::transform(state.begin(), state.end(), state.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    // A canonical stream may append a new run without truncating.  Only its
    // explicit start boundary can replace the current run; arbitrary records
    // from interleaved/old runs remain rejected.
    return incomingSequence <= 1 &&
        (state == "prelaunch" || state == "starting" || state == "initializing");
}

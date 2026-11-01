#pragma once

#include "verifier_progress.h"

#include <string_view>

enum class FrontierVerifierDisplayState {
    Unavailable,
    Online,
    SealedArchive,
};

inline FrontierVerifierDisplayState FrontierVerifierDisplay(
    bool telemetryPresent,
    bool live,
    bool terminalSealed,
    std::string_view state,
    const FrontierVerifierProgress& progress)
{
    if (!telemetryPresent) return FrontierVerifierDisplayState::Unavailable;
    if (live) return FrontierVerifierDisplayState::Online;
    const bool stopped = state == "stopped" || state == "complete";
    if ((terminalSealed || stopped) && progress.sourceSeenTotal > 0 &&
        progress.totalVerified() == progress.sourceSeenTotal && progress.caughtUp()) {
        return FrontierVerifierDisplayState::SealedArchive;
    }
    return FrontierVerifierDisplayState::Unavailable;
}

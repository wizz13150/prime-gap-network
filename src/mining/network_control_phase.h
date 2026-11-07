#pragma once
#include <string_view>

namespace pgn::mining {
struct NetworkControlPhase {
    bool suppressPresentation{};
    bool acknowledgeOn{};
    bool acknowledgeOff{};
};

// Transport/engine observations do not replace the latest user intent.
// A terminal fault remains visible until an explicit intent clears recovery.
constexpr NetworkControlPhase networkControlPhase(std::string_view phase,
                                                  bool wantedOn,
                                                  bool needsAttention,
                                                  bool sourceFresh)
{
    const bool paused = phase == "Paused";
    const bool suppress = (needsAttention && phase != "Needs attention") ||
                          (paused && wantedOn);
    return {suppress,
            !suppress && phase == "Searching" && wantedOn && sourceFresh,
            !suppress && paused && !wantedOn};
}
}

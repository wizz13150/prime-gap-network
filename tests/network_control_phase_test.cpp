#include "mining/network_control_phase.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

static void need(bool condition, const char* detail)
{
    if (!condition) throw std::runtime_error(detail);
}

struct Model {
    bool wantedOn{true}, pendingOn{true}, pendingOff{}, checked{true};
    bool needsAttention{}, resolvedOn{}, resolvedOff{}, rejected{};
    std::string message{"Starting"};
    void phase(std::string_view value, bool sourceFresh = true)
    {
        if (value == "Needs attention") needsAttention = true;
        const auto reply = pgn::mining::networkControlPhase(value, wantedOn, needsAttention, sourceFresh);
        if (reply.suppressPresentation) return;
        if (value == "Needs attention") {
            message = "Exact semantic error";
            checked = false;
            rejected = pendingOn || pendingOff;
            pendingOn = pendingOff = false;
        } else if (reply.acknowledgeOff) {
            checked = false;
            if (pendingOff) { resolvedOff = true; pendingOff = false; }
            message = "Paused";
        } else if (reply.acknowledgeOn) {
            checked = true;
            if (pendingOn) { resolvedOn = true; pendingOn = false; }
            message = "Searching";
        }
    }
    void intent(bool on)
    {
        wantedOn = checked = on;
        needsAttention = false; // existing NetworkControllerRecovery::intent
        pendingOn = on;
        pendingOff = !on;
    }
};

int main()
{
    Model resumed;
    resumed.phase("Paused");
    need(resumed.checked && resumed.pendingOn && !resumed.resolvedOn,
         "Late Paused erased ON or acknowledged the wrong direction");
    need(resumed.message == "Starting", "Late Paused replaced pending ON presentation");
    resumed.phase("Searching");
    need(resumed.resolvedOn && !resumed.pendingOn && resumed.checked,
         "Searching did not acknowledge ON");
    for (const bool explicitlyFresh : {false, true}) {
        Model waiting;
        waiting.phase("Searching", explicitlyFresh);
        need(waiting.resolvedOn == explicitlyFresh && waiting.pendingOn != explicitlyFresh,
             "Stale/missing freshness Searching fabricated an ON acknowledgement");
    }
    Model stopping;
    stopping.intent(false);
    stopping.phase("Searching");
    need(stopping.pendingOff && !stopping.resolvedOff && !stopping.checked,
         "Late Searching acknowledged OFF");
    stopping.phase("Paused");
    need(stopping.resolvedOff && !stopping.pendingOff && !stopping.checked,
         "Paused did not acknowledge OFF");
    Model failed;
    failed.phase("Needs attention");
    failed.phase("Paused");
    failed.phase("Searching");
    need(failed.message == "Exact semantic error" && !failed.checked && failed.rejected &&
             !failed.resolvedOn && !failed.resolvedOff,
         "Late phase erased a sticky error or fabricated a positive ACK");
    failed.intent(true);
    failed.phase("Paused");
    failed.phase("Searching");
    need(!failed.needsAttention && failed.resolvedOn && failed.checked,
         "Explicit retry could not clear the terminal hold");
    for (const bool wanted : {false, true}) {
        const auto reply = pgn::mining::networkControlPhase("Report ready", wanted, false, true);
        need(!reply.acknowledgeOn && !reply.acknowledgeOff,
             "A completed report replaced an exact control ACK");
    }
    std::cout << "PASS 10 directional/sticky/freshness phase scenarios; pure state, no UI/GPU/Core\n";
}

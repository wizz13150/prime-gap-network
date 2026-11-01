#pragma once
#include <string_view>

constexpr int kFrontierGpuHostFirstPending = -98;
constexpr int kFrontierBrokenVerificationChain = -99;

// W8 CPU journals start at queue admission. They do not emit the GPU's
// discovery/host-validation pair. Keep that trace distinct: a node receipt
// must not fabricate a missing local-verification event.
inline int FrontierJournalVerificationStage(std::string_view event, bool cpu)
{
    if (!cpu && event == "gpu_gap_discovered") return 1;
    if (!cpu && event == "gpu_host_validated") return 2;
    const int base = cpu ? 10 : 0;
    if (event == "queued_before_memory_enqueue") return base + 3;
    if (event == "node_submission_started") return base + 4;
    if (event == "node_accepted_pending_canonical") return base + 5;
    if (event == "node_rejected_or_stale") return -(base + 5);
    return 0;
}

inline int FrontierJournalVerificationTransition(int previous, int next)
{
    if (!next) return previous;
    if (previous == kFrontierBrokenVerificationChain)
        return kFrontierBrokenVerificationChain;
    // Older GPU workers journalled host validation before discovery.  Hold
    // that receipt as incomplete until the matching discovery arrives; do
    // not silently certify a missing discovery or erase other order errors.
    if (previous == kFrontierGpuHostFirstPending) {
        if (next == 1) return 2;
        if (next == 2) return kFrontierGpuHostFirstPending;
        return kFrontierBrokenVerificationChain;
    }
    if (previous == 0 && next == 2) return kFrontierGpuHostFirstPending;
    const int oldMagnitude = previous < 0 ? -previous : previous;
    const int magnitude = next < 0 ? -next : next;
    if (previous == 0 && next == 13) return 13;
    if (magnitude < oldMagnitude || magnitude > oldMagnitude + 1 ||
        (magnitude == oldMagnitude && (previous < 0) != (next < 0)))
        return kFrontierBrokenVerificationChain;
    return next;
}

// Display-only aggregate of the existing per-result verification states.
// State ownership/ordering and all authoritative journal records stay outside.
struct FrontierVerificationStageCounts {
    int hostValidated = 0;
    int submitted = 0;
    int pendingCanonical = 0;
    int rejected = 0;
    int chainErrors = 0;

    void transition(int previous, int next)
    {
        if (previous == next) return;
        contribute(previous, -1);
        contribute(next, 1);
    }

private:
    // 13..15 are CPU queue/RPC receipts without an observed host-validation trace.
    void contribute(int stage, int delta)
    {
        if (stage == kFrontierBrokenVerificationChain ||
            stage == kFrontierGpuHostFirstPending) {
            chainErrors += delta;
            return;
        }
        const int magnitude = stage < 0 ? -stage : stage;
        if (magnitude >= 13 && magnitude <= 15) {
            if (magnitude >= 14) submitted += delta;
            if (stage == 15) pendingCanonical += delta;
            if (stage == -15) rejected += delta;
            return;
        }
        if (magnitude >= 2) hostValidated += delta;
        if (magnitude >= 4) submitted += delta;
        if (stage == 5) pendingCanonical += delta;
        if (stage < 0) rejected += delta;
    }
};

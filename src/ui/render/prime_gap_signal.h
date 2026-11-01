#pragma once

#include <algorithm>
#include <cmath>

enum class FrontierPrimeGapSignalKind {
    Idle,
    Candidate,
    Exact,
    Rejected,
};

struct FrontierPrimeGapVisualState {
    double boundaryIntensity{0.72};
    double transferProgress{0.0};
    bool transferVisible{false};
    bool exactLocked{false};
    bool rejectedFade{false};
};

inline FrontierPrimeGapVisualState FrontierPrimeGapVisual(
    FrontierPrimeGapSignalKind kind,
    long long signalAgeMs,
    bool verifierActive,
    long long verifierObservedMs,
    long long nowMs)
{
    FrontierPrimeGapVisualState visual;
    if (verifierActive && verifierObservedMs > 0 && nowMs >= verifierObservedMs) {
        constexpr double pi = 3.14159265358979323846;
        const double heartbeatPhase =
            std::fmod(static_cast<double>(nowMs - verifierObservedMs), 1800.0) / 1800.0;
        visual.boundaryIntensity = 0.82 + 0.16 *
            (0.5 + 0.5 * std::sin(heartbeatPhase * 2.0 * pi));
    }
    if (signalAgeMs < 0) return visual;
    switch (kind) {
    case FrontierPrimeGapSignalKind::Candidate:
        if (signalAgeMs <= 2200) {
            visual.transferVisible = true;
            visual.transferProgress = std::clamp(signalAgeMs / 1800.0, 0.0, 1.0);
            visual.boundaryIntensity = std::max(visual.boundaryIntensity, 0.9);
        }
        break;
    case FrontierPrimeGapSignalKind::Exact:
        if (signalAgeMs <= 4000) {
            visual.exactLocked = true;
            visual.boundaryIntensity = 1.0;
        }
        break;
    case FrontierPrimeGapSignalKind::Rejected:
        if (signalAgeMs <= 1600) {
            visual.rejectedFade = true;
            visual.boundaryIntensity = std::max(0.08, 1.0 - signalAgeMs / 1600.0);
        }
        break;
    case FrontierPrimeGapSignalKind::Idle:
        break;
    }
    return visual;
}

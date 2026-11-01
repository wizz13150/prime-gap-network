#pragma once

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>

inline std::optional<double> FrontierFirstEmittedProducerRate(
    std::initializer_list<std::optional<double>> rates)
{
    for (const std::optional<double>& rate : rates) {
        // An emitted zero is a real observation, not permission to borrow a
        // positive value from a secondary metric and animate phantom work.
        if (rate && std::isfinite(*rate) && *rate >= 0.0) return rate;
    }
    return std::nullopt;
}

inline double FrontierProducerRelativeSpeed(double rate, double maximumFreshRate)
{
    if (!std::isfinite(rate) || !std::isfinite(maximumFreshRate) ||
        rate <= 0.0 || maximumFreshRate <= 0.0) return 0.0;
    return std::clamp(rate / maximumFreshRate, 0.0, 1.0);
}

inline double FrontierAdvanceProducerPhase(double phase,
                                           double relativeSpeed,
                                           long long elapsedMs,
                                           double referenceCyclesPerSecond = 0.16)
{
    if (elapsedMs <= 0 || relativeSpeed <= 0.0) return phase;
    phase = std::fmod(phase + referenceCyclesPerSecond *
                      std::clamp(relativeSpeed, 0.0, 1.0) * elapsedMs / 1000.0, 1.0);
    return phase < 0.0 ? phase + 1.0 : phase;
}

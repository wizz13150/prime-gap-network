#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct FrontierGapGeometryProvenance {
    std::optional<std::size_t> decimalDigits;
    std::optional<int> actualNumberBits;
    std::optional<int> historicalShiftEquivalent;
};

inline std::optional<std::size_t> FrontierCanonicalDecimalDigits(std::string_view value)
{
    if (value.empty()) return std::nullopt;
    for (const char digit : value) {
        if (digit < '0' || digit > '9') return std::nullopt;
    }
    const std::size_t first = value.find_first_not_of('0');
    return first == std::string_view::npos ? 1 : value.size() - first;
}

inline FrontierGapGeometryProvenance FrontierGapGeometryFromEvent(
    std::string_view leftEndpoint,
    std::string_view rightEndpoint,
    std::optional<int> emittedNumberBits,
    std::optional<int> emittedComputeShift)
{
    const auto leftDigits = FrontierCanonicalDecimalDigits(leftEndpoint);
    const auto rightDigits = FrontierCanonicalDecimalDigits(rightEndpoint);
    std::optional<std::size_t> digits;
    if (leftDigits && rightDigits) digits = std::max(*leftDigits, *rightDigits);
    else if (leftDigits) digits = leftDigits;
    else if (rightDigits) digits = rightDigits;
    return {digits, emittedNumberBits, emittedComputeShift};
}

// Graph X coordinates are exact gap lengths, never discovery ordinals or
// timestamps.  Candidate-v2 currently bounds exact_gap to JSON's exact integer
// range, but retaining its decimal spelling avoids any double round-trip and
// makes the contract fail closed if that envelope ever changes.
inline std::optional<std::uint64_t> FrontierExactGapInteger(std::string_view value)
{
    if (value.empty() || value.front() == '+' || value.front() == '-') return std::nullopt;
    std::uint64_t result{0};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result == 0) {
        return std::nullopt;
    }
    return result;
}

inline std::optional<std::uint64_t> FrontierExactQ48(std::string_view value)
{
    if (value.empty() || value.front() == '+' || value.front() == '-') return std::nullopt;
    std::uint64_t result{0};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result == 0) {
        return std::nullopt;
    }
    return result;
}

enum class FrontierGapGraphIngestionDecision {
    Admit,
    RejectBelowNetworkDifficulty,
    AwaitNetworkDifficulty,
};

inline std::optional<FrontierGapGraphIngestionDecision>
FrontierClassifyGapGraphIngestionQ48(std::string_view meritQ48,
                                    std::string_view networkTargetQ48)
{
    const auto merit = FrontierExactQ48(meritQ48);
    const auto target = FrontierExactQ48(networkTargetQ48);
    if (!merit || !target) return std::nullopt;
    return *merit >= *target
        ? FrontierGapGraphIngestionDecision::Admit
        : FrontierGapGraphIngestionDecision::RejectBelowNetworkDifficulty;
}

struct FrontierExactGapGraphPoint {
    std::uint64_t exactGap{0};
    double merit{0.0};
    std::size_t sourceOrdinal{0};
};

template <typename Samples, typename Timestamp>
inline std::optional<double> FrontierApplicableNetworkDifficulty(
    const Samples& samples,
    Timestamp observed)
{
    const auto position = std::upper_bound(
        samples.cbegin(), samples.cend(), observed,
        [](const auto& value, const auto& sample) { return value < sample.first; });
    if (position == samples.cbegin()) return std::nullopt;
    const double difficulty = std::prev(position)->second;
    if (!std::isfinite(difficulty) || difficulty <= 0.0) return std::nullopt;
    return difficulty;
}

// The ordinary Horizon graph admits only results that met the network
// difficulty applicable when they were observed.  Missing target provenance
// is an explicit pending state, never a licence to guess a constant.
// Diagnostic journals may remain exhaustive, but every graph view obeys the
// same scientific-usefulness boundary.
inline FrontierGapGraphIngestionDecision FrontierClassifyGapGraphIngestion(
    double merit,
    std::optional<double> applicableNetworkDifficulty)
{
    if (!std::isfinite(merit) || merit <= 0.0) {
        return FrontierGapGraphIngestionDecision::RejectBelowNetworkDifficulty;
    }
    if (!applicableNetworkDifficulty || !std::isfinite(*applicableNetworkDifficulty) ||
        *applicableNetworkDifficulty <= 0.0) {
        return FrontierGapGraphIngestionDecision::AwaitNetworkDifficulty;
    }
    return merit >= *applicableNetworkDifficulty
        ? FrontierGapGraphIngestionDecision::Admit
        : FrontierGapGraphIngestionDecision::RejectBelowNetworkDifficulty;
}

// Use immutable producer provenance when it exists; otherwise classify against
// the authenticated historical target curve.  Keeping this operation pure lets
// both initial ingestion and every later graph projection share the same gate,
// so a target-curve backfill cannot leave an already-loaded below-target point
// visible.
inline FrontierGapGraphIngestionDecision FrontierClassifyGapGraphProjection(
    std::string_view meritQ48,
    std::string_view networkTargetQ48,
    double merit,
    std::optional<double> applicableNetworkDifficulty)
{
    const auto sourceBound = FrontierClassifyGapGraphIngestionQ48(
        meritQ48, networkTargetQ48);
    return sourceBound ? *sourceBound
                       : FrontierClassifyGapGraphIngestion(
                             merit, applicableNetworkDifficulty);
}

enum class FrontierGapTerminalDisposition {
    None,
    Rejected,
    Stale,
};

// Mirror the miner journal's terminal vocabulary without conflating proof
// validity with network outcome.  A host-validated gap remains exact while its
// rejected/stale submission outcome is rendered separately.
inline FrontierGapTerminalDisposition FrontierGapJournalTerminalDisposition(
    std::string_view eventName)
{
    if (eventName.find("rejected") != std::string_view::npos) {
        return FrontierGapTerminalDisposition::Rejected;
    }
    if (eventName.find("stale") != std::string_view::npos ||
        eventName.find("discarded") != std::string_view::npos) {
        return FrontierGapTerminalDisposition::Stale;
    }
    return FrontierGapTerminalDisposition::None;
}

inline std::vector<FrontierExactGapGraphPoint> FrontierLosslessExactGapProjection(
    const std::vector<FrontierExactGapGraphPoint>& points)
{
    // Coordinates may collide (notably OFF and ON for a retained gap).  A
    // collision is an overlay, never a reason to erase either source event.
    return points;
}

inline bool FrontierGapGraphAdmits(bool minimumMeritFilterAbMode,
                                   bool filterControlPoint,
                                   bool admittedByActiveRunPolicy)
{
    // Exhaustive FILTER_OFF/FILTER_ON evidence belongs only to the labelled
    // A/B view.  Ordinary production never imports those control lanes; it
    // plots exactly the egress already selected by the active search policy.
    return minimumMeritFilterAbMode
        ? filterControlPoint
        : (!filterControlPoint && admittedByActiveRunPolicy);
}

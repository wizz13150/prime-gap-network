#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace pgn::mining {

// Absolute search bounds remain decimal strings. These are producer counters,
// not floating point representations of a prime or a range boundary.
struct CounterSample {
    std::string run;
    std::string device;
    std::uint64_t sequence{};
    std::chrono::nanoseconds sourceTime{};
    std::uint64_t positions{};
    std::uint64_t retainedGaps{};
    std::uint64_t primalityTests{};
    std::optional<std::uint64_t> processedGaps;
};

enum class SampleStatus { Baseline, Accepted, IdentityChanged, OutOfOrder, CounterReset };

struct IntervalMetrics {
    CounterSample source;
    std::chrono::nanoseconds begin{};
    std::chrono::nanoseconds duration{};
    std::uint64_t positions{};
    std::uint64_t retainedGaps{};
    std::uint64_t primalityTests{};
    double positionsPerSecond{};
    double retainedGapsPerSecond{};
    double testsPerSecond{};
    std::optional<double> retainedGapsPerBillionPositions;
    std::optional<std::uint64_t> processedGaps;
    std::optional<double> processedGapsPerSecond;
    std::optional<double> retainedFraction;
};

struct SampleResult {
    SampleStatus status{SampleStatus::Baseline};
    std::optional<IntervalMetrics> interval;
};

// Exactly one adjacent source interval: no moving window, EMA, interpolation,
// synthetic zero, or run-to-date rate. A real zero delta stays zero.
class RawMetrics {
public:
    SampleResult accept(const CounterSample& sample);
    const std::optional<CounterSample>& latest() const { return latest_; }
    void clear() { latest_.reset(); }

private:
    std::optional<CounterSample> latest_;
};

// Display evidence over an explicitly reported source-clock span. This is the
// ratio of cumulative deltas to elapsed time, never an average of burst rates.
// RawMetrics remains the independent, unchanged adjacent-interval record.
class SourceRateWindow {
public:
    std::optional<IntervalMetrics> accept(const CounterSample& sample);
    void clear() { adjacent_.clear(); samples_.clear(); }
private:
    RawMetrics adjacent_;
    std::deque<CounterSample> samples_;
};

// Only contemporaneous intervals can be added. Incomparable devices must not
// be presented as a measured combined throughput.
std::optional<IntervalMetrics> simultaneousTotal(
    const std::vector<IntervalMetrics>& intervals);

} // namespace pgn::mining

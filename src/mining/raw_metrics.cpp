#include "raw_metrics.h"

#include <limits>

namespace pgn::mining {
namespace {
void calculate(IntervalMetrics& value) {
    const double seconds = std::chrono::duration<double>(value.duration).count();
    value.positionsPerSecond = double(value.positions) / seconds;
    value.retainedGapsPerSecond = double(value.retainedGaps) / seconds;
    value.testsPerSecond = double(value.primalityTests) / seconds;
    if (value.processedGaps) {
        value.processedGapsPerSecond = double(*value.processedGaps) / seconds;
        if (*value.processedGaps != 0)
            value.retainedFraction = double(value.retainedGaps) / double(*value.processedGaps);
    }
    if (value.positions != 0)
        value.retainedGapsPerBillionPositions = double(value.retainedGaps) * 1e9 / double(value.positions);
}
bool add(std::uint64_t& target, std::uint64_t amount) {
    if (amount > std::numeric_limits<std::uint64_t>::max() - target) return false;
    target += amount;
    return true;
}
} // namespace

SampleResult RawMetrics::accept(const CounterSample& sample) {
    if (!latest_) { latest_ = sample; return {}; }
    const auto& before = *latest_;
    if (sample.run != before.run || sample.device != before.device) {
        latest_ = sample;
        return {SampleStatus::IdentityChanged, {}};
    }
    if (sample.sequence <= before.sequence || sample.sourceTime <= before.sourceTime)
        return {SampleStatus::OutOfOrder, {}};
    if (sample.positions < before.positions || sample.retainedGaps < before.retainedGaps ||
        sample.primalityTests < before.primalityTests ||
        (sample.processedGaps && before.processedGaps && *sample.processedGaps < *before.processedGaps)) {
        latest_ = sample;
        return {SampleStatus::CounterReset, {}};
    }
    IntervalMetrics value{sample, before.sourceTime, sample.sourceTime - before.sourceTime,
        sample.positions - before.positions, sample.retainedGaps - before.retainedGaps,
        sample.primalityTests - before.primalityTests};
    if (sample.processedGaps && before.processedGaps)
        value.processedGaps = *sample.processedGaps - *before.processedGaps;
    calculate(value);
    latest_ = sample;
    return {SampleStatus::Accepted, value};
}

std::optional<IntervalMetrics> SourceRateWindow::accept(const CounterSample& sample) {
    const auto next = adjacent_.accept(sample);
    if (next.status == SampleStatus::OutOfOrder) return {};
    if (next.status != SampleStatus::Accepted) samples_.clear();
    samples_.push_back(sample);
    const auto cutoff = sample.sourceTime - std::chrono::seconds(60);
    // Keep the last real sample at/before the boundary; report the actual span
    // rather than interpolate a fictional counter at exactly sixty seconds.
    while (samples_.size() > 2 && samples_[1].sourceTime <= cutoff)
        samples_.pop_front();
    // Bounded even for a faulty/high-frequency publisher. A shortened span is
    // explicitly carried in IntervalMetrics, and must still pass warm-up.
    while (samples_.size() > 4096) samples_.pop_front();
    if (sample.sourceTime - samples_.front().sourceTime < std::chrono::seconds(5)) return {};
    RawMetrics endpoints;
    endpoints.accept(samples_.front());
    return endpoints.accept(sample).interval;
}

std::optional<IntervalMetrics> simultaneousTotal(const std::vector<IntervalMetrics>& intervals) {
    if (intervals.empty()) return {};
    IntervalMetrics result;
    result.begin = intervals.front().begin;
    result.duration = intervals.front().duration;
    if (result.duration.count() <= 0) return {};
    std::vector<std::string> seen;
    result.processedGaps = 0;
    for (const auto& item : intervals) {
        if (item.begin != result.begin || item.duration != result.duration) return {};
        for (const auto& device : seen) if (device == item.source.device) return {};
        seen.push_back(item.source.device);
        if (result.processedGaps) {
            if (!item.processedGaps) result.processedGaps.reset();
            else if (!add(*result.processedGaps, *item.processedGaps)) return {};
        }
        if (!add(result.positions, item.positions) || !add(result.retainedGaps, item.retainedGaps) ||
            !add(result.primalityTests, item.primalityTests)) return {};
    }
    calculate(result);
    return result;
}
} // namespace pgn::mining

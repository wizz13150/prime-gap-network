#pragma once
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace pgn::ui {
// Forecast only. Integrate cumulative positions over a real producer-clock
// window, so a delayed publication followed by a burst cannot become a peak ETA.
class BlockEtaEstimator {
public:
    struct Estimate { double seconds, windowSeconds, positionsPerSecond; };
    void reset() { epoch_.clear(); samples_.clear(); }
    std::optional<Estimate> observe(const std::string& epoch, std::uint64_t timeNs,
                                   std::uint64_t positions, std::uint64_t length) {
        if (epoch.empty() || !length || positions > length) { reset(); return {}; }
        if (epoch != epoch_ || (!samples_.empty() &&
            (timeNs < samples_.back().time || positions < samples_.back().positions))) {
            reset(); epoch_ = epoch;
        }
        if (samples_.empty() || timeNs > samples_.back().time)
            samples_.push_back({timeNs, positions});
        else if (positions != samples_.back().positions) { reset(); return {}; }
        constexpr std::uint64_t window = 60'000'000'000ULL;
        const auto cutoff = timeNs > window ? timeNs - window : 0;
        while (samples_.size() > 2 && samples_[1].time <= cutoff) samples_.pop_front();
        while (samples_.size() > 1024) samples_.pop_front();
        const auto span = timeNs - samples_.front().time;
        const auto delta = positions - samples_.front().positions;
        if (span < 5'000'000'000ULL || !delta) return {};
        const double seconds = double(span) / 1e9;
        const double rate = double(delta) / seconds;
        return Estimate{double(length - positions) / rate, seconds, rate};
    }
private:
    struct Sample { std::uint64_t time, positions; };
    std::string epoch_;
    std::deque<Sample> samples_;
};
}

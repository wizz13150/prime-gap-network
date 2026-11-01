#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
namespace pgn::ui {
struct RateGaugeReadout {double fraction{},reference{};};
// Shared by Total/GPU/CPU. Repaints never advance a producer-time window.
class RateGaugeScale {
    struct Sample {std::uint64_t ns;double value;};std::deque<Sample> samples_;std::string epoch_;
public:
    std::optional<RateGaugeReadout> observe(std::optional<double> value,std::string_view epoch,std::optional<std::uint64_t> ns){
        if(!value||!std::isfinite(*value)||*value<0||epoch.empty()||!ns){samples_.clear();epoch_.clear();return {};}
        if(epoch_!=epoch||(!samples_.empty()&&*ns<samples_.back().ns)){samples_.clear();epoch_=epoch;}
        if(samples_.empty()||*ns>samples_.back().ns)samples_.push_back({*ns,*value});else samples_.back().value=*value;
        const auto cutoff=*ns>60'000'000'000ULL?*ns-60'000'000'000ULL:0;
        while(samples_.size()>1&&samples_.front().ns<cutoff)samples_.pop_front();while(samples_.size()>512)samples_.pop_front();
        double peak=0;for(const auto& sample:samples_)peak=std::max(peak,sample.value);const double reference=peak*1.15;
        const double ratio=reference>0?std::clamp(*value/reference,0.,1.):0.;return RateGaugeReadout{std::log1p(9.*ratio)/std::log(10.),reference};
    }
};
}

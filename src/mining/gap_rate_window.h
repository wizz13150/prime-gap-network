#pragma once
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace pgn::mining {
struct GapRateMean {double perSecond{},perBillionPositions{},positionsPerSecond{};std::uint64_t spanNs{};};
// A ratio of counter deltas, never an average of sparse per-frame ratios.
// Only the worker's active computation clock advances this bounded window.
class GapRateWindow {
    struct Sample {std::uint64_t ns,positions,gaps;};
    std::string identity_;std::deque<Sample> samples_;
public:
    std::optional<GapRateMean> observe(std::string_view identity,bool active,std::uint64_t ns,std::uint64_t positions,std::uint64_t gaps){
        if(!active||identity.empty()){reset();return {};}
        if(identity_!=identity){reset();identity_=identity;}
        if(!samples_.empty()){
            const auto old=samples_.back();
            if(ns<old.ns||positions<old.positions||gaps<old.gaps||(ns==old.ns&&(positions!=old.positions||gaps!=old.gaps))){reset();identity_=identity;}
        }
        if(samples_.empty()||ns>samples_.back().ns)samples_.push_back({ns,positions,gaps});
        const auto cutoff=ns>60'000'000'000ULL?ns-60'000'000'000ULL:0;
        while(samples_.size()>2&&samples_[1].ns<=cutoff)samples_.pop_front();
        while(samples_.size()>512)samples_.pop_front();
        const auto first=samples_.front();const auto span=ns-first.ns;
        if(span<5'000'000'000ULL||positions<=first.positions)return {};
        const auto delta=gaps-first.gaps;return GapRateMean{double(delta)*1e9/double(span),double(delta)*1e9/double(positions-first.positions),double(positions-first.positions)*1e9/double(span),span};
    }
    void reset(){samples_.clear();identity_.clear();}
};
}

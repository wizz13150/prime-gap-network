// Exact projection of ordered, authenticated first-complete receipt clocks.
// This arithmetic type grants no native admission or consensus authority.
#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>
namespace pgn::discussion {
struct HourlyReceiptHistogram {
    int64_t first_bin{};
    std::vector<uint64_t> bins;
    // This is the actual uint32 native block timestamp domain, not a cache
    // retention quota or a restriction on the number of miners.
    static constexpr int64_t maximum_clock=std::numeric_limits<uint32_t>::max();
    static constexpr int64_t maximum_bin=(maximum_clock+300)/600;
    bool Valid()const {
        if(bins.empty())return first_bin==0;
        return first_bin>=0&&first_bin<=maximum_bin&&
            bins.size()<=uint64_t(maximum_bin-first_bin)+1&&bins.front()!=0&&bins.back()!=0;
    }
    bool Add(int64_t first_received) {
        if(first_received<0||first_received>maximum_clock||!Valid())return false;
        const int64_t bin=(first_received+300)/600;
        if(bins.empty()){bins.push_back(1);first_bin=bin;return true;}
        if(bin<first_bin)return false;
        const auto index=uint64_t(bin-first_bin);
        if(index>=bins.max_size())return false;
        if(index>=bins.size())bins.resize(size_t(index)+1);
        auto& value=bins[size_t(index)];
        if(value==std::numeric_limits<uint64_t>::max())return false;
        ++value;return true;
    }
    std::optional<std::array<uint64_t,6>> Counts(int64_t hour)const {
        if(hour<0||hour%3600||!Valid())return {};
        std::array<uint64_t,6> counts{};
        for(unsigned i=0;i<counts.size();++i){
            const int64_t bin=hour/600-1-i;
            if(bin<first_bin)continue;
            const auto index=uint64_t(bin-first_bin);
            if(index<bins.size())counts[i]=bins[size_t(index)];
        }
        return counts;
    }
};
}

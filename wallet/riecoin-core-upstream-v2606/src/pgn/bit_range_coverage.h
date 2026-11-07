// Copyright (c) 2026 The PGN developers. MIT.
#pragma once
#include <pgn/range_commitment.h>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace pgn::core {
using CoverageSpans=std::vector<std::pair<Integer,Integer>>;
struct BitRangeCoverage {
    unsigned bits{};
    Integer lower,upper,search_start,search_end,excluded_prefix,reported,audited;
};
// Half-open interval UNION, not report volume. Retries/overlap cannot inflate
// progress. All arithmetic stays integral, including 95-bit adjacent endpoints.
inline CoverageSpans MergeCoverageSpans(CoverageSpans spans){
    for(const auto& [a,b]:spans)if(a<0||b<=a)throw std::invalid_argument("Invalid coverage interval");
    std::sort(spans.begin(),spans.end());CoverageSpans merged;
    for(const auto& span:spans){
        if(merged.empty()||span.first>merged.back().second)merged.push_back(span);
        else if(span.second>merged.back().second)merged.back().second=span.second;
    }
    return merged;
}
inline Integer CoverageIntersection(const CoverageSpans& spans,const Integer& lower,const Integer& upper,size_t& cursor){
    while(cursor<spans.size()&&spans[cursor].second<=lower)++cursor;
    Integer length{};
    for(size_t i=cursor;i<spans.size()&&spans[i].first<upper;++i){
        const Integer a=std::max(lower,spans[i].first),b=std::min(upper,spans[i].second);
        if(b>a)length+=b-a;
    }
    return length;
}
inline std::vector<BitRangeCoverage> ComputeBitRangeCoverage(const Integer& origin,const Integer& limit,
    CoverageSpans reported,CoverageSpans audited,unsigned first=67,unsigned last=95){
    if(origin<0||limit<=origin||first<1||first>last||last>128)throw std::invalid_argument("Invalid coverage domain");
    reported=MergeCoverageSpans(std::move(reported));audited=MergeCoverageSpans(std::move(audited));
    // Audited input must be a subset of reported input, not merely a smaller sum.
    size_t subset_cursor=0;
    for(const auto& [a,b]:audited){
        while(subset_cursor<reported.size()&&reported[subset_cursor].second<=a)++subset_cursor;
        if(subset_cursor==reported.size()||reported[subset_cursor].first>a||reported[subset_cursor].second<b)
            throw std::invalid_argument("Audited coverage outside reported ranges");
    }
    std::vector<BitRangeCoverage> rows;size_t r=0,a=0;
    for(unsigned bits=first;bits<=last;++bits){
        const Integer lower=Integer{1}<<(bits-1),upper=Integer{1}<<bits;
        const Integer start=std::max(origin,lower),end=std::min(limit,upper);
        if(end<=start)continue;
        const Integer excluded=start-lower;
        rows.push_back({bits,lower,upper,start,end,excluded,
            CoverageIntersection(reported,start,end,r),CoverageIntersection(audited,start,end,a)});
    }
    return rows;
}
inline uint64_t CoveragePartsPerBillion(const Integer& positions,const Integer& width){
    if(positions<0||width<=0||positions>width)throw std::invalid_argument("Invalid coverage fraction");
    return (positions*1000000000/width).convert_to<uint64_t>();
}
}

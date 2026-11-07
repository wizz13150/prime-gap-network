#pragma once
#include "class30_sieve_port.h"
#include "../../shared_composite/composite_page_composition.h"

namespace pgn_cpu_shared {
// CPU physical mapping of the same immutable, natively validated page used by
// the GPU complement. The page proves only its marks, not exhaustive PSP coverage.
class CpuCompositeOverlay {
    pgn_psp_native::CompositePage page_;
    explicit CpuCompositeOverlay(pgn_psp_native::CompositePage page):page_(std::move(page)){}
    static int cmp(Number96 a,Number96 b){
        if(a.high!=b.high)return a.high<b.high?-1:1;
        return a.low==b.low?0:(a.low<b.low?-1:1);
    }
public:
    static CpuCompositeOverlay factored(const std::string& path,const pgn_psp_native::Cohort& cohort){
        return CpuCompositeOverlay(pgn_psp_native::CompositePage::factored(path,cohort));
    }
    static CpuCompositeOverlay dense(const pgn_psp_native::SmallFactorTable& table,const pgn_psp_native::Cohort& cohort){
        return CpuCompositeOverlay(pgn_psp_native::CompositePage::dense(table,cohort));
    }
    static CpuCompositeOverlay merge(const CpuCompositeOverlay& a,const CpuCompositeOverlay& b){
        return CpuCompositeOverlay(pgn_psp_native::CompositePage::merge(a.page_,b.page_));
    }
    uint64_t events()const{return page_.events();}
    void assertCovers(Number96 start,uint64_t length)const{
        const auto c=page_.cohort().start();const uint64_t lo=c.low+page_.cohort().length();
        const Number96 begin{c.low,uint32_t(c.high)},end{lo,uint32_t(c.high+(lo<c.low))};
        const uint64_t queryLow=start.low+length;const uint64_t queryHigh=uint64_t(start.high)+(queryLow<start.low);
        if(!length||queryHigh>UINT32_MAX||cmp(start,begin)<0||cmp({queryLow,uint32_t(queryHigh)},end)>0)
            throw std::runtime_error("Composite page does not cover owned CPU range");
    }
    uint64_t apply(Class30Bitmap& bitmap)const{
        const auto cohort=page_.cohort().start();
        const Number96 cohortBegin{cohort.low,uint32_t(cohort.high)};
        const auto first=bitmap.number(0),last=bitmap.number(bitmap.slots_-1);
        if(cmp(last,cohortBegin)<0)return 0;
        // The bitmap is at most one native descriptor; the cohort may contain
        // many. Visit only the CSR tiles intersecting this bitmap, not the page.
        auto boundedOffset=[&](Number96 n){
            if(cmp(n,cohortBegin)<0)return uint64_t(0);
            const uint64_t high=uint64_t(n.high)-cohort.high-uint64_t(n.low<cohort.low);
            if(high)return page_.cohort().length()-1;
            return std::min<uint64_t>(n.low-cohort.low,page_.cohort().length()-1);
        };
        const uint64_t firstOwner=(boundedOffset(first)/30*8)/pgn_psp_tile::TileSlots;
        const uint64_t lastOwner=(boundedOffset(last)/30*8+7)/pgn_psp_tile::TileSlots;
        const auto& offsets=page_.offsets();const auto& slots=page_.slots();uint64_t changed=0;
        for(uint64_t owner=firstOwner;owner<=lastOwner&&owner+1<offsets.size();++owner){
            for(uint32_t i=offsets[size_t(owner)];i<offsets[size_t(owner)+1];++i){
                const uint64_t sourceSlot=owner*pgn_psp_tile::TileSlots+slots[i];
                const uint64_t delta=(sourceSlot/8)*30+Classes30[sourceSlot&7];
                const uint64_t lo=cohort.low+delta;
                const Number96 n{lo,uint32_t(cohort.high+(lo<cohort.low))};
                if(cmp(n,first)<0||cmp(n,last)>0)continue;
                const uint64_t distance=n.low-bitmap.origin_.low;
                const uint64_t index=distance/30*8+unsigned(pgn_psp_native::Class[distance%30]);
                if(index>=bitmap.slots_||pgn_psp_native::Class[distance%30]<0)
                    throw std::runtime_error("Certified composite CPU mapping escaped");
                auto& word=bitmap.composite_[index>>6];const uint64_t mask=1ull<<(index&63);
                changed+=!(word&mask);word|=mask;
            }
        }
        return changed;
    }
};
}

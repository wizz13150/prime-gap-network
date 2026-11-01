#pragma once
#include <cstdint>
#include <cassert>
namespace seed_schedule {
constexpr uint64_t Unit=1ULL<<16;
inline uint64_t divide_up(uint64_t numerator,uint64_t denominator){
    return numerator/denominator+(numerator%denominator!=0);
}
template<unsigned Cache>
inline uint64_t next_bound(uint32_t high,uint64_t bound,unsigned shift){
    const uint64_t square=uint64_t(high)*high;
    const uint64_t delta=square+(1ULL<<(Cache-92));
    // All intermediates fit64 bits for the admitted76..79-bit domains.
    const uint64_t scaled=divide_up(delta*bound*(high+1),square);
    return Unit+divide_up(Unit,high)+divide_up(scaled,1ULL<<(Cache-64-shift));
}
template<unsigned Bits>
inline uint32_t make_for_prefix(uint32_t high,unsigned prefix){
    static_assert(Bits>=76&&Bits<=79,"Bounded seed-schedule domain");
    assert(high>=(1u<<(Bits-65))&&high<(1u<<(Bits-64)));
    constexpr unsigned Cache=Bits+31;
    const uint64_t square=uint64_t(high)*high;
    uint64_t bound=divide_up((square+(1ULL<<(Cache-92)))*Unit,square);
    unsigned remaining=prefix-(Cache-64),count=0,mask=0,last=0;
    bool cheap=false;
    while(remaining){
        unsigned shift=remaining<31?remaining:31;
        while(bound*(1ULL<<shift)>=(1ULL<<48))--shift;
        assert(shift>0&&count<8);
        const bool final=shift==remaining;
        if(final){
            const bool fits96=bound*(high+1)*(1ULL<<shift)<(1ULL<<48);
            const uint64_t narrowed=next_bound<96>(high,bound,shift);
            cheap=fits96&&narrowed<2*Unit;
            bound=cheap?narrowed:next_bound<Cache>(high,bound,shift);
            last=shift;
        }else{
            assert(shift==30||shift==31);
            mask|=(shift==31?1u:0u)<<count;
            bound=next_bound<Cache>(high,bound,shift);
        }
        remaining-=shift;++count;assert(bound<4*Unit);
    }
    // count4 | final_shift5 | preceding31-vs30 mask7 | canonical2 | cheap_tail.
    return count|(last<<4)|(mask<<9)|(uint32_t(bound>=2*Unit)<<16)|(uint32_t(cheap)<<17);
}
template<unsigned Bits>inline uint32_t make(uint32_t high){return make_for_prefix<Bits>(high,high>>(Bits-72));}

}

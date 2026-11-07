// Private native-host construction. No production activation. MIT.
#pragma once
#include "psp_tile_layout.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif
namespace pgn_psp_native {
struct Wide {uint64_t low{},high{};};
inline void require(bool okay,const char* message){if(!okay)throw std::invalid_argument(message);}
inline uint64_t wide_mod_portable(Wide n,uint64_t divisor){
    require(divisor!=0,"Zero native complement divisor");uint64_t r{};
    for(int bit=127;bit>=0;--bit){
        const bool carry=(r>>63)!=0;
        const auto word=bit>=64?n.high:n.low;
        r=(r<<1)|((word>>(bit&63))&1);
        if(carry||r>=divisor)r-=divisor;
    }
    return r;
}
inline uint64_t wide_mod(Wide n,uint64_t divisor){
    require(divisor!=0,"Zero native complement divisor");
#if defined(_MSC_VER) && defined(_M_X64)
    // _udiv128 requires its high limb<divisor. Reduction preserves N mod d
    // and prevents quotient overflow even at95bits with small moduli.
    uint64_t remainder{};_udiv128(n.high%divisor,n.low,divisor,&remainder);return remainder;
#elif defined(__SIZEOF_INT128__)
    return uint64_t(((static_cast<unsigned __int128>(n.high)<<64)|n.low)%divisor);
#else
    return wide_mod_portable(n,divisor);
#endif
}
inline uint32_t power(uint32_t base,uint32_t exponent,uint32_t prime){
    uint64_t result=1;
    while(exponent){if(exponent&1)result=result*base%prime;base=uint64_t(base)*base%prime;exponent>>=1;}
    return uint32_t(result);
}
struct Progression {uint32_t prime{},order{};uint64_t step{};};
class SmallFactorTable {
    friend struct FrozenCanonicalTable;
    std::vector<Progression> rows_;uint32_t ordinary_{},limit_{};
    SmallFactorTable(uint32_t ordinary,uint32_t limit):ordinary_(ordinary),limit_(limit){}
public:
    static SmallFactorTable build(uint32_t ordinary=80000,uint32_t limit=10000000){
        require(ordinary>=2&&ordinary<limit&&limit<=10000000,"Bounded prime factor scope required");
        SmallFactorTable out(ordinary,limit);std::vector<uint32_t> spf(limit+1);
        for(uint32_t p=2;uint64_t(p)*p<=limit;++p)if(!spf[p])
            for(uint32_t n=p*p;n<=limit;n+=p)if(!spf[n])spf[n]=p;
        for(uint32_t p=ordinary+1;p<=limit;++p){
            if(spf[p])continue;
            uint32_t order=p-1,remaining=p-1;
            while(remaining>1){
                const uint32_t factor=spf[remaining]?spf[remaining]:remaining;
                while(remaining%factor==0)remaining/=factor;
                while(order%factor==0&&power(2,order/factor,p)==1)order/=factor;
            }
            require(order&&power(2,order,p)==1,"Invalid multiplicative order");remaining=order;
            while(remaining>1){
                const uint32_t factor=spf[remaining]?spf[remaining]:remaining;
                require(power(2,order/factor,p)!=1,"Nonminimal multiplicative order");
                while(remaining%factor==0)remaining/=factor;
            }
            uint64_t step=uint64_t(p)*order;if(step&1)step*=2;
            out.rows_.push_back({p,order,step});
        }
        return out;
    }
    const std::vector<Progression>& rows()const{return rows_;}
    uint32_t ordinary()const{return ordinary_;}uint32_t limit()const{return limit_;}
};
inline constexpr uint64_t DescriptorSpan=uint64_t(pgn_psp_tile::DescriptorSlots/8)*30,MaximumCohort=1000000000000ull;
inline constexpr uint32_t MaximumEvents=16000000;
inline constexpr std::array<int,30> Class={-1,0,-1,-1,-1,-1,-1,1,-1,-1,-1,2,-1,3,-1,-1,-1,4,-1,5,-1,-1,-1,6,-1,-1,-1,-1,-1,7};
inline void validate_cohort(Wide start,uint64_t length){
    require(length&&length<=MaximumCohort,"Bounded physical complement cohort required");
    require(start.high>=3&&(start.high!=3||start.low>=std::numeric_limits<uint64_t>::max()-28),"Cohort below67bit domain");
    require(start.high<(1ull<<31),"Cohort begins outside95bit domain");
    const auto endLow=start.low+length,endHigh=start.high+(endLow<start.low);
    require(endHigh<(1ull<<31)||(endHigh==(1ull<<31)&&endLow==0),"Cohort crosses95bit end");
    require(wide_mod(start,30)==0,"Native class30 descriptor alignment required");
}
template<class Emit> inline void visit(const SmallFactorTable& table,Wide start,uint64_t length,Emit emit){
    validate_cohort(start,length);
    for(const auto& rule:table.rows()){
        const auto residue=wide_mod(start,rule.step);
        const auto first=(rule.step+rule.prime-residue)%rule.step;
        for(uint64_t at=first;at<length;){
            const auto bit=Class[at%30];
            if(bit>=0){const auto local=at%DescriptorSpan;emit(rule,at,at/DescriptorSpan,uint32_t((local/30)*8+unsigned(bit)));}
            if(rule.step>=length-at)break;
            at+=rule.step;
        }
    }
}
struct Page {std::vector<uint32_t> offsets,slots;uint64_t events{};};
inline Page build_page(const SmallFactorTable& table,Wide start,uint64_t length){
    validate_cohort(start,length);
    const auto descriptors=(length+DescriptorSpan-1)/DescriptorSpan;
    const auto buckets=descriptors*pgn_psp_tile::TilesPerDescriptor;
    require(buckets<=1048576,"Native complement tile budget exceeded");
    // A cohort uses two CSR passes. Derive each certified first landing only
    // once, bound to this exact start/table, and replay it in the same order.
    // This changes no progression, class30 test, duplicate or slot ordering.
    std::vector<uint64_t> first;first.reserve(table.rows().size());
    for(const auto& rule:table.rows()){
        const auto residue=wide_mod(start,rule.step);
        first.push_back((rule.step+rule.prime-residue)%rule.step);
    }
    const auto replay=[&](auto emit){
        size_t index=0;
        for(const auto& rule:table.rows()){
            for(uint64_t at=first[index++];at<length;){
                const auto bit=Class[at%30];
                if(bit>=0){const auto local=at%DescriptorSpan;emit(rule,at,at/DescriptorSpan,uint32_t((local/30)*8+unsigned(bit)));}
                if(rule.step>=length-at)break;
                at+=rule.step;
            }
        }
    };
    std::vector<uint32_t> counts(size_t(buckets),0);uint32_t total{};
    replay([&](const Progression&,uint64_t,uint64_t descriptor,uint32_t slot){
        require(total<MaximumEvents,"Native complement event budget exceeded");
        ++counts[size_t(descriptor*pgn_psp_tile::TilesPerDescriptor+(slot>>pgn_psp_tile::TileBits))];++total;
    });
    Page page;page.events=total;page.offsets.resize(size_t(buckets)+1);
    for(size_t i=0;i<counts.size();++i)page.offsets[i+1]=page.offsets[i]+counts[i];
    page.slots.resize(total);auto cursor=page.offsets;
    replay([&](const Progression&,uint64_t,uint64_t descriptor,uint32_t slot){
        const auto owner=size_t(descriptor*pgn_psp_tile::TilesPerDescriptor+(slot>>pgn_psp_tile::TileBits));
        page.slots[cursor[owner]++]=slot&(pgn_psp_tile::TileSlots-1);
    });
    return page;
}
// Safety: every generated N is p mod(p*ord), hence divisible by p>1;
// domainN>=2^66-29>p makes the cofactor nontrivial. Odd stride and class30
// filtering only discard marks. No true prime is removed. Scope completeness
// covers only PSPs having a prime factor ordinary<p<=limit, not the large tail.
}

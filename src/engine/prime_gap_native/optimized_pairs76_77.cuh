#pragma once
#include "montgomery_columns.cuh"
#include "seed_schedule.h"
#if defined(__CUDACC__)
#define P77_HD __host__ __device__ __forceinline__
#define P77_UNROLL _Pragma("unroll")
#else
#define P77_HD inline
#define P77_UNROLL
#endif
namespace optimized_pairs76_77 {
using Value=optimized_narrow_euler::Value;using Wide=montgomery_columns::Wide;
struct PreparedState{uint32_t muLower=0,seedPlan=0;};
P77_HD Value subtract_if_ge(Value a,Value n){
    if(a.z>n.z||(a.z==n.z&&(a.y>n.y||(a.y==n.y&&a.x>=n.x)))){
        uint64_t v=uint64_t(a.x)-n.x;a.x=uint32_t(v);v=uint64_t(a.y)-n.y-uint32_t(v>>63);a.y=uint32_t(v);a.z-=n.z+uint32_t(v>>63);
    }return a;
}
template<unsigned Bits>
P77_HD Value make_seed(uint64_t low,uint32_t high,uint32_t mu,uint32_t plan){
    static_assert(Bits==76||Bits==77,"Width-specific retained pair schedule");
    constexpr unsigned Cache=Bits+31;
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32);
    const uint64_t p0=uint64_t(mu)*n0,p1=uint64_t(mu)*n1+(p0>>32),p2=uint64_t(mu)*high+(p1>>32);
    uint64_t v=uint64_t(0)-uint32_t(p0);Value a{uint32_t(v),0,0};
    v=uint64_t(0)-uint32_t(p1)-uint32_t(v>>63);a.y=uint32_t(v);a.z=0u-uint32_t(p2)-uint32_t(v>>63);
    const unsigned count=plan&15,tail=(plan>>4)&31,mask=(plan>>9)&127;
    P77_UNROLL for(unsigned j=0;j<7;++j)if(j+1<count)
        a=montgomery_columns::fold<Cache>(Wide{a.x,a.y,a.z,0},30+((mask>>j)&1),low,high,mu);
    if(plan&(1u<<17))a=optimized_narrow_euler::shift_fold(a,tail,n0,n1,high,mu>>(Cache-96));
    else a=montgomery_columns::fold<Cache>(Wide{a.x,a.y,a.z,0},tail,low,high,mu);
    if(plan&(1u<<16))a=subtract_if_ge(a,Value{n0<<1,(n1<<1)|(n0>>31),2*high+(n1>>31)});
    return subtract_if_ge(a,Value{n0,n1,high});
}
template<unsigned Bits>
P77_HD unsigned pair_digit(uint64_t low,uint32_t high,unsigned group){
    const unsigned shift=Bits-11-2*group;
    if(shift>=63)return(high>>(shift-63))&3u;
    if(shift==62)return unsigned(((high&1u)<<1)|(low>>63));
    return unsigned((low>>1)>>shift)&3u;
}
template<unsigned Bits>
P77_HD Value state_before_terminal(uint64_t low,uint32_t high,uint32_t mu,uint32_t plan,uint32_t np){
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32),lower=mu>>(Bits-65);
    Value a=make_seed<Bits>(low,high,mu,plan);
    P77_UNROLL for(unsigned group=0;group<33;++group){
        a=montgomery_columns::square_fold_narrow<96>(a,n0,n1,np,high,lower);
        a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
        a=optimized_narrow_euler::shift_fold(a,pair_digit<Bits>(low,high,group),n0,n1,high,lower);
    }return a;
}
P77_HD uint32_t upper_product(uint32_t a,uint32_t b){
#if defined(__CUDA_ARCH__)
    return __umulhi(a,b);
#else
    return uint32_t(uint64_t(a)*b>>32);
#endif
}
P77_HD bool terminal(Wide a,uint64_t low,uint32_t high,unsigned digit,unsigned negative,uint32_t np,uint32_t maximum){
    const uint32_t q=0u-a.x*np;if(q>maximum)return false;
    const uint64_t p1=uint64_t(q)*uint32_t(low>>32)+upper_product(q,uint32_t(low)),p2=uint64_t(q)*high+(p1>>32);
    const uint64_t t=uint64_t(1)<<(32-digit),mask=uint64_t(0)-negative,delta=(t^mask)+negative;const uint32_t ext=0u-negative;
    Value target;
#if defined(__CUDA_ARCH__)
    asm("{\n\t.reg .u32 r1,r2,r3;\n\tadd.cc.u32 r1,%3,%6;\n\taddc.cc.u32 r2,%4,%7;\n\taddc.u32 r3,%5,%8;\n\t"
        "mov.u32 %0,r1;\n\tmov.u32 %1,r2;\n\tmov.u32 %2,r3;\n\t}"
        :"=r"(target.x),"=r"(target.y),"=r"(target.z)
        :"r"(uint32_t(p1)),"r"(uint32_t(p2)),"r"(uint32_t(p2>>32)),"r"(uint32_t(delta)),"r"(uint32_t(delta>>32)),"r"(ext));
#else
    uint64_t u=uint64_t(uint32_t(p1))+uint32_t(delta);target.x=uint32_t(u);
    u=uint64_t(uint32_t(p2))+uint32_t(delta>>32)+(u>>32);target.y=uint32_t(u);target.z=uint32_t(p2>>32)+ext+uint32_t(u>>32);
#endif
    return a.y==target.x&&a.z==target.y&&a.w==target.z;
}

template<unsigned Bits>
P77_HD bool test_prepared(uint64_t low,uint32_t high,uint32_t mu,uint32_t plan){
    if(!(low&1))return false;
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32),np=optimized_narrow_euler::negative_inverse32(n0),lower=mu>>(Bits-65);
    Value a=state_before_terminal<Bits>(low,high,mu,plan,np);
    if constexpr(Bits==77)a=montgomery_columns::square_fold_narrow<96>(a,n0,n1,np,high,lower);
    a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
    return terminal(Wide{a.x,a.y,a.z,0},low,high,unsigned((low>>1)&(Bits==76?1:3)),unsigned(((low>>1)^(low>>2))&1),np,(1u<<(Bits-63))-1);
}
}
#undef P77_HD
#undef P77_UNROLL

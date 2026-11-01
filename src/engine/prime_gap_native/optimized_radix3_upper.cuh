#pragma once
#include "montgomery_columns.cuh"
#if defined(__CUDACC__)
#define R74_HD __host__ __device__ __forceinline__
#define R74_UNROLL _Pragma("unroll")
#else
#define R74_HD inline
#define R74_UNROLL
#endif
namespace optimized_radix3_upper {
using Value=optimized_narrow_euler::Value;using Wide=montgomery_columns::Wide;
struct PreparedState{uint32_t muLower=0;};
constexpr uint32_t Split73=335,Split74=703;
template<unsigned Bits,bool Upper>struct Strategy{
    static_assert(Bits==73||Bits==74,"Width-specific strategy");
    static constexpr unsigned Cache=Bits==74&&Upper?105:104;
    static constexpr unsigned Middle=Bits==73&&!Upper?96:Cache;
    static constexpr uint32_t TerminalMax=Bits==73?(1u<<28)-1:(1u<<11)-1;
};
R74_HD Value canonical(Value a,uint64_t low,uint32_t high){
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32);
    if(a.z>high||(a.z==high&&(a.y>n1||(a.y==n1&&a.x>=n0)))){
        uint64_t v=uint64_t(a.x)-n0;a.x=uint32_t(v);v=uint64_t(a.y)-n1-uint32_t(v>>63);a.y=uint32_t(v);a.z-=high+uint32_t(v>>63);
    }return a;
}
template<unsigned Bits,bool Upper>
R74_HD Value make_seed(uint64_t low,uint32_t high,uint32_t mu){
    constexpr unsigned Cache=Strategy<Bits,Upper>::Cache;
    const uint32_t prefix=high>>(Bits-72);Value a{0,0,1};
    R74_UNROLL for(unsigned j=0;j<255/29;++j)if(j<prefix/29)
        a=montgomery_columns::fold<Cache>(Wide{a.x,a.y,a.z,0},29,low,high,mu);
    if(prefix%29)a=montgomery_columns::fold<Cache>(Wide{a.x,a.y,a.z,0},prefix%29,low,high,mu);
    return canonical(a,low,high);
}
template<unsigned Bits,bool Upper>
R74_HD Value state_after20(uint64_t low,uint32_t high,uint32_t mu,uint32_t np){
    constexpr unsigned Cache=Strategy<Bits,Upper>::Cache,Middle=Strategy<Bits,Upper>::Middle;
    const uint32_t middle=mu>>(Cache-Middle),lower=mu>>(Cache-96),n0=uint32_t(low),n1=uint32_t(low>>32);
    Value a=make_seed<Bits,Upper>(low,high,mu);const uint64_t exponent=(uint64_t(high)<<63)|(low>>1);
    R74_UNROLL for(unsigned group=0;group<20;++group){
        a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
        a=montgomery_columns::square_fold<Middle>(a,n0,n1,np,high,middle);
        a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
        unsigned digit;
        if constexpr(Bits==74)digit=group==0?unsigned(((high&3u)<<1)|(low>>63)):unsigned((exponent>>(62-3*group))&7u);
        else digit=unsigned((exponent>>(61-3*group))&7u);
        a=optimized_narrow_euler::shift_fold(a,digit,n0,n1,high,lower);
    }return a;
}
R74_HD uint32_t upper_product(uint32_t a,uint32_t b){
#if defined(__CUDA_ARCH__)
    return __umulhi(a,b);
#else
    return uint32_t(uint64_t(a)*b>>32);
#endif
}
R74_HD bool terminal(Wide a,uint64_t low,uint32_t high,unsigned digit,unsigned negative,uint32_t np,uint32_t maximum){
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
template<unsigned Bits,bool Upper>
R74_HD bool test_variant(uint64_t low,uint32_t high,uint32_t mu){
    if(!(low&1))return false;
    constexpr unsigned Cache=Strategy<Bits,Upper>::Cache;
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32),np=optimized_narrow_euler::negative_inverse32(n0);
    Value a=state_after20<Bits,Upper>(low,high,mu,np);a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
    Wide result;
    if constexpr(Bits==73){
        a=montgomery_columns::fold<Cache>(montgomery_columns::rawSquareWide(a,n0,n1,np,high),0,low,high,mu);
        a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
        result=montgomery_columns::rawSquareWide(a,n0,n1,np,high);
    }else{
        a=montgomery_columns::square_fold<Cache>(a,n0,n1,np,high,mu);
        if constexpr(Upper)a=canonical(a,low,high);
        a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
        a=montgomery_columns::square_fold<Cache>(a,n0,n1,np,high,mu);
        if constexpr(Upper)a=canonical(a,low,high);
        a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);result={a.x,a.y,a.z,0};
    }
    return terminal(result,low,high,unsigned((low>>1)&(Bits==73?15:31)),unsigned(((low>>1)^(low>>2))&1),np,Strategy<Bits,Upper>::TerminalMax);
}
}
#undef R74_HD
#undef R74_UNROLL

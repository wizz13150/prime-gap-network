#pragma once
#include "extended_columns.cuh"
#include "retained_columns.cuh"
#if defined(__CUDACC__)
#define W87_HD __host__ __device__ __forceinline__
#define W87_UNROLL _Pragma("unroll")
#else
#define W87_HD inline
#define W87_UNROLL
#endif
namespace optimized_pairs88_94 {
using Value=optimized_narrow_euler::Value;using Wide=montgomery_columns::Wide;
struct PreparedState{uint32_t muLower=0,muUpper=0;};
W87_HD Wide retained_raw(Value a,uint32_t n0,uint32_t n1,uint32_t np,uint32_t high){
#if defined(__CUDA_ARCH__)
 return retained_columns::square_raw(a,n0,n1,np,high);
#else
 return montgomery_columns::rawSquareWide(a,n0,n1,np,high);
#endif
}
W87_HD Wide product(uint32_t q,uint64_t low,uint32_t high){
    const uint64_t p0=uint64_t(q)*uint32_t(low),p1=uint64_t(q)*uint32_t(low>>32)+(p0>>32),p2=uint64_t(q)*high+(p1>>32);
    return{uint32_t(p0),uint32_t(p1),uint32_t(p2),uint32_t(p2>>32)};
}
W87_HD Wide subtract(Wide a,Wide b){
    uint64_t x=uint64_t(a.x)-b.x;Wide r{uint32_t(x),0,0,0};
    x=uint64_t(a.y)-b.y-uint32_t(x>>63);r.y=uint32_t(x);
    x=uint64_t(a.z)-b.z-uint32_t(x>>63);r.z=uint32_t(x);r.w=a.w-b.w-uint32_t(x>>63);return r;
}
W87_HD Wide shifted_wide(Wide a,unsigned bits){
    if(!bits)return a;
    const uint64_t x=uint64_t(a.x)<<bits,y=(uint64_t(a.y)<<bits)|(x>>32),z=(uint64_t(a.z)<<bits)|(y>>32),w=(uint64_t(a.w)<<bits)|(z>>32);
    assert(!(w>>32));return{uint32_t(x),uint32_t(y),uint32_t(z),uint32_t(w)};
}
W87_HD Value subtract_if_ge(Value a,Value n){
    if(a.z>n.z||(a.z==n.z&&(a.y>n.y||(a.y==n.y&&a.x>=n.x)))){
        uint64_t v=uint64_t(a.x)-n.x;a.x=uint32_t(v);v=uint64_t(a.y)-n.y-uint32_t(v>>63);a.y=uint32_t(v);a.z-=n.z+uint32_t(v>>63);
    }return a;
}

template<unsigned Bits,bool Upper>W87_HD constexpr unsigned cache_bits(){
    if constexpr(Bits==94)return Upper?128:126;
    else return Bits+31+(Upper?2:0);
}
template<unsigned Bits,bool Upper>W87_HD Value make_seed(uint64_t low,uint32_t high,uint32_t mu,uint32_t high_mu){
    constexpr unsigned Cache=cache_bits<Bits,Upper>(),SeedCache=Bits+31,Shift=Cache-SeedCache;
    uint32_t seed_mu;if constexpr(Shift==0)seed_mu=mu;else seed_mu=(mu>>Shift)|(high_mu<<(32-Shift));
    const auto p=product(seed_mu,low,high),r=subtract(Wide{0,0,0,0},p);Value a{r.x,r.y,r.z};
    unsigned remaining=(high>>(Bits-72))-(SeedCache-64);
    W87_UNROLL for(unsigned j=0;j<7;++j)if(remaining>=30){a=montgomery_columns::fold<SeedCache>(Wide{a.x,a.y,a.z,0},30,low,high,seed_mu);remaining-=30;}
    if(remaining)a=montgomery_columns::fold<SeedCache>(Wide{a.x,a.y,a.z,0},remaining,low,high,seed_mu);
    return subtract_if_ge(a,Value{uint32_t(low),uint32_t(low>>32),high});
}
template<unsigned Bits>W87_HD unsigned pair_digit(uint64_t low,uint32_t high,unsigned group){
    const unsigned shift=Bits-11-2*group;
    if(shift>=63)return(high>>(shift-63))&3u;
    if(shift==62)return unsigned(((high&1u)<<1)|(low>>63));
    return unsigned((low>>1)>>shift)&3u;
}
template<unsigned Bits,bool Upper>W87_HD Value first(Value a,uint64_t low,uint32_t high,uint32_t np,uint32_t mu,uint32_t high_mu){
    constexpr unsigned Cache=cache_bits<Bits,Upper>();
#if defined(__CUDA_ARCH__)
    if constexpr(Cache==Bits+31)return retained_columns::square_integer32<Cache>(a,uint32_t(low),uint32_t(low>>32),np,mu,high);
    else if constexpr(Bits==94&&!Upper)return retained_columns::square_integer33(a,uint32_t(low),uint32_t(low>>32),np,mu,high);
    else return extended_columns::square_fold<Cache>(a,uint32_t(low),uint32_t(low>>32),np,high,mu,high_mu);
#else
    if constexpr(Cache==Bits+31)return montgomery_columns::square_fold<Cache>(a,uint32_t(low),uint32_t(low>>32),np,high,mu);
    else return extended_columns::square_fold<Cache>(a,uint32_t(low),uint32_t(low>>32),np,high,mu,Bits==94&&!Upper?1:high_mu);
#endif
}
template<unsigned Bits,bool Upper>W87_HD Value last(Wide a,unsigned digit,uint64_t low,uint32_t high,uint32_t mu,uint32_t high_mu){
    constexpr unsigned Cache=cache_bits<Bits,Upper>();
#if defined(__CUDA_ARCH__)
    if constexpr(Cache==Bits+31)return retained_columns::last_integer32<Cache>(a,digit,low,mu,high);
    else if constexpr(Bits==94&&!Upper)return retained_columns::last_integer33(a,digit,low,mu,high);
    else return extended_columns::fold<Cache>(a,digit,low,high,mu,high_mu);
#else
    if constexpr(Cache==Bits+31)return montgomery_columns::fold<Cache>(a,digit,low,high,mu);
    else return extended_columns::fold<Cache>(a,digit,low,high,mu,Bits==94&&!Upper?1:high_mu);
#endif
}
template<unsigned Bits,bool Upper>W87_HD Value state_before_terminal(uint64_t low,uint32_t high,uint32_t mu,uint32_t high_mu,uint32_t np){
    constexpr unsigned Tail=(Bits&1)?2:1,Pairs=(Bits-9-Tail)/2;
    Value a=make_seed<Bits,Upper>(low,high,mu,high_mu);
    W87_UNROLL for(unsigned group=0;group<Pairs;++group){
        a=first<Bits,Upper>(a,low,high,np,mu,high_mu);
        const auto raw=retained_raw(a,uint32_t(low),uint32_t(low>>32),np,high);
        a=last<Bits,Upper>(raw,pair_digit<Bits>(low,high,group),low,high,mu,high_mu);
    }return a;
}
W87_HD uint32_t upper_product(uint32_t a,uint32_t b){
#if defined(__CUDA_ARCH__)
    return __umulhi(a,b);
#else
    return uint32_t(uint64_t(a)*b>>32);
#endif
}
W87_HD bool terminal(Wide a,uint64_t low,uint32_t high,unsigned digit,unsigned negative,uint32_t np,uint32_t maximum){
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



template<unsigned Bits,bool Upper>W87_HD bool test_prepared(uint64_t low,uint32_t high,uint32_t mu,uint32_t high_mu){
    if(!(low&1))return false;
    constexpr unsigned Tail=(Bits&1)?2:1;
    const auto np=optimized_narrow_euler::negative_inverse32(uint32_t(low));
    auto a=state_before_terminal<Bits,Upper>(low,high,mu,high_mu,np);
    if constexpr(Tail==2)a=first<Bits,Upper>(a,low,high,np,mu,high_mu);
    // At94 bits a canonical terminal input keeps every valid witness within32 bits.
    if constexpr(Bits==94)a=subtract_if_ge(a,Value{uint32_t(low),uint32_t(low>>32),high});
    const auto raw=retained_raw(a,uint32_t(low),uint32_t(low>>32),np,high);
    constexpr uint32_t Maximum=Bits==94?UINT32_MAX:uint32_t((uint64_t(1)<<(Bits-62))+2);
    return terminal(raw,low,high,unsigned((low>>1)&((1u<<Tail)-1)),unsigned(((low>>1)^(low>>2))&1),np,Maximum);
}
}
#undef W87_HD
#undef W87_UNROLL

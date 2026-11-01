#pragma once
#include "montgomery_columns.cuh"
#if defined(__CUDACC__)
#define E75_HD __host__ __device__ __forceinline__
#define E75_UNROLL _Pragma("unroll")
#else
#define E75_HD inline
#define E75_UNROLL
#endif
namespace optimized_euler75 {
using Value=optimized_narrow_euler::Value;using Wide=montgomery_columns::Wide;
struct PreparedState{uint32_t muLower=0;};
constexpr uint32_t SplitHigh=1148;
E75_HD Value canonical(Value a,uint64_t low,uint32_t high){
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32);
    if(a.z>high||(a.z==high&&(a.y>n1||(a.y==n1&&a.x>=n0)))){
        uint64_t v=uint64_t(a.x)-n0;a.x=uint32_t(v);v=uint64_t(a.y)-n1-uint32_t(v>>63);a.y=uint32_t(v);a.z-=high+uint32_t(v>>63);
    }return a;
}
E75_HD Value make_seed(uint64_t low,uint32_t high,uint32_t mu){
    const uint32_t prefix=high>>3;Value a{0,0,1};
    E75_UNROLL for(unsigned j=0;j<255/29;++j)if(j<prefix/29)
        a=montgomery_columns::fold<106>(Wide{a.x,a.y,a.z,0},29,low,high,mu);
    if(prefix%29)a=montgomery_columns::fold<106>(Wide{a.x,a.y,a.z,0},prefix%29,low,high,mu);
    return canonical(a,low,high);
}
template<bool Upper>
E75_HD Value middle(Value a,uint64_t low,uint32_t high,uint32_t mu,uint32_t np){
    if constexpr(Upper)return montgomery_columns::square_fold_wide_q<106>(a,uint32_t(low),uint32_t(low>>32),np,high,mu);
    return montgomery_columns::square_fold<106>(a,uint32_t(low),uint32_t(low>>32),np,high,mu);
}
template<bool Upper>
E75_HD Value residue(uint64_t low,uint32_t high,uint32_t mu){
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32),np=optimized_narrow_euler::negative_inverse32(n0),lower=mu>>10;
    Value a=make_seed(low,high,mu);const uint64_t exponent=low>>1;
    E75_UNROLL for(unsigned group=0;group<22;++group){
        a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
        a=middle<Upper>(a,low,high,mu,np);
        a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
        const unsigned digit=group==0?unsigned(high&7u):unsigned((exponent>>(63-3*group))&7u);
        if constexpr(Upper)a=montgomery_columns::fold<106>(Wide{a.x,a.y,a.z,0},digit,low,high,mu);
        else a=optimized_narrow_euler::shift_fold(a,digit,n0,n1,high,lower);
    }return a;
}
template<bool Upper>
E75_HD bool test_variant(uint64_t low,uint32_t high,uint32_t mu){
    assert(high>=1024&&high<2048);if(!(low&1))return false;
    const auto a=residue<Upper>(low,high,mu);const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32);
    if(((low>>1)^(low>>2))&1)
        return optimized_narrow_euler::equals(a,Value{n0,n1,high-1})||optimized_narrow_euler::equals(a,Value{n0<<1,uint32_t((low<<1)>>32),2*high-1+uint32_t(low>>63)});
    return optimized_narrow_euler::equals(a,Value{0,0,1})||optimized_narrow_euler::equals(a,Value{n0,n1,high+1});
}
}
#undef E75_HD
#undef E75_UNROLL

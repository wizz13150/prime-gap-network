#pragma once
#include "montgomery_columns.cuh"
#if defined(__CUDACC__)
#define E72_HD __host__ __device__ __forceinline__
#define E72_UNROLL _Pragma("unroll")
#else
#define E72_HD inline
#define E72_UNROLL
#endif
namespace optimized_euler72 {
using Value=optimized_narrow_euler::Value;using Wide=montgomery_columns::Wide;
struct PreparedState {uint32_t muLower=0;};
E72_HD Value fold_wide(Wide t,uint64_t low,uint32_t high,uint32_t mu) {
    Value d;const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32);
#if defined(__CUDA_ARCH__)
    asm("{\n\t.reg .u32 q,p0,p1,p2;\n\t"
        "mul.hi.u32 q,%5,%10;\n\tmad.lo.u32 q,%6,%10,q;\n\t"
        "mul.lo.u32 p0,q,%7;\n\tmul.hi.u32 p1,q,%7;\n\t"
        "mad.lo.cc.u32 p1,q,%8,p1;\n\tmadc.hi.u32 p2,q,%8,0;\n\t"
        "mad.lo.u32 p2,q,%9,p2;\n\t"
        "sub.cc.u32 %0,%3,p0;\n\tsubc.cc.u32 %1,%4,p1;\n\tsubc.u32 %2,%5,p2;\n\t}"
        :"=r"(d.x),"=r"(d.y),"=r"(d.z)
        :"r"(t.x),"r"(t.y),"r"(t.z),"r"(t.w),"r"(n0),"r"(n1),"r"(high),"r"(mu));
#else
    const uint64_t q=(uint64_t(t.z)*mu>>32)+uint64_t(t.w)*mu;assert(q<=UINT32_MAX);
    const uint64_t p0=q*n0,p1=q*n1+(p0>>32);uint64_t s=uint64_t(t.x)-uint32_t(p0);d.x=uint32_t(s);
    s=uint64_t(t.y)-uint32_t(p1)-uint32_t(s>>63);d.y=uint32_t(s);d.z=t.z-uint32_t(q*high+(p1>>32))-uint32_t(s>>63);
#endif
    return d;
}
E72_HD Value make_seed(uint64_t low,uint32_t high,uint32_t mu) {
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32);Value a{0,0,1};
    E72_UNROLL for(unsigned j=0;j<255/22;++j)if(j<high/22)
        a=optimized_narrow_euler::shift_fold(a,22,n0,n1,high,mu);
    if(high%22)a=optimized_narrow_euler::shift_fold(a,high%22,n0,n1,high,mu);
    if(a.z>high||(a.z==high&&(a.y>n1||(a.y==n1&&a.x>=n0)))){
        uint64_t s=uint64_t(a.x)-n0;a.x=uint32_t(s);s=uint64_t(a.y)-n1-uint32_t(s>>63);a.y=uint32_t(s);a.z-=high+uint32_t(s>>63);
    }
    return a;
}
template<bool WideMiddle>
E72_HD Value residue_variant(uint64_t low,uint32_t high,uint32_t mu) {
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32),np=optimized_narrow_euler::negative_inverse32(n0);
    Value a=make_seed(low,high,mu);uint64_t stream=low;
    E72_UNROLL for(unsigned group=0;group<21;++group){
        a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
        if constexpr(WideMiddle)a=fold_wide(montgomery_columns::rawSquareWide(a,n0,n1,np,high),low,high,mu);
        else a=optimized_narrow_euler::square_fold(a,n0,n1,np,high,mu);
        a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
        a=optimized_narrow_euler::shift_fold(a,unsigned(stream>>61),n0,n1,high,mu);stream<<=3;
    }
    return a;
}
template<bool WideMiddle>
E72_HD bool test_variant(uint64_t low,uint32_t high,uint32_t mu){
    assert(high>=128&&high<256);if(!(low&1))return false;
    const auto a=residue_variant<WideMiddle>(low,high,mu);const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32);
    if(((low>>1)^(low>>2))&1)
        return optimized_narrow_euler::equals(a,Value{n0,n1,high-1})||optimized_narrow_euler::equals(a,Value{n0<<1,uint32_t((low<<1)>>32),2*high-1+uint32_t(low>>63)});
    return optimized_narrow_euler::equals(a,Value{0,0,1})||optimized_narrow_euler::equals(a,Value{n0,n1,high+1});
}
constexpr uint32_t NarrowMiddleHighEnd=232;
E72_HD Value residue(uint64_t low,uint32_t high,uint32_t mu) {
    if(high<NarrowMiddleHighEnd)return residue_variant<false>(low,high,mu);
    return residue_variant<true>(low,high,mu);
}
E72_HD bool test_prepared(uint64_t low,uint32_t high,uint32_t mu) {
    if(high<NarrowMiddleHighEnd)return test_variant<false>(low,high,mu);
    return test_variant<true>(low,high,mu);
}

}
#undef E72_HD
#undef E72_UNROLL

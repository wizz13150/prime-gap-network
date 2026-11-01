#pragma once
#include "optimized_narrow_euler.cuh"
#if defined(__CUDACC__)
#define E71_HD __host__ __device__ __forceinline__
#define E71_UNROLL _Pragma("unroll")
#else
#define E71_HD inline
#define E71_UNROLL
#endif
namespace optimized_radix3 {
using Value=optimized_narrow_euler::Value;
struct PreparedState {uint32_t muLower=0;};
E71_HD Value canonical_seed(Value a,uint64_t low,uint32_t high) {
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32);
    if(a.z>high||(a.z==high&&(a.y>n1||(a.y==n1&&a.x>=n0)))) {
        uint64_t v=uint64_t(a.x)-n0;a.x=uint32_t(v);
        v=uint64_t(a.y)-n1-uint32_t(v>>63);a.y=uint32_t(v);a.z-=high+uint32_t(v>>63);
    }
    return a;
}
template<unsigned Bits>
E71_HD Value make_seed(uint64_t low,uint32_t high,uint32_t mu) {
    static_assert(Bits==70||Bits==71,"Radix-three width specialization");
    constexpr unsigned SeedBits=94-Bits,LowPrefixBits=72-Bits;
    const uint32_t prefix=(high<<LowPrefixBits)|uint32_t(low>>(64-LowPrefixBits));
    const unsigned whole=prefix/SeedBits,tail=prefix%SeedBits;
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32);Value a{0,0,1};
    E71_UNROLL for(unsigned j=0;j<255/SeedBits;++j)if(j<whole)
        a=optimized_narrow_euler::shift_fold(a,SeedBits,n0,n1,high,mu);
    if(tail)a=optimized_narrow_euler::shift_fold(a,tail,n0,n1,high,mu);
    return canonical_seed(a,low,high);
}
template<unsigned Bits>
E71_HD Value residue(uint64_t low,uint32_t high,uint32_t mu) {
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32),np=optimized_narrow_euler::negative_inverse32(n0);
    Value a=make_seed<Bits>(low,high,mu);uint64_t stream=low<<(72-Bits);
    E71_UNROLL for(unsigned group=0;group<20;++group) {
        a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
        a=optimized_narrow_euler::square_fold(a,n0,n1,np,high,mu);
        a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
        a=optimized_narrow_euler::shift_fold(a,unsigned(stream>>61),n0,n1,high,mu);
        stream<<=3;
    }
    a=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
    if constexpr(Bits==71)a=optimized_narrow_euler::square_fold(a,n0,n1,np,high,mu);
    return optimized_narrow_euler::shift_fold(a,unsigned((low>>1)&(Bits==70?1:3)),n0,n1,high,mu);
}
template<unsigned Bits>
E71_HD bool test_prepared(uint64_t low,uint32_t high,uint32_t mu) {
    assert(high>=(1u<<(Bits-65))&&high<(1u<<(Bits-64)));
    if(!(low&1))return false;
    const auto a=residue<Bits>(low,high,mu);const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32);
    if(((low>>1)^(low>>2))&1)
        return optimized_narrow_euler::equals(a,Value{n0,n1,high-1})||optimized_narrow_euler::equals(a,Value{n0<<1,uint32_t((low<<1)>>32),2*high-1+uint32_t(low>>63)});
    return optimized_narrow_euler::equals(a,Value{0,0,1})||optimized_narrow_euler::equals(a,Value{n0,n1,high+1});
}
}
#undef E71_HD
#undef E71_UNROLL

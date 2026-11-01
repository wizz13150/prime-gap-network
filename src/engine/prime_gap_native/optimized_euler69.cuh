#pragma once
#include "optimized_narrow_euler.cuh"
namespace optimized_euler69 {
using Value=optimized_narrow_euler::Value;
struct State {uint32_t bucket=0,mu=0;};
struct PreparedState {uint32_t muLower=0;};
#if defined(__CUDACC__)
#define E69_HD __host__ __device__ __forceinline__
#define E69_UNROLL _Pragma("unroll")
#else
#define E69_HD inline
#define E69_UNROLL
#endif
E69_HD Value shift_full(Value a,unsigned bits,uint64_t low,uint32_t high,uint32_t mu) {
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32);
#if defined(__CUDA_ARCH__)
    Value d;uint32_t carry;
    asm("{\n\t.reg .u32 x,y,z,c,q,p0,p1,p2;\n\t"
        "shl.b32 x,%4,%7;\n\tshf.l.wrap.b32 y,%4,%5,%7;\n\t"
        "shf.l.wrap.b32 z,%5,%6,%7;\n\tshf.l.wrap.b32 c,%6,0,%7;\n\t"
        "mul.hi.u32 q,z,%10;\n\tmad.lo.u32 q,c,%10,q;\n\t"
        "mul.lo.u32 p0,q,%8;\n\tmul.hi.u32 p1,q,%8;\n\t"
        "mad.lo.cc.u32 p1,q,%9,p1;\n\tmadc.hi.u32 p2,q,%9,0;\n\t"
        "mad.lo.u32 p2,q,%11,p2;\n\t"
        "sub.cc.u32 %0,x,p0;\n\tsubc.cc.u32 %1,y,p1;\n\tsubc.u32 %2,z,p2;\n\t"
        "mov.u32 %3,c;\n\t}"
        :"=r"(d.x),"=r"(d.y),"=r"(d.z),"=r"(carry)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(bits),"r"(n0),"r"(n1),"r"(mu),"r"(high));
    return d;
#else
    const uint64_t x=uint64_t(a.x)<<bits,y=(uint64_t(a.y)<<bits)|(x>>32),z=(uint64_t(a.z)<<bits)|(y>>32);
    const uint64_t q=(uint64_t(uint32_t(z))*mu>>32)+(z>>32)*mu;assert(q<=UINT32_MAX);
    const uint64_t p0=q*n0,p1=q*n1+(p0>>32);
    uint64_t s=uint64_t(uint32_t(x))-uint32_t(p0);Value d{uint32_t(s),0,0};
    s=uint64_t(uint32_t(y))-uint32_t(p1)-uint32_t(s>>63);d.y=uint32_t(s);
    d.z=uint32_t(z)-uint32_t(q*high+(p1>>32))-uint32_t(s>>63);return d;
#endif
}
template<uint32_t High>
E69_HD Value residue(uint64_t low,uint32_t mu) {
    static_assert(High>=16&&High<32,"69-bit high word");
    constexpr unsigned group=High<=21?4:3,seedBits=High<=21?26:25;
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32),np=optimized_narrow_euler::negative_inverse32(n0);
    Value a{0,0,1};
    E69_UNROLL for(unsigned j=0;j<(High*8)/seedBits;++j)
        a=optimized_narrow_euler::shift_fold(a,seedBits,n0,n1,High,mu);
    const unsigned tail=(High*8)%seedBits,nibble=unsigned(low>>61);
    if(tail+nibble<=seedBits){if(tail+nibble)a=optimized_narrow_euler::shift_fold(a,tail+nibble,n0,n1,High,mu);}
    else{if(tail)a=optimized_narrow_euler::shift_fold(a,tail,n0,n1,High,mu);if(nibble)a=optimized_narrow_euler::shift_fold(a,nibble,n0,n1,High,mu);}
    uint64_t stream=low<<3;
    E69_UNROLL for(unsigned j=0;j<60/group;++j) {
        if constexpr(group==4) {
            a=optimized_narrow_euler::square_raw(a,n0,n1,np,High);
            a=optimized_narrow_euler::square_fold(a,n0,n1,np,High,mu);
            a=optimized_narrow_euler::square_raw(a,n0,n1,np,High);
            a=optimized_narrow_euler::square_raw(a,n0,n1,np,High);
            a=shift_full(a,unsigned(stream>>60),low,High,mu);
        } else {
            a=optimized_narrow_euler::square_fold(a,n0,n1,np,High,mu);
            a=optimized_narrow_euler::square_raw(a,n0,n1,np,High);
            a=optimized_narrow_euler::square_raw(a,n0,n1,np,High);
            a=optimized_narrow_euler::shift_fold(a,unsigned(stream>>61),n0,n1,High,mu);
        }
        stream<<=group;
    }
    const Value twoN{n0<<1,(n1<<1)|(n0>>31),2*High+uint32_t(low>>63)};
    if(a.z>twoN.z||(a.z==twoN.z&&(a.y>twoN.y||(a.y==twoN.y&&a.x>=twoN.x)))){
        uint64_t t=uint64_t(a.x)-twoN.x;a.x=uint32_t(t);
        t=uint64_t(a.y)-twoN.y-uint32_t(t>>63);a.y=uint32_t(t);a.z-=twoN.z+uint32_t(t>>63);
    }
    return a;
}
template<uint32_t High>
E69_HD bool test_prepared(uint64_t low,uint32_t mu) {
    if(!(low&1))return false;
    const auto a=residue<High>(low,mu);const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32);
    if(((low>>1)^(low>>2))&1)
        return optimized_narrow_euler::equals(a,Value{n0,n1,High-1})||optimized_narrow_euler::equals(a,Value{n0<<1,uint32_t((low<<1)>>32),2*High-1+uint32_t(low>>63)});
    return optimized_narrow_euler::equals(a,Value{0,0,1})||optimized_narrow_euler::equals(a,Value{n0,n1,High+1});
}
template<uint32_t High>
E69_HD bool test(uint64_t low,State&state) {
    const uint32_t bucket=uint32_t(low>>36);
    if(!state.mu||state.bucket!=bucket){state.bucket=bucket;state.mu=optimized_narrow_euler::build_mu((uint64_t(bucket)<<36)|((1ULL<<36)-1),High);}
    return test_prepared<High>(low,state.mu);
}
}
#undef E69_HD
#undef E69_UNROLL

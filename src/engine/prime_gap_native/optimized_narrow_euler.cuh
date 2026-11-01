#pragma once
#include <stdint.h>
#include <assert.h>
#if defined(__CUDACC__)
#define OPT_HD __host__ __device__ __forceinline__
#else
#define OPT_HD inline
#endif
// Candidate optimized Euler/Jacobi predicate for every 67/68-bit odd modulus.
// The caller supplies the actual high word. There is no alternative search backend.
namespace optimized_narrow_euler {
struct Value { uint32_t x,y,z; };
struct State { uint32_t bucketTag=0,muLower=0; };
static_assert(sizeof(State)==8,"Two-word arithmetic cache");
OPT_HD bool admits_high(uint32_t high) { return high>=4 && high<16; }
OPT_HD int compare_product96(uint32_t q,uint64_t low,uint32_t high) {
    const uint64_t p0=uint64_t(q)*uint32_t(low);
    const uint64_t p1=uint64_t(q)*uint32_t(low>>32)+(p0>>32);
    const uint64_t p2=uint64_t(q)*high+(p1>>32);
    if(p2<(uint64_t(1)<<32))return -1;
    if(p2>(uint64_t(1)<<32))return 1;
    return (uint32_t(p0)|uint32_t(p1))?1:0;
}
OPT_HD uint32_t build_mu(uint64_t maxLow,uint32_t high) {
    uint32_t mu=0;
#if defined(__CUDA_ARCH__)
#pragma unroll 1
#endif
    for(int bit=30;bit>=0;--bit) {
        const uint32_t trial=mu|(uint32_t(1)<<bit);
        if(compare_product96(trial,maxLow,high)<=0)mu=trial;
    }
    return mu;
}
OPT_HD uint32_t negative_inverse32(uint32_t n) {
    uint32_t x=n;
    x*=2u-n*x;x*=2u-n*x;x*=2u-n*x;x*=2u-n*x;
    return 0u-x;
}
#if !defined(__CUDA_ARCH__)
OPT_HD void hostAddProduct(uint32_t &l,uint32_t &m,uint32_t &h,uint32_t x,uint32_t y) {
    const uint64_t p=uint64_t(x)*y;
    uint64_t s=uint64_t(l)+uint32_t(p);l=uint32_t(s);
    s=uint64_t(m)+(p>>32)+(s>>32);m=uint32_t(s);h+=uint32_t(s>>32);
}
#endif
OPT_HD Value square_raw(Value a, uint32_t n0, uint32_t n1, uint32_t np32, uint32_t high) {
    Value t;
#if defined(__CUDA_ARCH__)
    asm volatile("{\n\t"
                 ".reg .u32 l,m,h,q0,q1,z2,t0,t1,t2;\n\t"
                 "add.u32 z2,%5,%5;\n\t"
                 "mul.lo.u32 l,%3,%3;\n\t"
                 "mul.hi.u32 m,%3,%3;\n\t"
                 "mul.lo.u32 q0,l,%8;\n\t"
                 "mad.lo.cc.u32 l,q0,%6,l;\n\t"
                 "madc.hi.cc.u32 m,q0,%6,m;\n\t"
                 "addc.u32 h,0,0;\n\t"
                 "mov.u32 l,m;\n\t"
                 "mov.u32 m,h;\n\t"
                 "mad.lo.cc.u32 l,q0,%7,l;\n\t"
                 "madc.hi.u32 m,q0,%7,m;\n\t"
                 "mov.u32 h,0;\n\t"
                 "mad.lo.cc.u32 l,%3,%4,l;\n\t"
                 "madc.hi.cc.u32 m,%3,%4,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mad.lo.cc.u32 l,%3,%4,l;\n\t"
                 "madc.hi.cc.u32 m,%3,%4,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mul.lo.u32 q1,l,%8;\n\t"
                 "mad.lo.cc.u32 l,q1,%6,l;\n\t"
                 "madc.hi.cc.u32 m,q1,%6,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mov.u32 l,m;\n\t"
                 "mov.u32 m,h;\n\t"
                 "mad.lo.cc.u32 l,q0,%9,l;\n\t"
                 "madc.hi.u32 m,q0,%9,m;\n\t"
                 "mad.lo.cc.u32 l,%3,z2,l;\n\t"
                 "madc.hi.u32 m,%3,z2,m;\n\t"
                 "mov.u32 h,0;\n\t"
                 "mad.lo.cc.u32 l,q1,%7,l;\n\t"
                 "madc.hi.cc.u32 m,q1,%7,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mad.lo.cc.u32 l,%4,%4,l;\n\t"
                 "madc.hi.cc.u32 m,%4,%4,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mov.u32 t0,l;\n\t"
                 "mov.u32 l,m;\n\t"
                 "mov.u32 m,h;\n\t"
                 "mad.lo.cc.u32 l,q1,%9,l;\n\t"
                 "madc.hi.u32 m,q1,%9,m;\n\t"
                 "mad.lo.cc.u32 l,%4,z2,l;\n\t"
                 "madc.hi.u32 m,%4,z2,m;\n\t"
                 "mov.u32 t1,l;\n\t"
                 "mad.lo.u32 t2,%5,%5,m;\n\t"
                 "mov.u32 %0,t0;\n\t"
                 "mov.u32 %1,t1;\n\t"
                 "mov.u32 %2,t2;\n\t"
                 "}\n"
                 : "=r"(t.x), "=r"(t.y), "=r"(t.z)
                 : "r"(a.x), "r"(a.y), "r"(a.z), "r"(n0), "r"(n1), "r"(np32), "r"(high));
#else
    // Portable host arithmetic counterpart, NOT separately compiled here.
    const uint64_t xx = uint64_t(a.x) * a.x;
    uint32_t l = uint32_t(xx), m = uint32_t(xx >> 32), h = 0;
    const uint32_t q0 = l * np32;
    hostAddProduct(l, m, h, q0, n0);
    l = m;
    m = h;
    h = 0;
    hostAddProduct(l, m, h, q0, n1);
    hostAddProduct(l, m, h, a.x, a.y);
    hostAddProduct(l, m, h, a.x, a.y);
    const uint32_t q1 = l * np32;
    hostAddProduct(l, m, h, q1, n0);
    l = m;
    m = h;
    h = 0;
    hostAddProduct(l, m, h, q0, high);
    hostAddProduct(l, m, h, a.x, 2 * a.z);
    hostAddProduct(l, m, h, q1, n1);
    hostAddProduct(l, m, h, a.y, a.y);
    t.x = l;
    l = m;
    m = h;
    h = 0;
    hostAddProduct(l, m, h, q1, high);
    hostAddProduct(l, m, h, a.y, 2 * a.z);
    t.y = l;
    t.z = a.z * a.z + m;
#endif
    return t;
}


// mu=floor(2^96/Nmax) for the complete 2^36 bucket. All uses keep X<2^96.
OPT_HD Value fold(Value t,uint32_t n0,uint32_t n1,uint32_t high,uint32_t mu) {
#if defined(__CUDA_ARCH__)
    Value d;
    asm("{\n\t.reg .u32 q,p0,p1,p2,r0,r1,r2;\n\t"
        "mul.hi.u32 q,%5,%8;\n\t"
        "mul.lo.u32 p0,q,%6;\n\tmul.hi.u32 p1,q,%6;\n\t"
        "mad.lo.cc.u32 p1,q,%7,p1;\n\tmadc.hi.u32 p2,q,%7,0;\n\t"
        "mad.lo.u32 p2,q,%9,p2;\n\t"
        "sub.cc.u32 r0,%3,p0;\n\tsubc.cc.u32 r1,%4,p1;\n\tsubc.u32 r2,%5,p2;\n\t"
        "mov.u32 %0,r0;\n\tmov.u32 %1,r1;\n\tmov.u32 %2,r2;\n\t}"
        : "=r"(d.x),"=r"(d.y),"=r"(d.z)
        : "r"(t.x),"r"(t.y),"r"(t.z),"r"(n0),"r"(n1),"r"(mu),"r"(high));
    return d;
#else
    const uint32_t q=uint32_t((uint64_t(t.z)*mu)>>32);
    const uint64_t p0=uint64_t(q)*n0,p1=uint64_t(q)*n1+(p0>>32);
    const uint32_t p2=uint32_t(uint64_t(q)*high+(p1>>32));
    uint64_t s=uint64_t(t.x)-uint32_t(p0);Value d{uint32_t(s),0,0};
    s=uint64_t(t.y)-uint32_t(p1)-uint32_t(s>>63);d.y=uint32_t(s);
    d.z=t.z-p2-uint32_t(s>>63);return d;
#endif
}
OPT_HD Value shifted(Value a,uint32_t bits) {
    assert(bits<32);
#if defined(__CUDA_ARCH__)
    Value t;
    asm("{\n\t.reg .u32 x,y,z;\n\t"
        "shl.b32 x,%3,%6;\n\tshf.l.wrap.b32 y,%3,%4,%6;\n\t"
        "shf.l.wrap.b32 z,%4,%5,%6;\n\t"
        "mov.u32 %0,x;\n\tmov.u32 %1,y;\n\tmov.u32 %2,z;\n\t}"
        : "=r"(t.x),"=r"(t.y),"=r"(t.z)
        : "r"(a.x),"r"(a.y),"r"(a.z),"r"(bits));
    return t;
#else
    const uint64_t x=uint64_t(a.x)<<bits;
    const uint64_t y=(uint64_t(a.y)<<bits)|(x>>32);
    const uint64_t z=(uint64_t(a.z)<<bits)|(y>>32);
    assert((z>>32)==0);return {uint32_t(x),uint32_t(y),uint32_t(z)};
#endif
}
OPT_HD Value shift_fold(Value a,uint32_t bits,uint32_t n0,uint32_t n1,uint32_t high,uint32_t mu) {
    // A zero digit still folds the preceding wide square.
    return fold(shifted(a,bits),n0,n1,high,mu);
}
OPT_HD Value square_fold(Value a,uint32_t n0,uint32_t n1,uint32_t np,uint32_t high,uint32_t mu) {
    return fold(square_raw(a,n0,n1,np,high),n0,n1,high,mu);
}
OPT_HD bool equals(Value a,Value b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
template<class Cache>
OPT_HD bool test(uint64_t low,uint32_t high,Cache &state) {
    assert(admits_high(high));
    if(!(low&1))return false;
    const uint32_t bucket=uint32_t(low>>36),tag=(high<<28)|bucket;
    if(!state.muLower||state.bucketTag!=tag) {
        const uint64_t last=(uint64_t(bucket)<<36)|((uint64_t(1)<<36)-1);
        state.muLower=build_mu(last,high);state.bucketTag=tag;
    }
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32),mu=state.muLower,np=negative_inverse32(n0);
    Value a{0,0,1};
    const uint32_t prefix=high*16,chunks=prefix/26,tail=prefix%26,nibble=uint32_t(low>>60);
#if defined(__CUDA_ARCH__)
#pragma unroll
#endif
    for(unsigned j=0;j<9;++j)if(j<chunks)a=shift_fold(a,26,n0,n1,high,mu);
    if(tail+nibble<=26)a=shift_fold(a,tail+nibble,n0,n1,high,mu);
    else {a=shift_fold(a,tail,n0,n1,high,mu);a=shift_fold(a,nibble,n0,n1,high,mu);}
    uint64_t stream=((low&~uint64_t(15))<<4)|uint64_t(128);
#if defined(__CUDA_ARCH__)
#pragma unroll
#endif
    for(unsigned group=0;group<14;++group) {
        a=square_raw(a,n0,n1,np,high);
        a=square_fold(a,n0,n1,np,high,mu);
        a=square_raw(a,n0,n1,np,high);
        a=square_raw(a,n0,n1,np,high);
        a=shift_fold(a,uint32_t(stream>>60),n0,n1,high,mu);stream<<=4;
    }
    a=square_raw(a,n0,n1,np,high);
    a=square_fold(a,n0,n1,np,high,mu);
    a=square_raw(a,n0,n1,np,high);
    a=shift_fold(a,uint32_t((low>>1)&7),n0,n1,high,mu);
    if(((low>>1)^(low>>2))&1)
        return equals(a,Value{n0,n1,high-1})||equals(a,Value{n0<<1,uint32_t((low<<1)>>32),2*high-1+uint32_t(low>>63)});
    return equals(a,Value{0,0,1})||equals(a,Value{n0,n1,high+1});
}
} // namespace optimized_narrow_euler
#undef OPT_HD

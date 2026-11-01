#pragma once
#include "optimized_narrow_euler.cuh"
#if defined(__CUDACC__)
#define MONT_HD __host__ __device__ __forceinline__
#else
#define MONT_HD inline
#endif
namespace montgomery_columns {
using Value=optimized_narrow_euler::Value;
struct Wide {uint32_t x,y,z,w;};
// R64 Montgomery square columns. Caller proves a.z<2^31 and full result<2^128.
MONT_HD void hostAddProduct(uint32_t &l,uint32_t &m,uint32_t &h,uint32_t x,uint32_t y) {
    const uint64_t p=uint64_t(x)*y;
    uint64_t s=uint64_t(l)+uint32_t(p);l=uint32_t(s);
    s=uint64_t(m)+(p>>32)+(s>>32);m=uint32_t(s);h+=uint32_t(s>>32);
}
MONT_HD Wide rawSquareWide(Value a, uint32_t n0, uint32_t n1, uint32_t np32, uint32_t high) {
    Wide t;
#if defined(__CUDA_ARCH__)
    asm volatile("{\n\t"
                 ".reg .u32 l,m,h,q0,q1,z2,t0,t1,t2,t3;\n\t"
                 "add.u32 z2,%6,%6;\n\t"
                 "mul.lo.u32 l,%4,%4;\n\t"
                 "mul.hi.u32 m,%4,%4;\n\t"
                 "mul.lo.u32 q0,l,%9;\n\t"
                 "mad.lo.cc.u32 l,q0,%7,l;\n\t"
                 "madc.hi.cc.u32 m,q0,%7,m;\n\t"
                 "addc.u32 h,0,0;\n\t"
                 "mov.u32 l,m;\n\t"
                 "mov.u32 m,h;\n\t"
                 "mad.lo.cc.u32 l,q0,%8,l;\n\t"
                 "madc.hi.cc.u32 m,q0,%8,m;\n\t"
                  "addc.u32 h,0,0;\n\t"
                 "mad.lo.cc.u32 l,%4,%5,l;\n\t"
                 "madc.hi.cc.u32 m,%4,%5,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mad.lo.cc.u32 l,%4,%5,l;\n\t"
                 "madc.hi.cc.u32 m,%4,%5,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mul.lo.u32 q1,l,%9;\n\t"
                 "mad.lo.cc.u32 l,q1,%7,l;\n\t"
                 "madc.hi.cc.u32 m,q1,%7,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mov.u32 l,m;\n\t"
                 "mov.u32 m,h;\n\t"
                 "mad.lo.cc.u32 l,q0,%10,l;\n\t"
                 "madc.hi.u32 m,q0,%10,m;\n\t"
                 "mad.lo.cc.u32 l,%4,z2,l;\n\t"
                 "madc.hi.cc.u32 m,%4,z2,m;\n\t"
                  "addc.u32 h,0,0;\n\t"
                 "mad.lo.cc.u32 l,q1,%8,l;\n\t"
                 "madc.hi.cc.u32 m,q1,%8,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mad.lo.cc.u32 l,%5,%5,l;\n\t"
                 "madc.hi.cc.u32 m,%5,%5,m;\n\t"
                 "addc.u32 h,h,0;\n\t"
                 "mov.u32 t0,l;\n\t"
                 "mov.u32 l,m;\n\t"
                 "mov.u32 m,h;\n\t"
                 "mad.lo.cc.u32 l,q1,%10,l;\n\t"
                 "madc.hi.u32 m,q1,%10,m;\n\t"
                 "mad.lo.cc.u32 l,%5,z2,l;\n\t"
                 "madc.hi.cc.u32 m,%5,z2,m;\n\t"
                  "addc.u32 h,0,0;\n\t"
                 "mov.u32 t1,l;\n\t"
                 "mad.lo.cc.u32 t2,%6,%6,m;\n\t"
                 "madc.hi.u32 t3,%6,%6,h;\n\t"
                 "mov.u32 %0,t0;\n\t"
                 "mov.u32 %1,t1;\n\t"
                 "mov.u32 %2,t2;\n\t"
                 "mov.u32 %3,t3;\n\t"
                 "}\n"
                 : "=r"(t.x), "=r"(t.y), "=r"(t.z), "=r"(t.w)
                 : "r"(a.x), "r"(a.y), "r"(a.z), "r"(n0), "r"(n1), "r"(np32), "r"(high));
#else
    // Portable column counterpart; independent integer oracle is proposed separately.

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
    const uint64_t top = uint64_t(a.z) * a.z + m + (uint64_t(h) << 32);
    t.z = uint32_t(top);
    t.w = uint32_t(top >> 32);
#endif
    return t;
}

template<unsigned CacheBits>
inline uint32_t reciprocal(uint64_t low,uint32_t high) {
    static_assert(CacheBits>=96&&CacheBits<=126,"Reciprocal target fits128 bits");
    uint32_t mu=0;
    for(int bit=31;bit>=0;--bit){
        const uint32_t trial=mu|(1u<<bit);
        const uint64_t p0=uint64_t(trial)*uint32_t(low),p1=uint64_t(trial)*uint32_t(low>>32)+(p0>>32),p2=uint64_t(trial)*high+(p1>>32);
        constexpr uint64_t target=uint64_t(1)<<(CacheBits-64);
        if(p2<target||(p2==target&&!(uint32_t(p0)|uint32_t(p1))))mu=trial;
    }
    return mu;
}
template<unsigned CacheBits>
MONT_HD Value fold(Wide a,unsigned bits,uint64_t low,uint32_t high,uint32_t mu) {
    Value d;const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32);
#if defined(__CUDA_ARCH__)
    asm("{\n\t.reg .u32 t0,t1,t2,t3,q,p0,p1,p2; .reg .u64 column;\n\t"
        "shl.b32 t0,%3,%7;\n\tshf.l.wrap.b32 t1,%3,%4,%7;\n\t"
        "shf.l.wrap.b32 t2,%4,%5,%7;\n\tshf.l.wrap.b32 t3,%5,%6,%7;\n\t"
        "mul.hi.u32 p0,t2,%11;\n\tcvt.u64.u32 column,p0;\n\t"
        "mad.wide.u32 column,t3,%11,column;\n\tshr.u64 column,column,%12;\n\tcvt.u32.u64 q,column;\n\t"
        "mul.lo.u32 p0,q,%8;\n\tmul.hi.u32 p1,q,%8;\n\t"
        "mad.lo.cc.u32 p1,q,%9,p1;\n\tmadc.hi.u32 p2,q,%9,0;\n\tmad.lo.u32 p2,q,%10,p2;\n\t"
        "sub.cc.u32 %0,t0,p0;\n\tsubc.cc.u32 %1,t1,p1;\n\tsubc.u32 %2,t2,p2;\n\t}"
        :"=r"(d.x),"=r"(d.y),"=r"(d.z)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(a.w),"r"(bits),"r"(n0),"r"(n1),"r"(high),"r"(mu),"n"(CacheBits-96));
#else
    if(bits){
        const uint64_t x=uint64_t(a.x)<<bits,y=(uint64_t(a.y)<<bits)|(x>>32),z=(uint64_t(a.z)<<bits)|(y>>32),w=(uint64_t(a.w)<<bits)|(z>>32);
        assert(!(w>>32));a={uint32_t(x),uint32_t(y),uint32_t(z),uint32_t(w)};
    }
    const uint64_t column=(uint64_t(a.z)*mu>>32)+uint64_t(a.w)*mu,q=column>>(CacheBits-96);assert(q<=UINT32_MAX);
    const uint64_t p0=q*n0,p1=q*n1+(p0>>32);uint64_t s=uint64_t(a.x)-uint32_t(p0);d.x=uint32_t(s);
    s=uint64_t(a.y)-uint32_t(p1)-uint32_t(s>>63);d.y=uint32_t(s);d.z=a.z-uint32_t(q*high+(p1>>32))-uint32_t(s>>63);
#endif
    return d;
}

template<unsigned CacheBits>
MONT_HD Value square_fold(Value a,uint32_t n0,uint32_t n1,uint32_t np,uint32_t high,uint32_t mu){
#if defined(__CUDA_ARCH__)
    Value d;
    asm volatile("{\n\t"
                 ".reg .u32 l,m,h,q0,q1,z2,t0,t1,t2,t3;\n\t"
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
                 "madc.hi.cc.u32 m,q0,%7,m;\n\t"
                  "addc.u32 h,0,0;\n\t"
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
                 "madc.hi.cc.u32 m,%3,z2,m;\n\t"
                  "addc.u32 h,0,0;\n\t"
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
                 "madc.hi.cc.u32 m,%4,z2,m;\n\t"
                  "addc.u32 h,0,0;\n\t"
                 "mov.u32 t1,l;\n\t"
                 "mad.lo.cc.u32 t2,%5,%5,m;\n\t"
                 "madc.hi.u32 t3,%5,%5,h;\n\t"

        ".reg .u32 q,p0,p1,p2; .reg .u64 column;\n\t"
        "mul.hi.u32 p0,t2,%10;\n\tcvt.u64.u32 column,p0;\n\t"
        "mad.wide.u32 column,t3,%10,column;\n\tshr.u64 column,column,%11;\n\tcvt.u32.u64 q,column;\n\t"
        "mul.lo.u32 p0,q,%6;\n\tmul.hi.u32 p1,q,%6;\n\t"
        "mad.lo.cc.u32 p1,q,%7,p1;\n\tmadc.hi.u32 p2,q,%7,0;\n\tmad.lo.u32 p2,q,%9,p2;\n\t"
        "sub.cc.u32 %0,t0,p0;\n\tsubc.cc.u32 %1,t1,p1;\n\tsubc.u32 %2,t2,p2;\n\t}"
        :"=r"(d.x),"=r"(d.y),"=r"(d.z)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(n0),"r"(n1),"r"(np),"r"(high),"r"(mu),"n"(CacheBits-96));
    return d;
#else
    return fold<CacheBits>(rawSquareWide(a,n0,n1,np,high),0,uint64_t(n0)|(uint64_t(n1)<<32),high,mu);
#endif
}

template<unsigned CacheBits>
MONT_HD Value square_fold_wide_q(Value a,uint32_t n0,uint32_t n1,uint32_t np,uint32_t high,uint32_t mu){
#if defined(__CUDA_ARCH__)
    Value d;
    asm volatile("{\n\t"
                 ".reg .u32 l,m,h,q0,q1,z2,t0,t1,t2,t3;\n\t"
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
                 "madc.hi.cc.u32 m,q0,%7,m;\n\t"
                  "addc.u32 h,0,0;\n\t"
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
                 "madc.hi.cc.u32 m,%3,z2,m;\n\t"
                  "addc.u32 h,0,0;\n\t"
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
                 "madc.hi.cc.u32 m,%4,z2,m;\n\t"
                  "addc.u32 h,0,0;\n\t"
                 "mov.u32 t1,l;\n\t"
                 "mad.lo.cc.u32 t2,%5,%5,m;\n\t"
                 "madc.hi.u32 t3,%5,%5,h;\n\t"

        ".reg .u32 q,qh,p0,p1,p2; .reg .u64 column;\n\t"
        "mul.hi.u32 p0,t2,%10;\n\tcvt.u64.u32 column,p0;\n\t"
        "mad.wide.u32 column,t3,%10,column;\n\tshr.u64 column,column,%11;\n\tmov.b64 {q,qh},column;\n\t"
        "mul.lo.u32 p0,q,%6;\n\tmul.hi.u32 p1,q,%6;\n\t"
        "mad.lo.cc.u32 p1,q,%7,p1;\n\tmadc.hi.u32 p2,q,%7,0;\n\t"
        "mad.lo.cc.u32 p1,qh,%6,p1;\n\tmadc.hi.u32 p2,qh,%6,p2;\n\t"
        "mad.lo.u32 p2,q,%9,p2;\n\tmad.lo.u32 p2,qh,%7,p2;\n\t"
        "sub.cc.u32 %0,t0,p0;\n\tsubc.cc.u32 %1,t1,p1;\n\tsubc.u32 %2,t2,p2;\n\t}"
        :"=r"(d.x),"=r"(d.y),"=r"(d.z)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(n0),"r"(n1),"r"(np),"r"(high),"r"(mu),"n"(CacheBits-96));
    return d;
#else
    const auto t=rawSquareWide(a,n0,n1,np,high);
    const uint64_t q=((uint64_t(t.z)*mu>>32)+uint64_t(t.w)*mu)>>(CacheBits-96);assert(q<(1ULL<<34));
    const uint32_t ql=uint32_t(q),qh=uint32_t(q>>32);
    const uint64_t p0=uint64_t(ql)*n0,p1=uint64_t(ql)*n1+(p0>>32)+uint64_t(qh)*n0,
                   p2=uint64_t(ql)*high+uint64_t(qh)*n1+(p1>>32);
    // Products are needed modulo2^96; a carry beyond word2 is discarded.
    uint64_t s=uint64_t(t.x)-uint32_t(p0);Value r{uint32_t(s),0,0};
    s=uint64_t(t.y)-uint32_t(p1)-uint32_t(s>>63);r.y=uint32_t(s);r.z=t.z-uint32_t(p2)-uint32_t(s>>63);return r;
#endif
}

template<unsigned CacheBits>
MONT_HD Value square_fold_narrow(Value a,uint32_t n0,uint32_t n1,uint32_t np,uint32_t high,uint32_t mu){
#if defined(__CUDA_ARCH__)
    Value d;
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

        ".reg .u32 q,p0,p1,p2;\n\t"
        "mul.hi.u32 q,t2,%10;\n\tshr.u32 q,q,%11;\n\t"
        "mul.lo.u32 p0,q,%6;\n\tmul.hi.u32 p1,q,%6;\n\t"
        "mad.lo.cc.u32 p1,q,%7,p1;\n\tmadc.hi.u32 p2,q,%7,0;\n\tmad.lo.u32 p2,q,%9,p2;\n\t"
        "sub.cc.u32 %0,t0,p0;\n\tsubc.cc.u32 %1,t1,p1;\n\tsubc.u32 %2,t2,p2;\n\t}"
        :"=r"(d.x),"=r"(d.y),"=r"(d.z)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(n0),"r"(n1),"r"(np),"r"(high),"r"(mu),"n"(CacheBits-96));
    return d;
#else
    const auto t=optimized_narrow_euler::square_raw(a,n0,n1,np,high);
    return fold<CacheBits>(Wide{t.x,t.y,t.z,0},0,uint64_t(n0)|(uint64_t(n1)<<32),high,mu);
#endif
}

template<unsigned CacheBits>
MONT_HD Value shift_fold_narrow(Value a,unsigned bits,uint64_t low,uint32_t high,uint32_t mu){
    Value r;
#if defined(__CUDA_ARCH__)
    asm("{\n\t.reg .u32 t0,t1,t2,q,p0,p1,p2;\n\t"
        "shl.b32 t0,%3,%6;\n\tshf.l.wrap.b32 t1,%3,%4,%6;\n\tshf.l.wrap.b32 t2,%4,%5,%6;\n\t"
        "mul.hi.u32 q,t2,%9;\n\tshr.u32 q,q,%11;\n\t"
        "mul.lo.u32 p0,q,%7;\n\tmul.hi.u32 p1,q,%7;\n\t"
        "mad.lo.cc.u32 p1,q,%8,p1;\n\tmadc.hi.u32 p2,q,%8,0;\n\tmad.lo.u32 p2,q,%10,p2;\n\t"
        "sub.cc.u32 %0,t0,p0;\n\tsubc.cc.u32 %1,t1,p1;\n\tsubc.u32 %2,t2,p2;\n\t}"
        :"=r"(r.x),"=r"(r.y),"=r"(r.z)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(bits),"r"(uint32_t(low)),"r"(uint32_t(low>>32)),"r"(mu),"r"(high),"n"(CacheBits-96));
    return r;
#else
    const uint64_t x=uint64_t(a.x)<<bits,y=(uint64_t(a.y)<<bits)|(x>>32),z=(uint64_t(a.z)<<bits)|(y>>32);assert(!(z>>32));
    return fold<CacheBits>(Wide{a.x,a.y,a.z,0},bits,low,high,mu);
#endif
}

}
#undef MONT_HD

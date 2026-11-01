#pragma once
#include <stdint.h>
#include <assert.h>
#if defined(__CUDACC__)
#define MATH_HD __host__ __device__ __forceinline__
#else
#define MATH_HD inline
#endif
namespace scout_wide_math {
struct Value{uint32_t x,y,z;};
struct Wide{uint32_t x,y,z,w;};
MATH_HD void hostAddProduct(uint32_t &l,uint32_t &m,uint32_t &h,uint32_t x,uint32_t y) {
    const uint64_t p=uint64_t(x)*y;
    uint64_t s=uint64_t(l)+uint32_t(p);l=uint32_t(s);
    s=uint64_t(m)+(p>>32)+(s>>32);m=uint32_t(s);h+=uint32_t(s>>32);
}
MATH_HD Wide rawSquareWide(Value a, uint32_t n0, uint32_t n1, uint32_t np32, uint32_t high) {
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
MATH_HD Wide product(uint32_t q,uint64_t low,uint32_t high) {
    const uint64_t p0=uint64_t(q)*uint32_t(low);
    const uint64_t p1=uint64_t(q)*uint32_t(low>>32)+(p0>>32);
    const uint64_t p2=uint64_t(q)*high+(p1>>32);
    return {uint32_t(p0),uint32_t(p1),uint32_t(p2),uint32_t(p2>>32)};
}
MATH_HD Wide subtract(Wide a,Wide b) {
#if defined(__CUDA_ARCH__)
    Wide r;
    asm("{\n\t.reg .u32 x,y,z,w;\n\t"
        "sub.cc.u32 x,%4,%8;\n\tsubc.cc.u32 y,%5,%9;\n\t"
        "subc.cc.u32 z,%6,%10;\n\tsubc.u32 w,%7,%11;\n\t"
        "mov.u32 %0,x;\n\tmov.u32 %1,y;\n\tmov.u32 %2,z;\n\tmov.u32 %3,w;\n\t}"
        : "=r"(r.x),"=r"(r.y),"=r"(r.z),"=r"(r.w)
        : "r"(a.x),"r"(a.y),"r"(a.z),"r"(a.w),"r"(b.x),"r"(b.y),"r"(b.z),"r"(b.w));
    return r;
#else
    uint64_t s=uint64_t(a.x)-b.x;Wide r{uint32_t(s),0,0,0};
    s=uint64_t(a.y)-b.y-uint32_t(s>>63);r.y=uint32_t(s);
    s=uint64_t(a.z)-b.z-uint32_t(s>>63);r.z=uint32_t(s);
    r.w=a.w-b.w-uint32_t(s>>63);return r;
#endif
}
MATH_HD bool at_least(Wide a,uint64_t low,uint32_t high) {
    return a.w || a.z>high || (a.z==high &&
        (a.y>uint32_t(low>>32)||(a.y==uint32_t(low>>32)&&a.x>=uint32_t(low))));
}
template<unsigned Bits>
MATH_HD uint32_t quotient(Wide t,uint32_t mu) {
    // The omitted incoming carry contributes <2^-(Bits-65) to the quotient.
    // The full-domain rational bound still guarantees <=2 N-subtractions.
    const uint64_t top=uint64_t(t.w)*mu+((uint64_t(t.z)*mu)>>32);
    return uint32_t(top>>(Bits-65));
}
MATH_HD uint32_t negative_inverse(uint32_t n) {
    uint32_t x=n;x*=2u-n*x;x*=2u-n*x;x*=2u-n*x;x*=2u-n*x;return 0u-x;
}
MATH_HD Wide shifted(Value a,unsigned digit) {
    assert(digit<32);
    const uint64_t x=uint64_t(a.x)<<digit;
    const uint64_t y=(uint64_t(a.y)<<digit)|(x>>32);
    const uint64_t z=(uint64_t(a.z)<<digit)|(y>>32);
    return {uint32_t(x),uint32_t(y),uint32_t(z),uint32_t(z>>32)};
}
MATH_HD Wide shifted_wide(Wide a,unsigned bits) {
    assert(bits<32);
    if(!bits)return a;
    const uint64_t x=uint64_t(a.x)<<bits;
    const uint64_t y=(uint64_t(a.y)<<bits)|(x>>32);
    const uint64_t z=(uint64_t(a.z)<<bits)|(y>>32);
    const uint64_t w=(uint64_t(a.w)<<bits)|(z>>32);
    assert(!(w>>32));return {uint32_t(x),uint32_t(y),uint32_t(z),uint32_t(w)};
}
template<unsigned Bits>
MATH_HD unsigned exponent_digit(uint64_t low,uint32_t high,int shift,unsigned mask) {
    // A group can straddle low/high; preserve both bits at shift 63.
    if(shift>=64)return (high>>(shift-64))&mask;
    const uint64_t window=(low>>shift)|(shift?uint64_t(high)<<(64-shift):0);
    return unsigned(window)&mask;
}
MATH_HD Wide add(Wide a,Wide b) {
#if defined(__CUDA_ARCH__)
    Wide r;
    asm("{\n\t.reg .u32 x,y,z,w;\n\t"
        "add.cc.u32 x,%4,%8;\n\taddc.cc.u32 y,%5,%9;\n\t"
        "addc.cc.u32 z,%6,%10;\n\taddc.u32 w,%7,%11;\n\t"
        "mov.u32 %0,x;\n\tmov.u32 %1,y;\n\tmov.u32 %2,z;\n\tmov.u32 %3,w;\n\t}"
        :"=r"(r.x),"=r"(r.y),"=r"(r.z),"=r"(r.w)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(a.w),"r"(b.x),"r"(b.y),"r"(b.z),"r"(b.w));
    return r;
#else
    uint64_t v=uint64_t(a.x)+b.x;Wide r{uint32_t(v),0,0,0};
    v=uint64_t(a.y)+b.y+(v>>32);r.y=uint32_t(v);
    v=uint64_t(a.z)+b.z+(v>>32);r.z=uint32_t(v);
    r.w=a.w+b.w+uint32_t(v>>32);return r;
#endif
}
MATH_HD bool terminal_equation(Wide raw,uint64_t low,uint32_t high,uint32_t np,unsigned digit,bool negative) {
    // The final raw-square quotient is <N/2^64+2<2^32. Its low word
    // therefore fixes the complete q, as in the retained terminal equation.
    const uint32_t q=0u-raw.x*np;
    const Wide p=product(q,low,high);
    const Wide delta=digit?Wide{0,0x80000000u,0,0}:Wide{0,0,1,0};
    const Wide target=negative?subtract(p,delta):add(p,delta);
    return raw.x==target.x&&raw.y==target.y&&raw.z==target.z&&raw.w==target.w;
}
}
#undef MATH_HD

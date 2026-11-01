#pragma once
#include "wide_math.cuh"
#if defined(__CUDACC__)
#define R95_HD __host__ __device__ __forceinline__
#define R95_UNROLL _Pragma("unroll")
#else
#define R95_HD inline
#define R95_UNROLL
#endif
namespace optimized_retained95_low {
using scout_wide_math::Value;using scout_wide_math::Wide;
constexpr uint32_t HighBegin=uint32_t(1)<<30,HighEnd=uint32_t(5)<<28;
#if defined(__CUDA_ARCH__)
__device__ __forceinline__ Value square_digit_device(Value a, uint32_t n0, uint32_t n1,
                                                             uint32_t np32, uint32_t muL,
                                                             unsigned digit, uint32_t high) {
    Value d;
    // One CC-local block. l/m/h are a column plus carry, not a 64-bit C++
    // comparison or carry materialisation. Small terms precede full products.
    asm volatile("{\n\t"
                 ".reg .u32 l,m,h,q0,q1,z2,t0,t1,t2,t3,q,p0,p1,p2;\n\t"
                 ".reg .u64 column;\n\t .reg .f32 fz,fc,fq;\n\t"
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
                 "mad.lo.cc.u32 l,q0,%11,l;\n\t"
                 "madc.hi.u32 m,q0,%11,m;\n\t"
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
                 "mad.lo.cc.u32 l,q1,%11,l;\n\t"
                 "madc.hi.u32 m,q1,%11,m;\n\t"
                 "mad.lo.cc.u32 l,%4,z2,l;\n\t"
                 "madc.hi.u32 m,%4,z2,m;\n\t"
                 "mov.u32 t1,l;\n\t"
                 "mad.lo.cc.u32 t2,%5,%5,m;\n\t"
                 "madc.hi.u32 t3,%5,%5,0;\n\t"
                 "shf.l.wrap.b32 t3,t2,t3,%10;\n\t"
                 "shf.l.wrap.b32 t2,t1,t2,%10;\n\t"
                 "shf.l.wrap.b32 t1,t0,t1,%10;\n\t"
                 "shl.b32 t0,t0,%10;\n\t"
                 "mul.hi.u32 q,t2,%9;\n\t"
                 "mad.lo.cc.u32 q,t3,%9,q;\n\t"
                 "madc.hi.u32 p0,t3,%9,0;\n\t"
                 "shf.r.wrap.b32 q,q,p0,30;\n\t"
                 "add.u32 q,q,1;\n\t"
                 "mul.lo.u32 p0,q,%6;\n\t"
                 "mul.hi.u32 p1,q,%6;\n\t"
                 "mad.lo.cc.u32 p1,q,%7,p1;\n\t"
                 "madc.hi.u32 p2,q,%7,0;\n\t"
                 "mad.lo.u32 p2,q,%11,p2;\n\t"
                 "sub.cc.u32 %0,t0,p0;\n\t"
                 "subc.cc.u32 %1,t1,p1;\n\t"
                 "subc.u32 %2,t2,p2;\n\t"
                 ".reg .pred repair;\n\t"
                 "setp.lt.s32 repair,%2,0;\n\t"
                 "@repair add.cc.u32 %0,%0,%6;\n\t"
                 "@repair addc.cc.u32 %1,%1,%7;\n\t"
                 "@repair addc.u32 %2,%2,%11;\n\t"
                 "}\n"
                 : "=r"(d.x), "=r"(d.y), "=r"(d.z)
                 : "r"(a.x), "r"(a.y), "r"(a.z), "r"(n0), "r"(n1), "r"(np32), "r"(muL), "r"(digit), "r"(high));
    return d;
}
#endif
R95_HD Value square_digit(Value a,uint64_t low,uint32_t high,uint32_t np,uint32_t mu,unsigned digit) {
#if defined(__CUDA_ARCH__)
    return square_digit_device(a,uint32_t(low),uint32_t(low>>32),np,mu,digit,high);
#else
    const Wide x=scout_wide_math::shifted_wide(scout_wide_math::rawSquareWide(a,uint32_t(low),uint32_t(low>>32),np,high),digit);
    const uint32_t q=scout_wide_math::quotient<95>(x,mu)+1;
    Wide r=scout_wide_math::subtract(x,scout_wide_math::product(q,low,high));
    if(int32_t(r.w)<0)r=scout_wide_math::add(r,Wide{uint32_t(low),uint32_t(low>>32),high,0});
    assert(!r.w&&!scout_wide_math::at_least(r,low,high));return{r.x,r.y,r.z};
#endif
}
R95_HD Value seed_shift(Value a, uint32_t bits, uint32_t n0, uint32_t n1, uint32_t mu, uint32_t high) {
#if defined(__CUDA_ARCH__)
    Value d;
    asm volatile("{\n\t"
                 ".reg .u32 t0,t1,t2,t3,q,p0,p1,p2,r0,r1,r2;\n\t .reg .u64 column;\n\t"
                 "shl.b32 t0,%3,%6;\n\t shf.l.wrap.b32 t1,%3,%4,%6;\n\t"
                 "shf.l.wrap.b32 t2,%4,%5,%6;\n\t shf.l.wrap.b32 t3,%5,0,%6;\n\t"
                 "mul.hi.u32 p0,t2,%9;\n\t cvt.u64.u32 column,p0;\n\t"
                 "mad.wide.u32 column,t3,%9,column;\n\t"
                 "shr.u64 column,column,30;\n\t cvt.u32.u64 q,column;\n\t"
                 "mul.lo.u32 p0,q,%7;\n\t mul.hi.u32 p1,q,%7;\n\t"
                 "mad.lo.cc.u32 p1,q,%8,p1;\n\t madc.hi.u32 p2,q,%8,0;\n\t"
                 "mad.lo.u32 p2,q,%10,p2;\n\t"
                 "sub.cc.u32 r0,t0,p0;\n\t subc.cc.u32 r1,t1,p1;\n\t subc.u32 r2,t2,p2;\n\t"
                 "mov.u32 %0,r0;\n\t mov.u32 %1,r1;\n\t mov.u32 %2,r2;\n\t}"
                 : "=r"(d.x), "=r"(d.y), "=r"(d.z)
                 : "r"(a.x), "r"(a.y), "r"(a.z), "r"(bits), "r"(n0), "r"(n1), "r"(mu), "r"(high));
    return d;
#else
    const uint64_t x = uint64_t(a.x) << bits, y = (uint64_t(a.y) << bits) | (x >> 32),
                   z = (uint64_t(a.z) << bits) | (y >> 32);
    const Wide t{uint32_t(x), uint32_t(y), uint32_t(z), uint32_t(z >> 32)};
    const uint64_t column = (uint64_t(t.z) * mu >> 32) + uint64_t(t.w) * mu;
    const uint32_t q = uint32_t(column >> 30);
    const uint64_t p0 = uint64_t(q) * n0, p1 = uint64_t(q) * n1 + (p0 >> 32);
    const uint32_t p2 = uint32_t(uint64_t(q) * high + (p1 >> 32));
    uint64_t u = uint64_t(t.x) - uint32_t(p0);
    Value r{uint32_t(u), 0, 0};
    u = uint64_t(t.y) - uint32_t(p1) - uint32_t(u >> 63);
    r.y = uint32_t(u);
    r.z = t.z - p2 - uint32_t(u >> 63);
    return r;
#endif
}

R95_HD Value make_seed(uint64_t low,uint32_t high,uint32_t mu) {
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32),remaining=(high>>23)-62;
    const auto p=scout_wide_math::product(mu,low,high);
    const auto r=scout_wide_math::subtract(Wide{0,0,0,0},p);
    // The exact floor coefficient changes by at most one over this interval,
    // hence the true2^126 remainder is <2N<2^96; its low96 bits are complete.
    Value a{r.x,r.y,r.z};
    a=seed_shift(a,30,n0,n1,mu,high);a=seed_shift(a,30,n0,n1,mu,high);
    if(remaining>=90)a=seed_shift(a,30,n0,n1,mu,high);
    a=seed_shift(a,remaining%30,n0,n1,mu,high);
    if(scout_wide_math::at_least(Wide{a.x,a.y,a.z,0},low,high)){
        const auto v=scout_wide_math::subtract(Wide{a.x,a.y,a.z,0},Wide{n0,n1,high,0});a={v.x,v.y,v.z};
    }
    return a;
}

R95_HD bool test_prepared(uint64_t low,uint32_t high,uint32_t mu) {
    assert(high>=HighBegin&&high<HighEnd);if(!(low&1))return false;
    const uint32_t np=scout_wide_math::negative_inverse(uint32_t(low));
    Value a=make_seed(low,high,mu);
    R95_UNROLL for(int bit=86;bit>=2;--bit)a=square_digit(a,low,high,np,mu,scout_wide_math::exponent_digit<95>(low,high,bit,1));
    const Wide raw=scout_wide_math::rawSquareWide(a,uint32_t(low),uint32_t(low>>32),np,high);
    return scout_wide_math::terminal_equation(raw,low,high,np,unsigned(low>>1)&1,(((low>>1)^(low>>2))&1)!=0);
}
struct PreparedState {uint32_t muLower=0;};


}
#undef R95_UNROLL
#undef R95_HD

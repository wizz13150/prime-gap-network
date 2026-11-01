#pragma once
#include "wide_math.cuh"
#if defined(__CUDACC__)
#define F80_HD __host__ __device__ __forceinline__
#define F80_UNROLL _Pragma("unroll")
#else
#define F80_HD inline
#define F80_UNROLL
#endif
namespace optimized_float80 {
using scout_wide_math::Value;using scout_wide_math::Wide;
F80_HD uint32_t hostU32ToFloatRD(uint32_t x) {
    if (!x)
        return 0;
    int e = 31;
    while (!(x & (uint32_t(1) << e)))
        --e;
    uint32_t s = e > 23 ? x >> (e - 23) : x << (23 - e);
    return (uint32_t(e + 127) << 23) | (s & 0x7fffffu);
}
F80_HD uint32_t hostMultiplyFloatRD(uint32_t a, uint32_t b) {
    if (!a || !b)
        return 0;
    uint64_t p = uint64_t((a & 0x7fffffu) | (1u << 23)) * ((b & 0x7fffffu) | (1u << 23));
    int k = (p & (uint64_t(1) << 47)) ? 47 : 46,
        e = int((a >> 23) & 255) + int((b >> 23) & 255) - 300 + k;
    uint32_t s = uint32_t(p >> (k - 23));
    return (uint32_t(e + 127) << 23) | (s & 0x7fffffu);
}
F80_HD uint32_t hostFloatFloorU32(uint32_t a) {
    if (!a)
        return 0;
    int e = int((a >> 23) & 255) - 127;
    if (e < 0)
        return 0;
    uint32_t s = (a & 0x7fffffu) | (1u << 23);
    return e < 23 ? s >> (23 - e) : s << (e - 23);
}
F80_HD uint32_t coefficientBits(uint32_t mu) {
#if defined(__CUDA_ARCH__)
    return __float_as_uint(__uint2float_rd(mu)) - (47u << 23);
#else
    return hostU32ToFloatRD(mu) - (47u << 23);
#endif
}

F80_HD uint32_t hostU64ToFloatRD(uint64_t x) {
    if(!x)return 0;int e=63;while(!(x&(uint64_t(1)<<e)))--e;
    const uint64_t s=e>23?x>>(e-23):x<<(23-e);
    return(uint32_t(e+127)<<23)|(uint32_t(s)&0x7fffffu);
}
F80_HD uint32_t hostQuotient(Wide a,uint32_t coeff,unsigned digit) {
    const uint64_t top=uint64_t(a.z)|(uint64_t(a.w)<<32);
    return hostFloatFloorU32(hostMultiplyFloatRD(hostU64ToFloatRD(top),coeff+(digit<<23)));
}
F80_HD Value firstFloatWide(Value a,uint64_t low,uint32_t high,uint32_t np,uint32_t coeff) {
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
                 
        ".reg .f32 fz,fc,fq; .reg .u64 column;\n\t"
        "mov.b64 column,{t2,t3};\n\tcvt.rm.f32.u64 fz,column;\n\t"
        "mov.b32 fc,%10;\n\tmul.rm.f32 fq,fz,fc;\n\tcvt.rmi.u32.f32 q0,fq;\n\t"
        "mul.lo.u32 l,q0,%6;\n\tmul.hi.u32 m,q0,%6;\n\t"
        "mad.lo.cc.u32 m,q0,%7,m;\n\tmadc.hi.u32 h,q0,%7,0;\n\tmad.lo.u32 h,q0,%9,h;\n\t"
        "sub.cc.u32 %0,t0,l;\n\tsubc.cc.u32 %1,t1,m;\n\tsubc.u32 %2,t2,h;\n\t}"
        :"=r"(d.x),"=r"(d.y),"=r"(d.z)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(uint32_t(low)),"r"(uint32_t(low>>32)),"r"(np),"r"(high),"r"(coeff));
    return d;
#else
    const Wide raw=scout_wide_math::rawSquareWide(a,uint32_t(low),uint32_t(low>>32),np,high);
    const Wide r=scout_wide_math::subtract(raw,scout_wide_math::product(hostQuotient(raw,coeff,0),low,high));
    assert(!r.w);return{r.x,r.y,r.z};
#endif
}
F80_HD Value lastFloatWide(Wide a,unsigned digit,uint64_t low,uint32_t high,uint32_t coeff) {
#if defined(__CUDA_ARCH__)
    Value r;
    asm volatile("{\n\t.reg .f32 fz,fc,fq; .reg .u64 column;\n\t"
        ".reg .u32 cb,q,x,y,z,p0,p1,p2;\n\tmov.b64 column,{%5,%6};\n\t"
        "cvt.rm.f32.u64 fz,column;\n\tshl.b32 cb,%7,23;\n\tadd.u32 cb,cb,%10;\n\tmov.b32 fc,cb;\n\t"
        "mul.rm.f32 fq,fz,fc;\n\tcvt.rmi.u32.f32 q,fq;\n\t"
        "shl.b32 x,%3,%7;\n\tshf.l.wrap.b32 y,%3,%4,%7;\n\tshf.l.wrap.b32 z,%4,%5,%7;\n\t"
        "mul.lo.u32 p0,q,%8;\n\tmul.hi.u32 p1,q,%8;\n\t"
        "mad.lo.cc.u32 p1,q,%9,p1;\n\tmadc.hi.u32 p2,q,%9,0;\n\tmad.lo.u32 p2,q,%11,p2;\n\t"
        "sub.cc.u32 %0,x,p0;\n\tsubc.cc.u32 %1,y,p1;\n\tsubc.u32 %2,z,p2;\n\t}"
        :"=r"(r.x),"=r"(r.y),"=r"(r.z)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(a.w),"r"(digit),"r"(uint32_t(low)),"r"(uint32_t(low>>32)),"r"(coeff),"r"(high));
    return r;
#else
    const Wide x=scout_wide_math::shifted_wide(a,digit);
    const Wide r=scout_wide_math::subtract(x,scout_wide_math::product(hostQuotient(a,coeff,digit),low,high));
    assert(!r.w);return{r.x,r.y,r.z};
#endif
}
F80_HD Value seed_shift(Value a, uint32_t bits, uint32_t n0, uint32_t n1, uint32_t mu, uint32_t high) {
#if defined(__CUDA_ARCH__)
    Value d;
    asm volatile("{\n\t"
                 ".reg .u32 t0,t1,t2,t3,q,p0,p1,p2,r0,r1,r2;\n\t .reg .u64 column;\n\t"
                 "shl.b32 t0,%3,%6;\n\t shf.l.wrap.b32 t1,%3,%4,%6;\n\t"
                 "shf.l.wrap.b32 t2,%4,%5,%6;\n\t shf.l.wrap.b32 t3,%5,0,%6;\n\t"
                 "mul.hi.u32 p0,t2,%9;\n\t cvt.u64.u32 column,p0;\n\t"
                 "mad.wide.u32 column,t3,%9,column;\n\t"
                 "shr.u64 column,column,15;\n\t cvt.u32.u64 q,column;\n\t"
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
    const uint32_t q = uint32_t(column >> 15);
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

F80_HD Value make_seed(uint64_t low,uint32_t high,uint32_t mu) {
    const uint32_t n0=uint32_t(low),n1=uint32_t(low>>32),prefix=high>>8;
    Value a{0,0,1};
    a=seed_shift(a,30,n0,n1,mu,high);a=seed_shift(a,30,n0,n1,mu,high);
    a=seed_shift(a,30,n0,n1,mu,high);a=seed_shift(a,30,n0,n1,mu,high);
    if(prefix>=150)a=seed_shift(a,30,n0,n1,mu,high);
    if(prefix>=180)a=seed_shift(a,30,n0,n1,mu,high);
    if(prefix>=210)a=seed_shift(a,30,n0,n1,mu,high);
    if(prefix>=240)a=seed_shift(a,30,n0,n1,mu,high);
    a=seed_shift(a,prefix%30,n0,n1,mu,high);
    if(scout_wide_math::at_least(Wide{a.x,a.y,a.z,0},low,high)){
        const auto r=scout_wide_math::subtract(Wide{a.x,a.y,a.z,0},Wide{n0,n1,high,0});a={r.x,r.y,r.z};
    }
    return a;
}


constexpr uint32_t BoundedHighEnd=((uint32_t(1)<<16)*5/6)-1;
F80_HD Value reflect_near_capacity(Value a,uint64_t low,uint32_t high) {
#if defined(__CUDA_ARCH__)
    Value r;
    asm volatile("{\n\t.reg .u32 x,y,z; .reg .pred near_limit,negative;\n\t"
        "mov.u32 x,%3;\n\tmov.u32 y,%4;\n\tmov.u32 z,%5;\n\t"
        "setp.ge.u32 near_limit,z,65535;\n\t@!near_limit bra operand_ready;\n\t"
        "sub.cc.u32 x,x,%6;\n\tsubc.cc.u32 y,y,%7;\n\tsubc.u32 z,z,%8;\n\t"
        "setp.lt.s32 negative,z,0;\n\t@negative sub.cc.u32 x,0,x;\n\t"
        "@negative subc.cc.u32 y,0,y;\n\t@negative subc.u32 z,0,z;\n\t"
        "operand_ready: mov.u32 %0,x;\n\tmov.u32 %1,y;\n\tmov.u32 %2,z;\n\t}"
        :"=r"(r.x),"=r"(r.y),"=r"(r.z)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(uint32_t(low)),"r"(uint32_t(low>>32)),"r"(high));
    return r;
#else
    if(a.z>=65535){
        Wide r=scout_wide_math::subtract(Wide{a.x,a.y,a.z,0},Wide{uint32_t(low),uint32_t(low>>32),high,0});
        if(int32_t(r.z)<0)r=scout_wide_math::subtract(Wide{0,0,0,0},r);
        assert(!r.w);return{r.x,r.y,r.z};
    }
    return a;
#endif
}
F80_HD Value raw_bounded(Value a,uint64_t low,uint32_t high,uint32_t np) {
    const auto w=scout_wide_math::rawSquareWide(a,uint32_t(low),uint32_t(low>>32),np,high);
    assert(!w.w);return{w.x,w.y,w.z};
}
F80_HD Value first_bounded(Value a,uint64_t low,uint32_t np,uint32_t coeff,uint32_t high) {
#if defined(__CUDA_ARCH__)
    Value d;
    asm volatile("{\n.reg .f32 fz,fmu,fq;\n\t.reg .u32 l,m,h,q0,q1,z2,t0,t1,t2;\n\tadd.u32 z2,%5,%5;\n\tmul.lo.u32 l,%3,%3;\n\tmul.hi.u32 m,%3,%3;\n\tmul.lo.u32 q0,l,%8;\n\tmad.lo.cc.u32 l,q0,%6,l;\n\tmadc.hi.cc.u32 m,q0,%6,m;\n\taddc.u32 h,0,0;\n\tmov.u32 l,m;\n\tmov.u32 m,h;\n\tmad.lo.cc.u32 l,q0,%7,l;\n\tmadc.hi.cc.u32 m,q0,%7,m;\n\taddc.u32 h,0,0;\n\tmad.lo.cc.u32 l,%3,%4,l;\n\tmadc.hi.cc.u32 m,%3,%4,m;\n\taddc.u32 h,h,0;\n\tmad.lo.cc.u32 l,%3,%4,l;\n\tmadc.hi.cc.u32 m,%3,%4,m;\n\taddc.u32 h,h,0;\n\tmul.lo.u32 q1,l,%8;\n\tmad.lo.cc.u32 l,q1,%6,l;\n\tmadc.hi.cc.u32 m,q1,%6,m;\n\taddc.u32 h,h,0;\n\tmov.u32 l,m;\n\tmov.u32 m,h;\n\tmad.lo.cc.u32 l,q0,%10,l;\n\tmadc.hi.u32 m,q0,%10,m;\n\tmad.lo.cc.u32 l,%3,z2,l;\n\tmadc.hi.u32 m,%3,z2,m;\n\tmov.u32 h,0;\n\tmad.lo.cc.u32 l,q1,%7,l;\n\tmadc.hi.cc.u32 m,q1,%7,m;\n\taddc.u32 h,h,0;\n\tmad.lo.cc.u32 l,%4,%4,l;\n\tmadc.hi.cc.u32 m,%4,%4,m;\n\taddc.u32 h,h,0;\n\tmov.u32 t0,l;\n\tmov.u32 l,m;\n\tmov.u32 m,h;\n\tmad.lo.cc.u32 l,q1,%10,l;\n\tmadc.hi.u32 m,q1,%10,m;\n\tmad.lo.cc.u32 l,%4,z2,l;\n\tmadc.hi.u32 m,%4,z2,m;\n\tmov.u32 t1,l;\n\tmad.lo.u32 t2,%5,%5,m;\n\tcvt.rm.f32.u32 fz,t2;\nmov.b32 fmu,%9;\nmul.rm.f32 fq,fz,fmu;\ncvt.rmi.u32.f32 q0,fq;\nmul.lo.u32 l,q0,%6;\nmul.hi.u32 m,q0,%6;\nmad.lo.cc.u32 m,q0,%7,m;\nmadc.hi.u32 h,q0,%7,0;\nmad.lo.u32 h,q0,%10,h;\nsub.cc.u32 %0,t0,l;\nsubc.cc.u32 %1,t1,m;\nsubc.u32 %2,t2,h;\n}"
        :"=r"(d.x),"=r"(d.y),"=r"(d.z)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(uint32_t(low)),"r"(uint32_t(low>>32)),"r"(np),"r"(coeff),"r"(high));
    return d;
#else
    const auto a2=raw_bounded(a,low,high,np);const Wide x{a2.x,a2.y,a2.z,0};
    const auto r=scout_wide_math::subtract(x,scout_wide_math::product(hostQuotient(x,coeff,0),low,high));
    assert(!r.w);return{r.x,r.y,r.z};
#endif
}
F80_HD Value last_bounded(Value a,unsigned digit,uint64_t low,uint32_t coeff,uint32_t high) {
#if defined(__CUDA_ARCH__)
    Value r;
    asm volatile("{\n.reg .f32 fz,fmu,fq;\n.reg .u32 cb,q,t0,t1,t2,p0,p1,p2;\nshl.b32 cb,%6,23;\nadd.u32 cb,cb,%9;\nmov.b32 fmu,cb;\ncvt.rm.f32.u32 fz,%5;\nmul.rm.f32 fq,fz,fmu;\ncvt.rmi.u32.f32 q,fq;\nshl.b32 t0,%3,%6;\nshf.l.wrap.b32 t1,%3,%4,%6;\nshf.l.wrap.b32 t2,%4,%5,%6;\nmul.lo.u32 p0,q,%7;\nmul.hi.u32 p1,q,%7;\nmad.lo.cc.u32 p1,q,%8,p1;\nmadc.hi.u32 p2,q,%8,0;\nmad.lo.u32 p2,q,%10,p2;\nsub.cc.u32 %0,t0,p0;\nsubc.cc.u32 %1,t1,p1;\nsubc.u32 %2,t2,p2;\n}"
        :"=r"(r.x),"=r"(r.y),"=r"(r.z)
        :"r"(a.x),"r"(a.y),"r"(a.z),"r"(digit),"r"(uint32_t(low)),"r"(uint32_t(low>>32)),"r"(coeff),"r"(high));
    return r;
#else
    const auto x=scout_wide_math::shifted(a,digit);
    const auto r=scout_wide_math::subtract(x,scout_wide_math::product(hostQuotient(Wide{a.x,a.y,a.z,0},coeff,digit),low,high));
    assert(!r.w);return{r.x,r.y,r.z};
#endif
}
template<bool Reflect>
F80_HD bool test_bounded(uint64_t low,uint32_t high,uint32_t mu,uint32_t coeff) {
    if(!(low&1))return false;
    const auto np=scout_wide_math::negative_inverse(uint32_t(low));Value a=make_seed(low,high,mu);
    F80_UNROLL for(int shift=70;shift>=2;shift-=2){
        if constexpr(Reflect)a=reflect_near_capacity(a,low,high);
        a=first_bounded(a,low,np,coeff,high);
        if constexpr(Reflect)a=reflect_near_capacity(a,low,high);
        a=last_bounded(raw_bounded(a,low,high,np),scout_wide_math::exponent_digit<80>(low,high,shift,3),low,coeff,high);
    }
    if constexpr(Reflect)a=reflect_near_capacity(a,low,high);
    const auto raw=raw_bounded(a,low,high,np);
    return scout_wide_math::terminal_equation(Wide{raw.x,raw.y,raw.z,0},low,high,np,unsigned(low>>1)&1,(((low>>1)^(low>>2))&1)!=0);
}
F80_HD bool test_prepared(uint64_t low,uint32_t high,uint32_t mu,uint32_t coeff) {
    assert(high>=32768&&high<65536);
    if(high<BoundedHighEnd)return test_bounded<false>(low,high,mu,coeff);
    return test_bounded<true>(low,high,mu,coeff);
}
struct PreparedState {uint32_t muLower=0,coefficient=0;};


}
#undef F80_UNROLL
#undef F80_HD

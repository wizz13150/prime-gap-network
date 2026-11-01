#pragma once
#include "montgomery_columns.cuh"
namespace retained_columns {
using Value=optimized_narrow_euler::Value;using Wide=montgomery_columns::Wide;
// Preconditions: a.z<2^31 and high+2*a.z+8<2^32.
// The first cancelled column obeys floor((a0^2+q0*n0)/2^32)<a0+q0;
// adding q0*n1 therefore cannot carry beyond64 bits. Mixed high columns
// are bounded by high+2*a.z+8, so their omitted carry outputs are zero.
#if defined(__CUDA_ARCH__)
__device__ __forceinline__ Wide square_raw(Value a, uint32_t n0, uint32_t n1, uint32_t np32,uint32_t high) {
    Wide t;

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
                 "madc.hi.u32 m,q0,%8,m;\n\t"
                 "mov.u32 h,0;\n\t"
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
                 "madc.hi.u32 m,%4,z2,m;\n\t"
                 "mov.u32 h,0;\n\t"
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
                 "madc.hi.u32 m,%5,z2,m;\n\t"
                 "mov.u32 t1,l;\n\t"
                 "mad.lo.cc.u32 t2,%6,%6,m;\n\t"
                 "madc.hi.u32 t3,%6,%6,0;\n\t"
                 "mov.u32 %0,t0;\n\t"
                 "mov.u32 %1,t1;\n\t"
                 "mov.u32 %2,t2;\n\t"
                 "mov.u32 %3,t3;\n\t"
                 "}\n"
                 : "=r"(t.x), "=r"(t.y), "=r"(t.z), "=r"(t.w)
                 : "r"(a.x), "r"(a.y), "r"(a.z), "r"(n0), "r"(n1), "r"(np32),"r"(high));

    return t;
}
__device__ __forceinline__ Value square_float(Value a, uint32_t n0, uint32_t n1,
                                                           uint32_t np32, uint32_t muL,uint32_t high) {
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
                 "mad.lo.cc.u32 l,q0,%10,l;\n\t"
                 "madc.hi.u32 m,q0,%10,m;\n\t"
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
                 "mad.lo.cc.u32 l,q1,%10,l;\n\t"
                 "madc.hi.u32 m,q1,%10,m;\n\t"
                 "mad.lo.cc.u32 l,%4,z2,l;\n\t"
                 "madc.hi.u32 m,%4,z2,m;\n\t"
                 "mov.u32 t1,l;\n\t"
                 "mad.lo.cc.u32 t2,%5,%5,m;\n\t"
                 "madc.hi.u32 t3,%5,%5,0;\n\t"
                 "mov.b64 column,{t2,t3};\n\t"
                 "cvt.rm.f32.u64 fz,column;\n\t mov.b32 fc,%9;\n\t"
                 "mul.rm.f32 fq,fz,fc;\n\t cvt.rmi.u32.f32 q,fq;\n\t"
                 "mul.lo.u32 p0,q,%6;\n\t"
                 "mul.hi.u32 p1,q,%6;\n\t"
                 "mad.lo.cc.u32 p1,q,%7,p1;\n\t"
                 "madc.hi.u32 p2,q,%7,0;\n\t"
                 "mad.lo.u32 p2,q,%10,p2;\n\t"
                 "sub.cc.u32 %0,t0,p0;\n\t"
                 "subc.cc.u32 %1,t1,p1;\n\t"
                 "subc.u32 %2,t2,p2;\n\t"
                 "}\n"
                 : "=r"(d.x), "=r"(d.y), "=r"(d.z)
                 : "r"(a.x), "r"(a.y), "r"(a.z), "r"(n0), "r"(n1), "r"(np32), "r"(muL),"r"(high));
    return d;
}
template<unsigned Cache>
__device__ __forceinline__ Value square_integer32(Value a, uint32_t n0, uint32_t n1,
                                                             uint32_t np32, uint32_t muL,uint32_t high) {
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
                 "mad.lo.cc.u32 l,q0,%10,l;\n\t"
                 "madc.hi.u32 m,q0,%10,m;\n\t"
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
                 "mad.lo.cc.u32 l,q1,%10,l;\n\t"
                 "madc.hi.u32 m,q1,%10,m;\n\t"
                 "mad.lo.cc.u32 l,%4,z2,l;\n\t"
                 "madc.hi.u32 m,%4,z2,m;\n\t"
                 "mov.u32 t1,l;\n\t"
                 "mad.lo.cc.u32 t2,%5,%5,m;\n\t"
                 "madc.hi.u32 t3,%5,%5,0;\n\t"
                 "mul.hi.u32 q,t2,%9;\n\t"
                 "mad.lo.cc.u32 q,t3,%9,q;\n\t"
                 "madc.hi.u32 p0,t3,%9,0;\n\t"
                 "shf.r.wrap.b32 q,q,p0,%11;\n\t"
                 "mul.lo.u32 p0,q,%6;\n\t"
                 "mul.hi.u32 p1,q,%6;\n\t"
                 "mad.lo.cc.u32 p1,q,%7,p1;\n\t"
                 "madc.hi.u32 p2,q,%7,0;\n\t"
                 "mad.lo.u32 p2,q,%10,p2;\n\t"
                 "sub.cc.u32 %0,t0,p0;\n\t"
                 "subc.cc.u32 %1,t1,p1;\n\t"
                 "subc.u32 %2,t2,p2;\n\t"
                 "}\n"
                 : "=r"(d.x), "=r"(d.y), "=r"(d.z)
                 : "r"(a.x), "r"(a.y), "r"(a.z), "r"(n0), "r"(n1), "r"(np32), "r"(muL),"r"(high),"n"(Cache-96));
    return d;
}
template<unsigned Cache>
__device__ __forceinline__ Value last_integer32(Wide a, unsigned digit, uint64_t low, uint32_t mu,uint32_t high) {

    Value r;
    asm volatile("{\n\t .reg .u32 t0,t1,t2,t3,q,p0,p1,p2;\n\t"
                 "shl.b32 t0,%3,%7;\n\t shf.l.wrap.b32 t1,%3,%4,%7;\n\t shf.l.wrap.b32 "
                 "t2,%4,%5,%7;\n\t shf.l.wrap.b32 t3,%5,%6,%7;\n\t"
                 "mul.hi.u32 q,t2,%10;\n\t mad.lo.cc.u32 q,t3,%10,q;\n\t madc.hi.u32 "
                 "p0,t3,%10,0;\n\t shf.r.wrap.b32 q,q,p0,%12;\n\t"
                 "mul.lo.u32 p0,q,%8;\n\t mul.hi.u32 p1,q,%8;\n\t mad.lo.cc.u32 p1,q,%9,p1;\n\t "
                 "madc.hi.u32 p2,q,%9,0;\n\t mad.lo.u32 p2,q,%11,p2;\n\t"
                 "sub.cc.u32 %0,t0,p0;\n\t subc.cc.u32 %1,t1,p1;\n\t subc.u32 %2,t2,p2;\n\t}"
                 : "=r"(r.x), "=r"(r.y), "=r"(r.z)
                 : "r"(a.x), "r"(a.y), "r"(a.z), "r"(a.w), "r"(digit), "r"(uint32_t(low)),
                   "r"(uint32_t(low >> 32)), "r"(mu),"r"(high),"n"(Cache-96));
    return r;

}
__device__ __forceinline__ Value square_integer33(Value a, uint32_t n0, uint32_t n1, uint32_t np32,
                                                 uint32_t muL,uint32_t high) {
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
                 "mad.lo.cc.u32 l,q0,%10,l;\n\t"
                 "madc.hi.u32 m,q0,%10,m;\n\t"
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
                 "mad.lo.cc.u32 l,q1,%10,l;\n\t"
                 "madc.hi.u32 m,q1,%10,m;\n\t"
                 "mad.lo.cc.u32 l,%4,z2,l;\n\t"
                 "madc.hi.u32 m,%4,z2,m;\n\t"
                 "mov.u32 t1,l;\n\t"
                 "mad.lo.cc.u32 t2,%5,%5,m;\n\t"
                 "madc.hi.u32 t3,%5,%5,0;\n\t"
                 "mul.hi.u32 p0,t2,%9;\n\t"
                 "mad.lo.cc.u32 p0,t3,%9,p0;\n\t"
                 "madc.hi.u32 p1,t3,%9,0;\n\t"
                 "add.cc.u32 p0,p0,t2;\n\t"
                 "addc.u32 p1,p1,t3;\n\t"
                 "shf.r.wrap.b32 q,p0,p1,30;\n\t"
                 "mul.lo.u32 p0,q,%6;\n\t"
                 "mul.hi.u32 p1,q,%6;\n\t"
                 "mad.lo.cc.u32 p1,q,%7,p1;\n\t"
                 "madc.hi.u32 p2,q,%7,0;\n\t"
                 "mad.lo.u32 p2,q,%10,p2;\n\t"
                 "sub.cc.u32 %0,t0,p0;\n\t"
                 "subc.cc.u32 %1,t1,p1;\n\t"
                 "subc.u32 %2,t2,p2;\n\t"
                 "}\n"
                 : "=r"(d.x), "=r"(d.y), "=r"(d.z)
                 : "r"(a.x), "r"(a.y), "r"(a.z), "r"(n0), "r"(n1), "r"(np32), "r"(muL),"r"(high));
    return d;
}
__device__ __forceinline__ Value last_integer33(Wide a, unsigned digit, uint64_t low, uint32_t mu,uint32_t high) {

    Value r;
    asm volatile("{\n\t .reg .u32 t0,t1,t2,t3,q,qh,p0,p1,p2;\n\t"
                 "shl.b32 t0,%3,%7;\n\t shf.l.wrap.b32 t1,%3,%4,%7;\n\t shf.l.wrap.b32 "
                 "t2,%4,%5,%7;\n\t shf.l.wrap.b32 t3,%5,%6,%7;\n\t"
                 "mul.hi.u32 p0,t2,%10;\n\t"
                 "mad.lo.cc.u32 p0,t3,%10,p0;\n\t"
                 "madc.hi.u32 p1,t3,%10,0;\n\t"
                 "add.cc.u32 p0,p0,t2;\n\t"
                 "addc.cc.u32 p1,p1,t3;\n\t"
                 "addc.u32 p2,0,0;\n\t"
                 "shf.r.wrap.b32 q,p0,p1,30;\n\t"
                 "shf.r.wrap.b32 qh,p1,p2,30;\n\t"
                 "mul.lo.u32 p0,q,%8;\n\t mul.hi.u32 p1,q,%8;\n\t mad.lo.cc.u32 p1,q,%9,p1;\n\t "
                 "madc.hi.u32 p2,q,%9,0;\n\t mad.lo.u32 p2,q,%11,p2;\n\t"
                 ".reg .pred hasHigh;\n\t setp.ne.u32 hasHigh,qh,0;\n\t @hasHigh add.cc.u32 "
                 "p1,p1,%8;\n\t @hasHigh addc.u32 p2,p2,%9;\n\t"
                 "sub.cc.u32 %0,t0,p0;\n\t subc.cc.u32 %1,t1,p1;\n\t subc.u32 %2,t2,p2;\n\t}"
                 : "=r"(r.x), "=r"(r.y), "=r"(r.z)
                 : "r"(a.x), "r"(a.y), "r"(a.z), "r"(a.w), "r"(digit), "r"(uint32_t(low)),
                   "r"(uint32_t(low >> 32)), "r"(mu),"r"(high));
    return r;

}
#endif
}

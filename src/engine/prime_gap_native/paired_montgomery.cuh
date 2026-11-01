#pragma once
#include <stdint.h>
#include "R64lazy32_prp68.cuh"

// Specialized modular arithmetic; admission and fallback are defined below.
// Fallbacks preserve the same exact modular representation.
// Numerical domain/bucket/inverse/fallback are EXACTLY the existing cached primality ones.
namespace paired_montgomery {
using Value = myr_r64lazy32::Value;
using JobMu = myr_r64lazy32_prp::JobMu;
struct Folded {
    Value v;
}; // construction invariant0<=v<2N
struct Wide {
    Value v;
}; // construction invariant0<=v<256N
static_assert(sizeof(Folded) == 12 && sizeof(Wide) == 12, "No payload expansion");
#if defined(__CUDACC__)
#define C25_HD __host__ __device__ __forceinline__
#define C25_UNROLL _Pragma("unroll")
#else
#define C25_HD inline
#define C25_UNROLL
#endif

// ONLY a Folded input is admitted. Raw output<45N, included in Wide.
// This is the SAME bounded raw square column arithmetic through the exact raw t words,
// with NO final quotient/p*N/subtract. All third-word carries are retained.
C25_HD Wide rawSquare(Folded input, uint32_t n0, uint32_t n1, uint32_t np32) {
    const Value a = input.v;
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
                 "mad.lo.cc.u32 l,q0,10,l;\n\t"
                 "madc.hi.u32 m,q0,10,m;\n\t"
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
                 "mad.lo.cc.u32 l,q1,10,l;\n\t"
                 "madc.hi.u32 m,q1,10,m;\n\t"
                 "mad.lo.cc.u32 l,%4,z2,l;\n\t"
                 "madc.hi.u32 m,%4,z2,m;\n\t"
                 "mov.u32 t1,l;\n\t"
                 "mad.lo.u32 t2,%5,%5,m;\n\t"
                 "mov.u32 %0,t0;\n\t"
                 "mov.u32 %1,t1;\n\t"
                 "mov.u32 %2,t2;\n\t"
                 "}\n"
                 : "=r"(t.x), "=r"(t.y), "=r"(t.z)
                 : "r"(a.x), "r"(a.y), "r"(a.z), "r"(n0), "r"(n1), "r"(np32));
#else
    // Portable host arithmetic counterpart, NOT separately compiled here.
    using myr_r64lazy32_prp::hostAddProduct;
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
    hostAddProduct(l, m, h, q0, 10);
    hostAddProduct(l, m, h, a.x, 2 * a.z);
    hostAddProduct(l, m, h, q1, n1);
    hostAddProduct(l, m, h, a.y, a.y);
    t.x = l;
    l = m;
    m = h;
    h = 0;
    hostAddProduct(l, m, h, q1, 10);
    hostAddProduct(l, m, h, a.y, 2 * a.z);
    t.y = l;
    t.z = a.z * a.z + m;
#endif
    return {t};
}

// Full square of ANY constructed Wide value: sealed bounded raw square/cached primality implementation.
C25_HD Folded squareAndFold(Wide a, uint32_t n0, uint32_t n1, uint32_t np, uint32_t mu) {
    return {myr_r64lazy32_prp::squareAfterW3(a.v, n0, n1, np, mu)};
}

// Caller must use the SAME cached primality cold JobMu builder/admission. No new metadata.
// This changes temporary residue representatives, NOT the final Euler oracle.
C25_HD bool testAdmitted(uint64_t low, const JobMu &job) {
    if (!(low & 1))
        return false;
    const uint32_t n0 = uint32_t(low), n1 = uint32_t(low >> 32), mu = job.muLower;
    const uint32_t np = myr_r64lazy32_prp::negativeInverse32(n0);
    Value seed{0, 0, 1};
    C25_UNROLL for (int j = 0; j < 6; ++j) seed =
        myr_r64lazy32_prp::shiftReduce(seed, 26, n0, n1, mu);
    seed = myr_r64lazy32_prp::shiftReduce(seed, 4u + uint32_t(low >> 60), n0, n1, mu);
    const uint64_t e = (low - 1) >> 1;
    Wide a = rawSquare(Folded{seed}, n0, n1, np);                     //raw1, input<2N, output<45N
    a.v = myr_r64lazy32_prp::shiftOnly(a.v, uint32_t((e >> 58) & 1)); //now<90N
    for (int bit = 57; bit >= 3; bit -= 3) {
        Folded b = squareAndFold(a, n0, n1, np, mu); //input<90N or256N; output<2N
        Wide c = rawSquare(b, n0, n1, np);           //raw19, input<2N; output<45N
        b = squareAndFold(c, n0, n1, np, mu);        //input<45N; output<2N
        a.v = myr_r64lazy32_prp::shiftOnly(b.v, uint32_t((e >> (bit - 2)) & 7)); //now<256N
    }
    Folded b = squareAndFold(a, n0, n1, np, mu); //folded39, same final square
    const Value last = myr_r64lazy32_prp::shiftReduce(b.v, uint32_t(e & 1), n0, n1, mu);
    if (((low >> 1) ^ (low >> 2)) & 1)
        return myr_r64lazy32_prp::equals(last, Value{n0, n1, 9}) ||
               myr_r64lazy32_prp::equals(
                   last, Value{n0 << 1, uint32_t((low << 1) >> 32), 19u + uint32_t(low >> 63)});
    return myr_r64lazy32_prp::equals(last, Value{0, 0, 1}) ||
           myr_r64lazy32_prp::equals(last, Value{n0, n1, 11});
}

C25_HD bool testWithFallback(uint64_t low, const JobMu &job, bool *fellBack = nullptr) {
    if (!myr_r64lazy32_prp::admits(job, low)) {
        if (fellBack)
            *fellBack = true;
        return pgn_barrett70::test(low);
    }
    if (fellBack)
        *fellBack = false;
    return paired_montgomery::testAdmitted(low, job);
}
#undef C25_HD
#undef C25_UNROLL
} // namespace paired_montgomery

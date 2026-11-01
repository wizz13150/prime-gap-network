#pragma once
// Specialized modular arithmetic; admission and fallback are defined below.
// Real high64 word10 ONLY, exact old JobMu and a narrower <=2^36 job span.
// Fallbacks preserve the same exact modular representation.
#include "paired_montgomery_stream.cuh"
#if defined(__CUDACC__)
#define M600_HD __host__ __device__ __forceinline__
#define M600_SEED_UNROLL _Pragma("unroll")
#else
#define M600_HD inline
#define M600_SEED_UNROLL
#endif
namespace radix16_euler68 {
using Value = myr_r64lazy32::Value;
using JobMu = myr_r64lazy32_prp::JobMu;
// Internal contract: 0<=a<45N, N=10*2^64+low. Exact same raw columns as paired square.
// No call is made through paired square's narrower Folded<2N interface.
M600_HD Value rawSquare45(Value a, uint32_t n0, uint32_t n1, uint32_t np32) {
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
    return t;
}

// Generic true: a<22276N,bits<=15. False: a<45N,bits<=7 (tail), OR
// the proved reached hot a<(10036371/4096)N,bits<=15,delta<29/25.
// The latter always has X<2^94; generic large inputs STILL need true.
// Digit0 MUST still execute the linear fold; never early-return a here.
// The high part after shifting may need33bits. Its extra bit participates in
// q BEFORE the low96 subtraction. The actual residue is proved nonnegative
// and<4N (or<2N in the final small case), so modulo2^96 subtraction is exact.
template <bool Carry97>
M600_HD Value shiftFold(Value a, uint32_t bits, uint32_t n0, uint32_t n1, uint32_t mu) {
    Value t;
    uint32_t carry = 0;
#if defined(__CUDA_ARCH__)
    // PTX SHF has defined zero-distance semantics. No bits==0 branch is
    // required: the concatenation [0,a.z]<<0 has a zero high word.
    if constexpr (Carry97) {
        asm("{\n\t.reg .u32 x,y,z,h;\n\t"
            "shl.b32 x,%4,%7;\n\tshf.l.wrap.b32 y,%4,%5,%7;\n\t"
            "shf.l.wrap.b32 z,%5,%6,%7;\n\tshf.l.wrap.b32 h,%6,0,%7;\n\t"
            "mov.u32 %0,x;\n\tmov.u32 %1,y;\n\tmov.u32 %2,z;\n\tmov.u32 %3,h;\n\t}"
            : "=r"(t.x), "=r"(t.y), "=r"(t.z), "=r"(carry)
            : "r"(a.x), "r"(a.y), "r"(a.z), "r"(bits));
    } else {
        asm("{\n\t.reg .u32 x,y,z;\n\t"
            "shl.b32 x,%3,%6;\n\tshf.l.wrap.b32 y,%3,%4,%6;\n\t"
            "shf.l.wrap.b32 z,%4,%5,%6;\n\t"
            "mov.u32 %0,x;\n\tmov.u32 %1,y;\n\tmov.u32 %2,z;\n\t}"
            : "=r"(t.x), "=r"(t.y), "=r"(t.z)
            : "r"(a.x), "r"(a.y), "r"(a.z), "r"(bits));
    }
    Value d;
    asm("{\n\t.reg .u32 q,p0,p1,p2,r0,r1,r2;\n\t"
        "mul.hi.u32 q,%5,%8;\n\t"
        "mad.lo.u32 q,%9,%8,q;\n\t"
        "mul.lo.u32 p0,q,%6;\n\tmul.hi.u32 p1,q,%6;\n\t"
        "mad.lo.cc.u32 p1,q,%7,p1;\n\tmadc.hi.u32 p2,q,%7,0;\n\t"
        "mad.lo.u32 p2,q,10,p2;\n\t"
        "sub.cc.u32 r0,%3,p0;\n\tsubc.cc.u32 r1,%4,p1;\n\tsubc.u32 r2,%5,p2;\n\t"
        "mov.u32 %0,r0;\n\tmov.u32 %1,r1;\n\tmov.u32 %2,r2;\n\t}"
        : "=r"(d.x), "=r"(d.y), "=r"(d.z)
        : "r"(t.x), "r"(t.y), "r"(t.z), "r"(n0), "r"(n1), "r"(mu), "r"(carry));
    return d;
#else
    // The portable words use64-bit temporaries, so bits==0 never causes a
    // C++ shift by32; the high temporary carries the extra97th bit exactly.
    const uint64_t x = uint64_t(a.x) << bits;
    const uint64_t y = (uint64_t(a.y) << bits) | (x >> 32);
    const uint64_t highShift = (uint64_t(a.z) << bits) | (y >> 32);
    t = {uint32_t(x), uint32_t(y), uint32_t(highShift)};
    if constexpr (Carry97)
        carry = uint32_t(highShift >> 32);
    const uint32_t q = uint32_t((uint64_t(t.z) * mu) >> 32) + carry * mu;
    const uint64_t p0 = uint64_t(q) * n0;
    const uint64_t p1 = uint64_t(q) * n1 + (p0 >> 32);
    const uint32_t p2 = uint32_t(uint64_t(q) * 10 + (p1 >> 32));
    uint64_t z = uint64_t(t.x) - uint32_t(p0);
    Value d{uint32_t(z), 0, 0};
    z = uint64_t(t.y) - uint32_t(p1) - uint32_t(z >> 63);
    d.y = uint32_t(z);
    d.z = t.z - p2 - uint32_t(z >> 63);
    return d;
#endif
}

M600_HD bool admits(const JobMu &job, uint64_t low) {
    return myr_r64lazy32_prp::admits(job, low) && job.maxLow >= job.minLow &&
           job.maxLow - job.minLow <= (uint64_t(1) << 36);
}

// PRECONDITION: same exact cached primality ready JobMu, actual H10, plus the narrow span.
// This is not an unchecked arbitrary-high or arbitrary-mu entry point.
M600_HD bool testAdmitted(uint64_t low, const JobMu &job) {
    if (!(low & 1))
        return false;
    const uint32_t n0 = uint32_t(low), n1 = uint32_t(low >> 32), mu = job.muLower;
    const uint32_t np = myr_r64lazy32_prp::negativeInverse32(n0);
    Value a{0, 0, 1};
    M600_SEED_UNROLL for (int j = 0; j < 6; ++j) a =
        myr_r64lazy32_prp::shiftReduce(a, 26, n0, n1, mu);
    a = myr_r64lazy32_prp::shiftReduce(a, 4u + uint32_t(low >> 60), n0, n1, mu);

    // Same fourteen digits as the proved sentinel loop. The marker remains
    // below every consumed digit. A constant trip count permits full unroll;
    // no arithmetic, input domain, digit or tail operation changes.
    uint64_t stream = ((low & ~uint64_t(15)) << 4) | uint64_t(128);
#if defined(__CUDA_ARCH__)
#pragma unroll
#endif
    for (unsigned group_index = 0; group_index < 14u; ++group_index) {
        a = rawSquare45(a, n0, n1, np);                              // input<4N, output<177N<256N
        a = myr_r64lazy32_prp::squareAfterW3(a, n0, n1, np, mu);     // <2N
        a = rawSquare45(a, n0, n1, np);                              // <45N
        a = rawSquare45(a, n0, n1, np);                              // <22276N
        a = shiftFold<false>(a, uint32_t(stream >> 60), n0, n1, mu); // <11N/8; reached carry97==0
        stream <<= 4;
    }

    // Last three exponent bits are low3..1. Their digit<=7 permits a plain
    // 96-bit shift/fold and restores<2N for the UNCHANGED Euler oracle.
    a = rawSquare45(a, n0, n1, np);                                 // <177N
    a = myr_r64lazy32_prp::squareAfterW3(a, n0, n1, np, mu);        // <2N
    a = rawSquare45(a, n0, n1, np);                                 // <45N
    a = shiftFold<false>(a, uint32_t((low >> 1) & 7u), n0, n1, mu); // <2N
    if (((low >> 1) ^ (low >> 2)) & 1u)
        return myr_r64lazy32_prp::equals(a, Value{n0, n1, 9}) ||
               myr_r64lazy32_prp::equals(
                   a, Value{n0 << 1, uint32_t((low << 1) >> 32), 19u + uint32_t(low >> 63)});
    return myr_r64lazy32_prp::equals(a, Value{0, 0, 1}) ||
           myr_r64lazy32_prp::equals(a, Value{n0, n1, 11});
}

// A wide/unready envelope uses the retained streamed pair fold path, not a truncated span.
// fellBack means this private candidate was not used; streamed pair fold keeps its own guard.
M600_HD bool testWithFallback(uint64_t low, const JobMu &job, bool *fellBack = nullptr) {
    if (!radix16_euler68::admits(job, low)) {
        if (fellBack)
            *fellBack = true;
        return paired_montgomery_stream::testWithFallback(low, job);
    }
    if (fellBack)
        *fellBack = false;
    return radix16_euler68::testAdmitted(low, job);
}
} // namespace radix16_euler68
#undef M600_HD
#undef M600_SEED_UNROLL

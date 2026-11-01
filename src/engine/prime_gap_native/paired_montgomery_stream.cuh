#pragma once
// Independent SOURCE control candidate after exact Native AOT assembly verification.
// No arithmetic primitive, paired square consumer, small-number gate tiny gate or Native kit changed.
#include "paired_montgomery.cuh"

#if defined(__CUDACC__)
#define C29_HD __host__ __device__ __forceinline__
#define C29_SEED_UNROLL _Pragma("unroll")
#else
#define C29_HD inline
#define C29_SEED_UNROLL
#endif

namespace paired_montgomery_stream {
using Value = paired_montgomery::Value;
using JobMu = paired_montgomery::JobMu;
using Folded = paired_montgomery::Folded;
using Wide = paired_montgomery::Wide;

// SAME admission as paired square: odd low, real H10 JobMu, exact prepaid muLower.
// The arithmetic producer/RAW/FOLD bodies are the sealed paired square/cached primality ones.
C29_HD bool testAdmitted(uint64_t low, const JobMu &job) {
    if (!(low & 1))
        return false;
    const uint32_t n0 = uint32_t(low), n1 = uint32_t(low >> 32), mu = job.muLower;
    const uint32_t np = myr_r64lazy32_prp::negativeInverse32(n0);
    Value seed{0, 0, 1};
    C29_SEED_UNROLL for (int j = 0; j < 6; ++j) seed =
        myr_r64lazy32_prp::shiftReduce(seed, 26, n0, n1, mu);
    seed = myr_r64lazy32_prp::shiftReduce(seed, 4u + uint32_t(low >> 60), n0, n1, mu);

    Wide a = paired_montgomery::rawSquare(Folded{seed}, n0, n1, np);
    a.v = myr_r64lazy32_prp::shiftOnly(a.v, uint32_t((low >> 59) & 1u));

    // D=(low>>2)&(2^57-1). The marker is one bit BELOW the surviving D bits.
    // After exactly 19 shifts by3, D is gone and the marker alone reaches bit63.
    // No shift by64, signed shift, variable trip quota or early-oracle decision.
    uint64_t windows = ((low & ~uint64_t(3)) << 5) | uint64_t(64);
#if defined(__CUDA_ARCH__)
#pragma unroll 1
#endif
    do {
        Folded b = paired_montgomery::squareAndFold(a, n0, n1, np, mu);
        const Wide c = paired_montgomery::rawSquare(b, n0, n1, np);
        b = paired_montgomery::squareAndFold(c, n0, n1, np, mu);
        a.v = myr_r64lazy32_prp::shiftOnly(b.v, uint32_t(windows >> 61));
        windows <<= 3;
    } while (windows != (uint64_t(1) << 63));

    const Folded b = paired_montgomery::squareAndFold(a, n0, n1, np, mu);
    const Value last = myr_r64lazy32_prp::shiftReduce(b.v, uint32_t((low >> 1) & 1u), n0, n1, mu);
    if (((low >> 1) ^ (low >> 2)) & 1u)
        return myr_r64lazy32_prp::equals(last, Value{n0, n1, 9}) ||
               myr_r64lazy32_prp::equals(
                   last, Value{n0 << 1, uint32_t((low << 1) >> 32), 19u + uint32_t(low >> 63)});
    return myr_r64lazy32_prp::equals(last, Value{0, 0, 1}) ||
           myr_r64lazy32_prp::equals(last, Value{n0, n1, 11});
}

C29_HD bool testWithFallback(uint64_t low, const JobMu &job, bool *fellBack = nullptr) {
    if (!myr_r64lazy32_prp::admits(job, low)) {
        if (fellBack)
            *fellBack = true;
        return pgn_barrett70::test(low);
    }
    if (fellBack)
        *fellBack = false;
    return paired_montgomery_stream::testAdmitted(low, job);
}
} // namespace paired_montgomery_stream

#undef C29_HD
#undef C29_SEED_UNROLL

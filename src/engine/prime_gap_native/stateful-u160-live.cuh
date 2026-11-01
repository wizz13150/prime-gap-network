#pragma once
// Specialized modular arithmetic; admission and fallback are defined below.
// Same State8 and perpoint API as the retained cached Euler arithmetic/streamed pair fold wrapper.
#include "radix16_euler68.cuh"
#if defined(__CUDACC__)
#define M586_HD __host__ __device__ __forceinline__
#else
#define M586_HD inline
#endif
namespace cached_euler68 {
struct State {
    uint32_t bucketTag = 0, muLower = 0;
};
static_assert(sizeof(State) == 8, "Same two-word per-owner state");
constexpr uint64_t BucketMask = (uint64_t(1) << 36) - 1;
M586_HD bool test(uint64_t low, State &state) {
    if (!(low & 1))
        return false;
    const uint32_t tag = uint32_t(low >> 36);
    const uint64_t first = uint64_t(tag) << 36, last = first | BucketMask;
    if (!state.muLower || state.bucketTag != tag) {
        const auto built = myr_r64lazy32_prp::buildJob(10, first, 10, last);
        if (built.ready != myr_r64lazy32_prp::Ready)
            return pgn_barrett70::test(low);
        state.bucketTag = tag;
        state.muLower = built.muLower;
    }
    // The exact cached primality builder was run for THIS complete2^36 bucket. Reversed,
    // halo and repair queries are admitted by tag, never clipped/dropped.
    // Keep real bounds in JobMu to make narrow-domain cache's <=2^36 proof contract explicit.
    // testAdmitted reads only muLower; compiler/register consequences unpaid.
    const myr_r64lazy32_prp::JobMu hot{first, last, state.muLower, myr_r64lazy32_prp::Ready};
    return radix16_euler68::testAdmitted(low, hot);
}
} // namespace cached_euler68
#undef M586_HD

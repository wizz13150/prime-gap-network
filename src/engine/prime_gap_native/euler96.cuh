#ifndef PGN_EULER96_CUH
#define PGN_EULER96_CUH
// Experimental three-limb Euler/Jacobi representation, not a prime proof.
// CIOS carry structure follows the existing GPL-licensed mr68_kernel.cuh.
// Only the integer representation changes: 2^((n-1)/2) == (2|n) (mod n).
// Domain: odd 2^64 < n < 2^96. The production DLL remains untouched.
#include <stdint.h>
#include "barrett70-exact.cuh"
#include "stateful-u160-live.cuh"
#ifdef __CUDACC__
#define PGN96_HD __host__ __device__ __forceinline__
#define PGN96_UNROLL _Pragma("unroll")
#else
#define PGN96_HD inline
#define PGN96_UNROLL
#endif
namespace pgn_euler96 {
struct U96 {
    uint32_t w[3];
};
PGN96_HD bool equal(const U96 &a, const U96 &b) {
    return a.w[0] == b.w[0] && a.w[1] == b.w[1] && a.w[2] == b.w[2];
}
PGN96_HD bool greaterEqual(const U96 &a, const U96 &b) {
    for (int i = 2; i >= 0; --i)
        if (a.w[i] != b.w[i])
            return a.w[i] > b.w[i];
    return true;
}
PGN96_HD uint32_t subtract(U96 &a, const U96 &b) {
    uint32_t borrow = 0;
    PGN96_UNROLL
    for (int i = 0; i < 3; ++i) {
        const uint64_t d = uint64_t(a.w[i]) - b.w[i] - borrow;
        a.w[i] = uint32_t(d);
        borrow = uint32_t((d >> 32) & 1);
    }
    return borrow;
}
PGN96_HD uint32_t add(U96 &a, const U96 &b) {
    uint32_t carry = 0;
    PGN96_UNROLL
    for (int i = 0; i < 3; ++i) {
        const uint64_t s = uint64_t(a.w[i]) + b.w[i] + carry;
        a.w[i] = uint32_t(s);
        carry = uint32_t(s >> 32);
    }
    return carry;
}
PGN96_HD uint32_t inverse(uint32_t odd) {
    uint32_t x = odd;
    PGN96_UNROLL
    for (int i = 0; i < 5; ++i)
        x *= 2U - odd * x;
    return 0U - x;
}
PGN96_HD uint64_t quotientEstimate(const U96 &n) {
    const uint64_t low = uint64_t(n.w[0]) | (uint64_t(n.w[1]) << 32);
    const double denominator = double(n.w[2]) * 18446744073709551616.0 + double(low);
    return uint64_t(79228162514264337593543950336.0 / denominator);
}
PGN96_HD U96 montgomeryOne(const U96 &n) {
    // A double supplies only a quotient estimate. Exact multiply/carry and
    // signed correction below, never the estimate, decide the remainder.
    const uint64_t q = quotientEstimate(n);
    U96 product{};
    uint64_t carry = 0;
    PGN96_UNROLL
    for (int i = 0; i < 3; ++i) {
        const uint64_t p = uint64_t(n.w[i]) * q + carry;
        product.w[i] = uint32_t(p);
        carry = p >> 32;
    }
    U96 remainder{};
    int top = 1 - int(carry) - int(subtract(remainder, product));
    while (top < 0)
        top += int(add(remainder, n));
    while (top > 0 || greaterEqual(remainder, n))
        top -= int(subtract(remainder, n));
    return remainder;
}
#ifdef PGN_EULER96_BATCH_Q
// Only a guarded wheel batch may create a hint. Normalize its quotient once:
// q<=2^32 means a four-word product is sufficient. The
// remainder representation includes the signed high word; no lost carry.
PGN96_HD U96 correctHintedRemainder(const U96 &n, uint64_t q, uint64_t *exactQuotient = nullptr) {
    U96 product{};
    uint64_t carry = 0;
    PGN96_UNROLL
    for (int i = 0; i < 3; ++i) {
        const uint64_t p = uint64_t(n.w[i]) * q + carry;
        product.w[i] = uint32_t(p);
        carry = p >> 32;
    }
    U96 remainder{};
    int64_t top = 1 - int64_t(carry) - int64_t(subtract(remainder, product));
    while (top < 0) {
        top += int64_t(add(remainder, n));
        --q;
    }
    while (top > 0 || greaterEqual(remainder, n)) {
        top -= int64_t(subtract(remainder, n));
        ++q;
    }
    if (exactQuotient)
        *exactQuotient = q;
    return remainder;
}
PGN96_HD uint64_t batchQuotient(uint64_t low, uint64_t high, uint64_t nwords) {
    constexpr uint64_t MaxWords = 97ULL * (1ULL << 22);
    if (high == 0 || high > UINT32_MAX || nwords == 0 || nwords > MaxWords)
        return 0;
    // Slots=nwords*64, eight wheel classes per thirty numbers. The last
    // candidate is no larger than low + 30*nwords*8 - 1. Refuse high-word
    // crossing rather than silently extending this experiment's domain.
    const uint64_t span = 30ULL * nwords * 8ULL - 1;
    if (low > UINT64_MAX - span)
        return 0;
    const U96 n{{uint32_t(low), uint32_t(low >> 32), uint32_t(high)}};
    auto q = quotientEstimate(n);
    if (q == 0 || q > (1ULL << 32))
        return 0;
    correctHintedRemainder(n, q, &q);
    return q;
}
PGN96_HD U96 montgomeryOneHinted(const U96 &n, uint64_t q) {
    // batchQuotient supplies floor(R/base) exactly. For n>=base and span<2^37,
    // q<=2^32 and q*n<R+2^69<2R. Corrections decide Rmodn exactly; quotient
    // difference is <32+1 at the domain's worst n=2^64 boundary.
    return q ? correctHintedRemainder(n, q) : montgomeryOne(n);
}
#endif
struct Wide32 {
    uint32_t low, high;
};
PGN96_HD Wide32 multiplyAdd(uint32_t a, uint32_t b, uint32_t addend, uint32_t carry) {
    Wide32 r{};
#if defined(__CUDA_ARCH__) && defined(PGN_EULER96_WORD_OPS)
    // a*b+addend+carry <= (2^32-1)^2+2*(2^32-1) = 2^64-1.
    // Consume every input before writing the final outputs: explicit two-word
    // carry, no approximate multiplication or widened carry dependency.
    asm volatile("{ .reg .u32 l,h; mad.lo.cc.u32 l,%2,%3,%4; madc.hi.u32 h,%2,%3,0; add.cc.u32 "
                 "%0,l,%5; addc.u32 %1,h,0; }"
                 : "=r"(r.low), "=r"(r.high)
                 : "r"(a), "r"(b), "r"(addend), "r"(carry));
#else
    const uint64_t v = uint64_t(a) * b + addend + carry;
    r.low = uint32_t(v);
    r.high = uint32_t(v >> 32);
#endif
    return r;
}
PGN96_HD U96 multiply(const U96 &a, const U96 &b, const U96 &n, uint32_t n0inv) {
    uint32_t t[5] = {0, 0, 0, 0, 0};
    PGN96_UNROLL
    for (int i = 0; i < 3; ++i) {
#ifdef PGN_EULER96_WORD_OPS
        uint32_t c = 0;
        PGN96_UNROLL
        for (int j = 0; j < 3; ++j) {
            const auto p = multiplyAdd(a.w[j], b.w[i], t[j], c);
            t[j] = p.low;
            c = p.high;
        }
#else
        uint64_t c = 0;
        PGN96_UNROLL
        for (int j = 0; j < 3; ++j) {
            const uint64_t p = uint64_t(a.w[j]) * b.w[i] + t[j] + c;
            t[j] = uint32_t(p);
            c = p >> 32;
        }
#endif
        const uint64_t s0 = uint64_t(t[3]) + c;
        t[3] = uint32_t(s0);
        t[4] += uint32_t(s0 >> 32);
        const uint32_t m = t[0] * n0inv;
#ifdef PGN_EULER96_WORD_OPS
        // The low word cancels exactly because m = -t0/n0 mod 2^32.
        // Its only carry is (t0 != 0); the first reduction needs no low product.
#ifdef __CUDA_ARCH__
        c = __umulhi(m, n.w[0]) + uint32_t(t[0] != 0);
#else
        c = uint32_t((uint64_t(m) * n.w[0]) >> 32) + uint32_t(t[0] != 0);
#endif
        PGN96_UNROLL
        for (int j = 1; j < 3; ++j) {
            const auto p = multiplyAdd(m, n.w[j], t[j], c);
            t[j - 1] = p.low;
            c = p.high;
        }
#else
        c = (uint64_t(t[0]) + uint64_t(m) * n.w[0]) >> 32;
        PGN96_UNROLL
        for (int j = 1; j < 3; ++j) {
            const uint64_t p = uint64_t(m) * n.w[j] + t[j] + c;
            t[j - 1] = uint32_t(p);
            c = p >> 32;
        }
#endif
        const uint64_t s2 = uint64_t(t[3]) + c;
        t[2] = uint32_t(s2);
        t[3] = t[4] + uint32_t(s2 >> 32);
        t[4] = 0;
    }
    U96 result{{t[0], t[1], t[2]}}, reduced = result;
    const auto borrow = subtract(reduced, n);
    return t[3] != 0 || borrow == 0 ? reduced : result;
}
#ifdef PGN_EULER96_LAZY
PGN96_HD U96 multiplyLazy(const U96 &a, const U96 &b, const U96 &n, uint32_t n0inv) {
    uint32_t t[5] = {0, 0, 0, 0, 0};
    PGN96_UNROLL
    for (int i = 0; i < 3; ++i) {
#ifdef PGN_EULER96_WORD_OPS
        uint32_t c = 0;
        PGN96_UNROLL
        for (int j = 0; j < 3; ++j) {
            const auto p = multiplyAdd(a.w[j], b.w[i], t[j], c);
            t[j] = p.low;
            c = p.high;
        }
#else
        uint64_t c = 0;
        PGN96_UNROLL
        for (int j = 0; j < 3; ++j) {
            const uint64_t p = uint64_t(a.w[j]) * b.w[i] + t[j] + c;
            t[j] = uint32_t(p);
            c = p >> 32;
        }
#endif
        const uint64_t s0 = uint64_t(t[3]) + c;
        t[3] = uint32_t(s0);
        t[4] += uint32_t(s0 >> 32);
        const uint32_t m = t[0] * n0inv;
#ifdef PGN_EULER96_WORD_OPS
        // The low word cancels exactly because m = -t0/n0 mod 2^32.
        // Its only carry is (t0 != 0); the first reduction needs no low product.
#ifdef __CUDA_ARCH__
        c = __umulhi(m, n.w[0]) + uint32_t(t[0] != 0);
#else
        c = uint32_t((uint64_t(m) * n.w[0]) >> 32) + uint32_t(t[0] != 0);
#endif
        PGN96_UNROLL
        for (int j = 1; j < 3; ++j) {
            const auto p = multiplyAdd(m, n.w[j], t[j], c);
            t[j - 1] = p.low;
            c = p.high;
        }
#else
        c = (uint64_t(t[0]) + uint64_t(m) * n.w[0]) >> 32;
        PGN96_UNROLL
        for (int j = 1; j < 3; ++j) {
            const uint64_t p = uint64_t(m) * n.w[j] + t[j] + c;
            t[j - 1] = uint32_t(p);
            c = p >> 32;
        }
#endif
        const uint64_t s2 = uint64_t(t[3]) + c;
        t[2] = uint32_t(s2);
        t[3] = t[4] + uint32_t(s2 >> 32);
        t[4] = 0;
    }
    // For a,b<2n and n<R/4: REDC(ab)<n+4n*n/R<2n<R.
    // The three words retain the exact noncanonical residue; t[3] is zero.
    return U96{{t[0], t[1], t[2]}};
}
#endif
#ifdef PGN_EULER96_LAZY
// Exact fixed-high10 CIOS state. B=2^32, 10*2^64<=n<11*2^64,
// 0<=a,b<2n. After j reductions T_j=(a*b_prefix+n*m_prefix)/B^j
// is below a+n<3n<33*2^64. The persistent fourth/fifth words are
// therefore zero at every iteration, not merely at final acceptance.
PGN96_HD U96 multiplyLazyHigh10(const U96 &a, const U96 &b, const U96 &n, uint32_t inv) {
    uint32_t t0 = 0, t1 = 0, t2 = 0;
    PGN96_UNROLL
    for (int i = 0; i < 3; ++i) {
        const auto p0 = multiplyAdd(a.w[0], b.w[i], t0, 0);
        const auto p1 = multiplyAdd(a.w[1], b.w[i], t1, p0.high);
        // In the final column a2,b2<=21, t2<=32, p1.high<=21.
        // Its complete product+carry is below2^10: no high product exists.
        const auto p2 = i == 2 ? Wide32{a.w[2] * b.w[2] + t2 + p1.high, 0}
                               : multiplyAdd(a.w[2], b.w[i], t2, p1.high);
        const uint32_t m = p0.low * inv;
#ifdef __CUDA_ARCH__
        const uint32_t carry = __umulhi(m, n.w[0]) + uint32_t(p0.low != 0);
#else
        const uint32_t carry = uint32_t((uint64_t(m) * n.w[0]) >> 32) + uint32_t(p0.low != 0);
#endif
        const auto q1 = multiplyAdd(m, n.w[1], p1.low, carry);
        const auto q2 = multiplyAdd(m, 10u, p2.low, q1.high);
        t0 = q1.low;
        t1 = q2.low;
        t2 = p2.high + q2.high;
    }
    return U96{{t0, t1, t2}};
}
#endif
#ifdef PGN_EULER96_SQUARE
PGN96_HD U96 square(const U96 &a, const U96 &n, uint32_t n0inv) {
    // Six unique products instead of nine. Exact192-bit column sums retain
    // every cross-term twice; no float, truncated product or assumed high limb.
    const uint64_t p00 = uint64_t(a.w[0]) * a.w[0], p01 = uint64_t(a.w[0]) * a.w[1];
    const uint64_t p02 = uint64_t(a.w[0]) * a.w[2], p11 = uint64_t(a.w[1]) * a.w[1];
    const uint64_t p12 = uint64_t(a.w[1]) * a.w[2], p22 = uint64_t(a.w[2]) * a.w[2];
    uint32_t t[7] = {uint32_t(p00), 0, 0, 0, 0, 0, 0};
    uint64_t s = (p00 >> 32) + 2ULL * uint32_t(p01);
    t[1] = uint32_t(s);
    s = (s >> 32) + 2ULL * (p01 >> 32) + 2ULL * uint32_t(p02) + uint32_t(p11);
    t[2] = uint32_t(s);
    s = (s >> 32) + 2ULL * (p02 >> 32) + (p11 >> 32) + 2ULL * uint32_t(p12);
    t[3] = uint32_t(s);
    s = (s >> 32) + 2ULL * (p12 >> 32) + uint32_t(p22);
    t[4] = uint32_t(s);
    s = (s >> 32) + (p22 >> 32);
    t[5] = uint32_t(s);
    t[6] = uint32_t(s >> 32);
    // Exact radix2^32 REDC. A<n implies a^2<nR, so after division by R
    // the result is below2n and a single conditional subtraction suffices.
    PGN96_UNROLL
    for (int i = 0; i < 3; ++i) {
        const uint32_t m = t[i] * n0inv;
        uint64_t carry = (uint64_t(t[i]) + uint64_t(m) * n.w[0]) >> 32;
        PGN96_UNROLL
        for (int j = 1; j < 3; ++j) {
            const uint64_t p = uint64_t(m) * n.w[j] + t[i + j] + carry;
            t[i + j] = uint32_t(p);
            carry = p >> 32;
        }
        PGN96_UNROLL
        for (int k = i + 3; k < 7; ++k) {
            const uint64_t p = uint64_t(t[k]) + carry;
            t[k] = uint32_t(p);
            carry = p >> 32;
        }
    }
    const U96 result{{t[3], t[4], t[5]}};
    U96 reduced = result;
    const auto borrow = subtract(reduced, n);
    return t[6] != 0 || borrow == 0 ? reduced : result;
}
#endif
PGN96_HD U96 twice(const U96 &a, const U96 &n) {
    U96 result{};
    uint32_t carry = 0;
    PGN96_UNROLL
    for (int i = 0; i < 3; ++i) {
        const uint64_t s = uint64_t(a.w[i]) * 2 + carry;
        result.w[i] = uint32_t(s);
        carry = uint32_t(s >> 32);
    }
    U96 reduced = result;
    const auto borrow = subtract(reduced, n);
    return carry != 0 || borrow == 0 ? reduced : result;
}
PGN96_HD int highestBit(const U96 &e) {
    const int base = e.w[2] ? 64 : e.w[1] ? 32 : 0;
    uint32_t word = e.w[base / 32];
#ifdef __CUDA_ARCH__
    return base + 31 - __clz(word);
#else
    int bit = 0;
    while (word >>= 1)
        ++bit;
    return base + bit;
#endif
}
#ifdef PGN_EULER96_WINDOW4
PGN96_HD U96 powerOfTwoRemainderHint(const U96 &a, const U96 &n, unsigned shift,
                                     uint32_t quotient) {
    // The experimental fast domain is n.high=10, a<n, shift<=15.
    // Then a*2^shift<2^83 and n*quotient<2^84 for quotient<=32768:
    // both fit exactly in three limbs. Floating point is never an acceptance
    // criterion: signed carry/borrow correction decides the unique remainder.
    if (n.w[2] != 10 || shift > 15 || greaterEqual(a, n) || quotient > 32768) {
        U96 result = a;
        for (unsigned i = 0; i < shift; ++i)
            result = twice(result, n);
        return result;
    }
    if (shift == 0)
        return a;
    U96 shifted{};
    uint32_t carry = 0;
    PGN96_UNROLL
    for (int i = 0; i < 3; ++i) {
        const uint64_t v = (uint64_t(a.w[i]) << shift) | carry;
        shifted.w[i] = uint32_t(v);
        carry = uint32_t(v >> 32);
    }
    U96 product{};
    uint64_t productCarry = 0;
    PGN96_UNROLL
    for (int i = 0; i < 3; ++i) {
        const uint64_t v = uint64_t(n.w[i]) * quotient + productCarry;
        product.w[i] = uint32_t(v);
        productCarry = v >> 32;
    }
    int top = int(carry) - int(productCarry) - int(subtract(shifted, product));
    while (top < 0)
        top += int(add(shifted, n));
    while (top > 0 || greaterEqual(shifted, n))
        top -= int(subtract(shifted, n));
    return shifted;
}
PGN96_HD float reciprocalHint(const U96 &n) {
    return 1.0f / (float(n.w[2]) * 18446744073709551616.0f + float(n.w[1]) * 4294967296.0f +
                   float(n.w[0]));
}
PGN96_HD U96 timesPowerOfTwo(const U96 &a, const U96 &n, unsigned shift, float reciprocal) {
    if (shift == 0)
        return a;
    if (n.w[2] != 10 || shift > 15 || greaterEqual(a, n))
        return powerOfTwoRemainderHint(a, n, shift, 0);
    const float value =
        float(a.w[2]) * 18446744073709551616.0f + float(a.w[1]) * 4294967296.0f + float(a.w[0]);
    const uint32_t q = uint32_t((value * float(1U << shift)) * reciprocal);
    return powerOfTwoRemainderHint(a, n, shift, q);
}
#endif

// Mixed radix B*B*64 = 2^70 for 10*2^64<=n<11*2^64.
// a,b<2n and n<2^70/4 imply REDC(ab)<2n. This changes the
// Montgomery representation, never the Euler/Jacobi predicate.
PGN96_HD U96 montgomeryOne70(const U96 &n) {
    const uint64_t low = uint64_t(n.w[0]) | (uint64_t(n.w[1]) << 32);
    const uint32_t q = low <= 12297829382473034410ULL ? 6u : 5u;
    U96 product{};
    uint64_t carry = 0;
    PGN96_UNROLL
    for (int i = 0; i < 3; ++i) {
        const uint64_t v = uint64_t(n.w[i]) * q + carry;
        product.w[i] = uint32_t(v);
        carry = v >> 32;
    }
    U96 one{{0, 0, 64}};
    subtract(one, product);
    return one;
}
PGN96_HD U96 multiplyRadix70(const U96 &a, const U96 &b, const U96 &n, uint32_t inv) {
    uint32_t t0 = 0, t1 = 0, t2 = 0;
    PGN96_UNROLL
    for (int i = 0; i < 2; ++i) {
        const auto p0 = multiplyAdd(a.w[0], b.w[i], t0, 0);
        const auto p1 = multiplyAdd(a.w[1], b.w[i], t1, p0.high);
        const auto p2 = multiplyAdd(a.w[2], b.w[i], t2, p1.high);
        const uint32_t m = p0.low * inv;
#ifdef __CUDA_ARCH__
        const uint32_t carry = __umulhi(m, n.w[0]) + uint32_t(p0.low != 0);
#else
        const uint32_t carry = uint32_t((uint64_t(m) * n.w[0]) >> 32) + uint32_t(p0.low != 0);
#endif
        const auto q1 = multiplyAdd(m, n.w[1], p1.low, carry);
        const auto q2 = multiplyAdd(m, 10u, p2.low, q1.high);
        t0 = q1.low;
        t1 = q2.low;
        t2 = p2.high + q2.high;
    }
    const auto p0 = multiplyAdd(a.w[0], b.w[2], t0, 0);
    const auto p1 = multiplyAdd(a.w[1], b.w[2], t1, p0.high);
    const uint32_t p2 = a.w[2] * b.w[2] + t2 + p1.high;
    const uint32_t m = (p0.low * inv) & 63u;
    // Only the low six bits cancel in the final reduction. Retain every
    // other low bit and its carry; a 32-bit cancellation formula is invalid.
    const auto q0 = multiplyAdd(m, n.w[0], p0.low, 0);
    const auto q1 = multiplyAdd(m, n.w[1], p1.low, q0.high);
    const uint32_t q2 = m * 10u + p2 + q1.high;
    return U96{{(q0.low >> 6) | (q1.low << 26), (q1.low >> 6) | (q2 << 26), q2 >> 6}};
}

PGN96_HD bool test(uint64_t low, uint64_t high
#ifdef PGN_EULER96_BATCH_Q
                   ,
                   uint64_t batchQ = 0
#endif
) {
    if (!(low & 1) || high == 0 || high > UINT32_MAX)
        return false;
    const U96 n{{uint32_t(low), uint32_t(low >> 32), uint32_t(high)}};
    const U96 exponent{
        {((n.w[0] - 1) >> 1) | (n.w[1] << 31), (n.w[1] >> 1) | (n.w[2] << 31), n.w[2] >> 1}};
    if (high == 10) {
        return pgn_barrett70::test(low);
    }

#ifdef PGN_EULER96_BATCH_Q
    const auto one = montgomeryOneHinted(n, batchQ);
#else
    const auto one = montgomeryOne(n);
#endif
    const auto n0inv = inverse(n.w[0]);
    U96 result = twice(one, n);
#ifdef PGN_EULER96_LAZY
    // Admission is outside the square loop. The existing canonical path remains
    // the exact fallback for n>=R/4. Fixed-high10 callers compile this guard out.
    if (high < (1ULL << 30)) {
        U96 twoN = n;
        add(twoN, n);
        for (int bit = highestBit(exponent) - 1; bit >= 0; --bit) {
            result = high == 10 ? multiplyLazyHigh10(result, result, n, n0inv)
                                : multiplyLazy(result, result, n, n0inv);
            if ((exponent.w[bit / 32] >> (bit % 32)) & 1)
                result = twice(result, twoN);
        }
        if (greaterEqual(result, n))
            subtract(result, n);
        U96 expected = one;
        if (((low >> 1) ^ (low >> 2)) & 1) {
            expected = n;
            subtract(expected, one);
        }
        return equal(result, expected);
    }
#endif
#ifdef PGN_EULER96_WINDOW4
    if (high == 10) {
        const float reciprocal = reciprocalHint(n);
        // Exactly the original squarings, grouped as r^(2^k)*2^digit mod n.
        // No precomputed arbitrary-base table and no changed Euler predicate.
        for (int bit = highestBit(exponent) - 1; bit >= 0;) {
            const int count = bit >= 3 ? 4 : bit + 1;
            unsigned digit = 0;
            for (int j = 0; j < count; ++j, --bit) {
                result = multiply(result, result, n, n0inv);
                digit = (digit << 1) | ((exponent.w[bit / 32] >> (bit % 32)) & 1);
            }
            result = timesPowerOfTwo(result, n, digit, reciprocal);
        }
    } else
#endif
        for (int bit = highestBit(exponent) - 1; bit >= 0; --bit) {
#ifdef PGN_EULER96_SQUARE
            result = square(result, n, n0inv);
#else
        result = multiply(result, result, n, n0inv);
#endif
            if ((exponent.w[bit / 32] >> (bit % 32)) & 1)
                result = twice(result, n);
        }
    U96 expected = one;
    if (((low >> 1) ^ (low >> 2)) & 1) {
        expected = n;
        subtract(expected, one);
    }
    return equal(result, expected);
}
} // namespace pgn_euler96
#undef PGN96_HD
#undef PGN96_UNROLL
#endif

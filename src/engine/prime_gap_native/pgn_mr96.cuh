#ifndef PGN_MR96_CUH
#define PGN_MR96_CUH
// Exact integer Montgomery arithmetic reused from the paid Euler96 primitive.
// Thirteen first prime bases certify n <3317044064679887385961981.
// Sorenson/Webster, Theorem1.1, https://arxiv.org/abs/1509.00864.
// This point certificate does not certify exhaustive interval coverage.
#include "euler96.cuh"
namespace pgn_mr96 {
using pgn_euler96::U96;
__host__ __device__ __forceinline__ void half(U96 &x) {
    x.w[0] = (x.w[0] >> 1) | (x.w[1] << 31);
    x.w[1] = (x.w[1] >> 1) | (x.w[2] << 31);
    x.w[2] >>= 1;
}
__host__ __device__ __forceinline__ U96 scalarMontgomery(unsigned a, const U96 &one, const U96 &n) {
    U96 r{};
    for (int bit = 5; bit >= 0; --bit) {
        r = pgn_euler96::twice(r, n);
        if ((a >> bit) & 1) {
            const auto carry = pgn_euler96::add(r, one);
            if (carry || pgn_euler96::greaterEqual(r, n))
                pgn_euler96::subtract(r, n);
        }
    }
    return r;
}
__host__ __device__ __forceinline__ bool strongBase(const U96 &n, const U96 &d, unsigned s,
                                                    const U96 &one, const U96 &minusOne,
                                                    uint32_t inv, unsigned base) {
    const auto a = scalarMontgomery(base, one, n);
    U96 x = one;
    for (int bit = pgn_euler96::highestBit(d); bit >= 0; --bit) {
        x = pgn_euler96::multiply(x, x, n, inv);
        if ((d.w[bit / 32] >> (bit % 32)) & 1)
            x = pgn_euler96::multiply(x, a, n, inv);
    }
    if (pgn_euler96::equal(x, one) || pgn_euler96::equal(x, minusOne))
        return true;
    for (unsigned r = 1; r < s; ++r) {
        x = pgn_euler96::multiply(x, x, n, inv);
        if (pgn_euler96::equal(x, minusOne))
            return true;
    }
    return false;
}
// -1=outside the certified domain,0=composite,1=prime under the stated bound.
__host__ __device__ __forceinline__ int screen13(uint64_t low, uint64_t high) {
    if (high == 0 || high > UINT32_MAX)
        return -1;
    const U96 n{{uint32_t(low), uint32_t(low >> 32), uint32_t(high)}};

    if (!(low & 1))
        return 0;
    const U96 unit{{1, 0, 0}};
    U96 d = n;
    pgn_euler96::subtract(d, unit);
    unsigned s = 0;
    while (!(d.w[0] & 1)) {
        half(d);
        ++s;
    }
    const U96 one = pgn_euler96::montgomeryOne(n);
    U96 minusOne = n;
    pgn_euler96::subtract(minusOne, one);
    const uint32_t inv = pgn_euler96::inverse(n.w[0]);
    const unsigned bases[13] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41};
    for (const auto base : bases)
        if (!strongBase(n, d, s, one, minusOne, inv, base))
            return 0;
    return 1;
}
// Screening positives above this bound are not primality proofs.
__host__ __device__ __forceinline__ int prime12(uint64_t low, uint64_t high) {
    const U96 n{{uint32_t(low), uint32_t(low >> 32), uint32_t(high)}};
    const U96 bound{{0x2410a5fdu, 0x51adc5b2u, 0x2be69u}};
    if (high > UINT32_MAX || pgn_euler96::greaterEqual(n, bound))
        return -1;
    return screen13(low, high);
}

} // namespace pgn_mr96
#endif

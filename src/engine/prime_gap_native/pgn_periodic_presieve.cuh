#pragma once
#include <stdint.h>
#include <vector>
// Independent CUDA adaptation of the periodic-table pre-sieve operator used by
// primesieve (PreSieve.cpp, BSD-licensed project). This is generated from exact
// divisibility; its byte encoding differs: set bit = composite class30 slot.
// The supported 67–95-bit intervals are all far larger than these small primes.
#ifndef PGN_PRESIEVE_GROUPS
#define PGN_PRESIEVE_GROUPS 0
#endif
static_assert(PGN_PRESIEVE_GROUPS == 0 || PGN_PRESIEVE_GROUPS == 1 || PGN_PRESIEVE_GROUPS == 3 ||
                  PGN_PRESIEVE_GROUPS == 5,
              "Supported periodic-filter group counts");
constexpr unsigned PgnPreMax = PGN_PRESIEVE_GROUPS == 0   ? 13
                               : PGN_PRESIEVE_GROUPS == 1 ? 23
                               : PGN_PRESIEVE_GROUPS == 3 ? 41
                                                          : 59;
constexpr unsigned PgnPreProducts[5] = {7429, 899, 1517, 2021, 3127};
constexpr unsigned PgnPrePrimes[5][3] = {
    {17, 19, 23}, {29, 31, 0}, {37, 41, 0}, {43, 47, 0}, {53, 59, 0}};
inline std::vector<uint64_t> pgnMakePreTable(unsigned group) {
    const unsigned bits = 8 * PgnPreProducts[group], words = (bits + 63) / 64 + 1;
    std::vector<uint64_t> table(words, 0);
    constexpr unsigned cls[8] = {1, 7, 11, 13, 17, 19, 23, 29};
    for (unsigned j = 0; j < words * 64; ++j) {
        const unsigned s = j % bits, n = 30 * (s >> 3) + cls[s & 7];
        for (unsigned p : PgnPrePrimes[group])
            if (p && n % p == 0) {
                table[j >> 6] |= uint64_t(1) << (j & 63);
                break;
            }
    }
    return table;
}
#ifdef __CUDACC__
__device__ __constant__ const uint64_t *pgn_pre_tables[5];
template <unsigned I> __device__ __forceinline__ unsigned pgnPrePhase(uint64_t low, uint64_t high) {
    constexpr uint64_t P = I == 0   ? 7429
                           : I == 1 ? 899
                           : I == 2 ? 1517
                           : I == 3 ? 2021
                                    : 3127,
                       M = 30 * P;
    constexpr uint64_t R64 = (UINT64_MAX % M + 1) % M;
    // low:high is a class30 origin. Reduction modulo30P retains its exact phase.
    return unsigned(((low % M + (high % M) * R64) % M) / 30);
}
template <unsigned I>
__device__ __forceinline__ uint64_t pgnPreWord(unsigned phase, unsigned word) {
    constexpr unsigned P = I == 0 ? 7429 : I == 1 ? 899 : I == 2 ? 1517 : I == 3 ? 2021 : 3127;
    const unsigned byte = (phase + 8 * word) % P, q = byte >> 3, shift = (byte & 7) * 8;
    const auto *table = pgn_pre_tables[I];
    const uint64_t a = __ldg(table + q);
    return (a >> shift) | (shift ? __ldg(table + q + 1) << (64 - shift) : 0);
}
#endif

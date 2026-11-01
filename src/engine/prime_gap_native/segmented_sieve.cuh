#pragma once
// Fused class30 sieve with full high/low origin phases.
// The bitmap representation and wheel profile match candidate_walk.cuh.
#ifndef SCOUT_CANDIDATE_WALK_CUH
#error Include candidate_walk.cuh before segmented_sieve.cuh
#endif
#if !defined(PGN_FUSED_SIEVE) || !defined(PGN_MARK_FULL_FAST) || !defined(PGN_MARK_BANK_ROTATE)
#error Segmented sieve requires the unchanged retained marker compilation profile
#endif
#if PGN_PRESIEVE_GROUPS != 1
#error Segmented sieve requires the retained Tiny1001-periodic7429-Low7 profile
#endif
namespace segmented_sieve {
template <bool CacheRoots = false, bool EventWheel = false, bool WordPresieve = false,
          bool TinyWheel = false>
__device__ __forceinline__ static void
body(const uint64_t *__restrict__ primes, const uint64_t *__restrict__ invp,
     const uint64_t *__restrict__ r64, const uint32_t *__restrict__ itemPidx,
     const uint32_t *__restrict__ itemK0, uint32_t nitems, uint64_t Alo, uint64_t Ahi,
     uint64_t tileSlots, uint64_t ntiles, uint64_t *__restrict__ g_bm, uint32_t tileWords,
     const uint32_t *__restrict__ wpidx, int wheel_on,
     const uint32_t *__restrict__ rootSteps = nullptr, uint32_t logicalX = 0, uint32_t logicalY = 0,
     uint32_t logicalGridX = 0) {
    static_assert(CacheRoots && EventWheel && WordPresieve && TinyWheel,
                  "Segmented sieve fixed retained profile only");
    const uint32_t sieveX = logicalGridX ? logicalX : blockIdx.x;
    const uint32_t sieveY = logicalGridX ? logicalY : blockIdx.y;
    const uint32_t sieveGridX = logicalGridX ? logicalGridX : gridDim.x;
#ifdef PGN_FUSED_SIEVE
    // A logical bitmap region is exactly30*2^25 numbers/2^28 wheel slots.
    // Flatten only the submission boundary: each Y cohort retains the same
    // base, disjoint bitmap and tile loop as the original separate launch.
    const uint64_t batch_offset = uint64_t(sieveY) * (30ULL << 25);
    const uint64_t original_lo = Alo;
    Alo += batch_offset;
    Ahi += uint64_t(Alo < original_lo);
    g_bm += uint64_t(sieveY) * ((1ULL << 28) / 64);
#endif
    if constexpr (WordPresieve)
        if (tileSlots != (1ULL << 19) || blockDim.x != 512)
            __trap();
    extern __shared__ uint64_t sh[];
    __shared__ uint32_t lowPhase[WordPresieve ? 7 : 1];
    __shared__ uint64_t wpat[TinyWheel ? 1 : 127];
    __shared__ uint32_t tinyPhase;
    // One residue per prime, reused by all chunks and subsequent CTA tiles.
    __shared__ uint32_t originResidues[CacheRoots ? 7834 : 1];
    __shared__ uint32_t wpj[24];
    __shared__ uint32_t eventPrimeCursor;
#if PGN_PRESIEVE_GROUPS > 0
    __shared__ unsigned pgn_pre_phase[PGN_PRESIEVE_GROUPS];
#endif
    /* Step tables for the item loop (built once per CTA, ~15 ops per thread).
       A mark of prime p sits at a value p*m with m coprime to 30; moving to
       the next mark advances m by dm (one of the coprime gaps 2,4,6) and the
       VALUE by dm*p, which is a SLOT advance of
           8*dm*(p/30) + 8*floor((cv + dm*(p%30))/30)
                       + clsidx[(cv + dm*(p%30)) % 30] - clsidx[cv]
       where cv is the current value's class and dm follows the fixed cyclic
       class order, so the whole bracket depends only on
       (m-class index, p%30, position in the cycle).  s_step[jstart][unit][st]
       holds that bracket and s_dm8 the dm at each position, so an item's
       eight steps are 8 IMAD + 8 shared loads instead of the ~24 arithmetic
       ops (two /30, two %30, two clsidx lookups, ~8 table reads) per step the
       loop used to run for EVERY item. */
    __shared__ uint32_t s_step[8 * 8 * 8]; /* [jstart][unit p%30][st] */
    __shared__ uint32_t s_dm8[8];
    __shared__ uint8_t s_unit[30];
    if (threadIdx.x < 30u) {
        uint32_t r = threadIdx.x, j = 0xFFu;
        for (uint32_t t = 0; t < 8u; t++)
            if (c30_cls[t] == r)
                j = t;
        s_unit[r] = (uint8_t)j;
    }
    if (threadIdx.x >= 32u && threadIdx.x < 40u) {
        uint32_t j = threadIdx.x - 32u, j2 = (j + 1u) & 7u;
        s_dm8[j] = c30_cls[j2] - c30_cls[j] + ((j2 == 0u) ? 30u : 0u);
    }
    // Complete all512 shared entries for every supported launch width. At the
    // reference512threads this is byte-for-byte the same per-thread arithmetic.
    for (uint32_t i = threadIdx.x; i < 512u; i += blockDim.x) {
        uint32_t jstart = i >> 6, ri = (i >> 3) & 7u, st = i & 7u;
        uint32_t pmod = c30_cls[ri];
        uint32_t js = (jstart + st) & 7u, js2 = (js + 1u) & 7u;
        uint32_t dm = c30_cls[js2] - c30_cls[js] + ((js2 == 0u) ? 30u : 0u);
        uint32_t cv = (pmod * c30_cls[js]) % 30u;
        uint32_t w = cv + dm * pmod;
        s_step[i] = 8u * (w / 30u) + (uint32_t)c30_clsidx[w % 30u] - (uint32_t)c30_clsidx[cv];
    }
    __syncthreads();
    for (uint64_t tile = sieveX; tile < ntiles; tile += sieveGridX) {
        for (uint32_t w = threadIdx.x; w < tileWords; w += blockDim.x)
            sh[w] = 0;
        __syncthreads();
        /* tv = (Ahi:Alo) + ((tile*tileSlots)>>3)*30 + 1 as a 64-bit pair.
           The addend is < 2^35 (tile*tileSlots < 2^28), so the 128-bit sum
           is one carry-propagating add (was a u128 expression). */
        uint64_t t_lo = Alo + ((tile * tileSlots) >> 3) * 30u;
        uint64_t t_hi = Ahi + ((t_lo < Alo) ? 1ULL : 0ULL);
        t_lo += 1ULL;
        t_hi += (t_lo == 0ULL) ? 1ULL : 0ULL;
        if constexpr (TinyWheel) {
            static_assert(CacheRoots && EventWheel && WordPresieve,
                          "Tinywheel private fixed profile");
            // Published by the retained root barrier below. No added barrier.
            if (threadIdx.x == 0)
                tinyPhase = tile == sieveX
                                ? pgn_tinywheel::phase(t_lo - 1, t_hi - uint64_t(t_lo == 0))
                                : pgn_tinywheel::advance(tinyPhase);
        }
        if constexpr (CacheRoots) {
            for (uint32_t pi = threadIdx.x; pi < 7834; pi += blockDim.x) {
                const uint64_t p = primes[pi];
                if (tile == sieveX) {
                    const uint64_t pv = invp[pi];
                    const uint64_t hi = t_fastmod64(t_hi, p, pv), lo = t_fastmod64(t_lo, p, pv);
                    originResidues[pi] = uint32_t(t_fastmod64(hi * r64[pi] + lo, p, pv));
                } else if (sieveGridX != 1 || p <= 4096) {
                    if constexpr (EventWheel)
                        if (sieveGridX == 1 && pi >= pgn_event210::DenseBegin &&
                            pi < pgn_event210::DenseEnd)
                            continue;
                    // Small/multi-item primes retain the qualified residue update.
                    // For a large one-item prime on contiguous tiles this word
                    // instead stores its next exact mark and multiplier phase.
                    const uint32_t next = originResidues[pi] + rootSteps[pi];
                    originResidues[pi] = next >= p ? next - uint32_t(p) : next;
                }
            }
            __syncthreads();
        }

#if PGN_PRESIEVE_GROUPS > 0
        // t_lo:t_hi is origin+1, undo that increment before reducing the phase.
        const uint64_t preLow = t_lo - 1, preHigh = t_hi - uint64_t(t_lo == 0);
        if (threadIdx.x == 0)
            pgn_pre_phase[0] = pgnPrePhase<0>(preLow, preHigh);
#if PGN_PRESIEVE_GROUPS >= 3
        if (threadIdx.x == 1)
            pgn_pre_phase[1] = pgnPrePhase<1>(preLow, preHigh);
        if (threadIdx.x == 2)
            pgn_pre_phase[2] = pgnPrePhase<2>(preLow, preHigh);
#endif
#if PGN_PRESIEVE_GROUPS >= 5
        if (threadIdx.x == 3)
            pgn_pre_phase[3] = pgnPrePhase<3>(preLow, preHigh);
        if (threadIdx.x == 4)
            pgn_pre_phase[4] = pgnPrePhase<4>(preLow, preHigh);
#endif
#endif
        if (!TinyWheel && wheel_on && threadIdx.x < 3u) {
            int pi = (int)wpidx[threadIdx.x];
            uint64_t p = primes[pi], pv = invp[pi];
            uint64_t vm;
            if constexpr (CacheRoots)
                vm = originResidues[pi];
            else {
                uint64_t vh = t_fastmod64(t_hi, p, pv), vl = t_fastmod64(t_lo, p, pv);
                vm = t_fastmod64(vh * r64[pi] + vl, p, pv);
            }
            uint64_t d = vm ? (p - vm) : 0;
            uint64_t phi1 = 1u + d;
            uint64_t r0 = phi1 % 30u;
            uint64_t pm30 = p % 30u;
            uint32_t mask = c30_perm[pm30];
            uint64_t kadj = 0;
            while (!((mask >> ((r0 + kadj * pm30) % 30u)) & 1u))
                kadj++;
            uint64_t phi = phi1 + kadj * p;
            uint64_t r = phi % 30u;
            uint64_t pos = (phi / 30u) * 8u + (uint64_t)c30_clsidx[r];
            uint32_t step[8];
            uint64_t j = (uint64_t)c30_clsidx[(r * c30_pinv[pm30]) % 30u];
            uint64_t ph = phi;
            for (int st = 0; st < 8; st++) {
                uint64_t j2 = (j + 1u) & 7u;
                uint64_t dm = (uint64_t)c30_cls[j2] - (uint64_t)c30_cls[j];
                if (j2 == 0u)
                    dm += 30u;
                uint64_t ph2 = ph + dm * p;
                step[st] = (uint32_t)((ph2 / 30u) * 8u + (uint64_t)c30_clsidx[ph2 % 30u] -
                                      ((ph / 30u) * 8u + (uint64_t)c30_clsidx[ph % 30u]));
                ph = ph2;
                j = j2;
            }
            uint32_t cum = 0;
            for (int st = 0; st < 8; st++) {
                wpj[threadIdx.x * 8 + st] = (uint32_t)pos + cum;
                cum += step[st];
            }
        }
        if constexpr (WordPresieve) {
            static_assert(EventWheel && CacheRoots,
                          "Word tables replace the exact hybrid prime range only");
            if (threadIdx.x == 0)
                lowPhase[0] =
                    (tile == sieveX)
                        ? pgn_lowwords::firstPhase<29, 11, 24>(t_lo - 1, t_hi - uint64_t(t_lo == 0))
                        : pgn_lowwords::nextPhase<29>(lowPhase[0]);
            if (threadIdx.x == 1)
                lowPhase[1] =
                    (tile == sieveX)
                        ? pgn_lowwords::firstPhase<31, 27, 16>(t_lo - 1, t_hi - uint64_t(t_lo == 0))
                        : pgn_lowwords::nextPhase<31>(lowPhase[1]);
            if (threadIdx.x == 2)
                lowPhase[2] =
                    (tile == sieveX)
                        ? pgn_lowwords::firstPhase<37, 35, 12>(t_lo - 1, t_hi - uint64_t(t_lo == 0))
                        : pgn_lowwords::nextPhase<37>(lowPhase[2]);
            if (threadIdx.x == 3)
                lowPhase[3] =
                    (tile == sieveX)
                        ? pgn_lowwords::firstPhase<41, 34, 16>(t_lo - 1, t_hi - uint64_t(t_lo == 0))
                        : pgn_lowwords::nextPhase<41>(lowPhase[3]);
            if (threadIdx.x == 4)
                lowPhase[4] =
                    (tile == sieveX)
                        ? pgn_lowwords::firstPhase<43, 31, 41>(t_lo - 1, t_hi - uint64_t(t_lo == 0))
                        : pgn_lowwords::nextPhase<43>(lowPhase[4]);
            if (threadIdx.x == 5)
                lowPhase[5] =
                    (tile == sieveX)
                        ? pgn_lowwords::firstPhase<47, 19, 25>(t_lo - 1, t_hi - uint64_t(t_lo == 0))
                        : pgn_lowwords::nextPhase<47>(lowPhase[5]);
            if (threadIdx.x == 6)
                lowPhase[6] =
                    (tile == sieveX)
                        ? pgn_lowwords::firstPhase<53, 36, 15>(t_lo - 1, t_hi - uint64_t(t_lo == 0))
                        : pgn_lowwords::nextPhase<53>(lowPhase[6]);
        }
        if constexpr (EventWheel) {
            static_assert(CacheRoots, "Hybrid dense frontiers require cached roots");
            static_assert(PgnPreMax == 23, "Hybrid split is qualified for presieve group1");
            if (sieveGridX != 1)
                __trap();
            const unsigned lane = threadIdx.x & 31u;
            constexpr unsigned mask = 0xffffffffu;
            if (tile == sieveX) {
                const uint32_t origin210 =
                    30u * ((((originResidues[wpidx[0]] + 6u) % 7u) * 4u) % 7u);
                for (unsigned pi = threadIdx.x + pgn_event210::DenseBegin;
                     pi < pgn_event210::DenseEnd; pi += blockDim.x) {
                    const uint32_t p = uint32_t(primes[pi]), vm = originResidues[pi],
                                   pm30 = p % 30u;
                    const uint32_t phi1 = 1u + (vm ? p - vm : 0u), r0 = phi1 % 30u,
                                   permutation = c30_perm[pm30];
                    uint32_t adjust = 0;
                    while (!((permutation >> ((r0 + adjust * pm30) % 30u)) & 1u))
                        ++adjust;
                    const uint32_t phiBase = phi1 + adjust * p;
                    const uint32_t qBase = (((origin210 + phiBase % 210u) % 210u) *
                                            unsigned(pgn_event210::inverse[p % 210u])) %
                                           210u;
                    const uint32_t phase = pgn_event210::phase[qBase];
                    const uint64_t value =
                        uint64_t(phiBase) + uint64_t(pgn_event210::jump[qBase]) * p;
                    const uint32_t first =
                        uint32_t((value / 30u) * 8u) + uint32_t(c30_clsidx[value % 30u]);
                    if (first >= (1u << 17) || phase >= 48u)
                        __trap();
                    originResidues[pi] = (first << 6) | phase;
                }
            }
            if (threadIdx.x == 0)
                eventPrimeCursor =
                    pgn_event210::DenseBegin + (WordPresieve ? pgn_lowwords::Count : 0u);
            __syncthreads();
            for (;;) {
                uint32_t pair = 0;
                if (lane == 0)
                    pair = atomicAdd(&eventPrimeCursor, 2u);
                pair = __shfl_sync(mask, pair, 0);
                if (pair >= pgn_event210::DenseEnd)
                    break;
                const uint32_t pi = pair + (lane >> 4);
                // The last odd prime, if any, uses only its valid half-warp.
                if (pi < pgn_event210::DenseEnd) {
                    const unsigned halfLane = lane & 15u, halfMask = 0xffffu << (lane & 16u);
                    const uint32_t p = uint32_t(primes[pi]), packed = originResidues[pi],
                                   phase = packed & 63u;
                    const uint32_t *row =
                        pgn_event210::tinyPrefix + uint32_t(c30_clsidx[p % 30u]) * 96u;
                    const uint32_t factor = (p / 30u) * 8u;
                    const uint32_t va = __ldg(row + phase + halfLane);
                    const uint32_t vb = __ldg(row + phase + halfLane + 16u);
                    const uint32_t vc = __ldg(row + phase + halfLane + 32u);
                    uint32_t lo = factor * (va & 65535u) + (va >> 16);
                    uint32_t mid = factor * (vb & 65535u) + (vb >> 16);
                    uint32_t hi = factor * (vc & 65535u) + (vc >> 16);
                    const uint32_t first = __shfl_sync(halfMask, lo, 0, 16);
                    const uint32_t span = __shfl_sync(halfMask, hi, 15, 16) - first;
                    lo -= first;
                    mid -= first;
                    hi -= first;
                    uint32_t base = packed >> 6;
                    const uint32_t limit = uint32_t(tileSlots), advance = 56u * p;
                    while (base + span < limit) {
                        uint32_t at = base + lo;
                        atomicOr(((unsigned int *)sh) + (at >> 5), 1u << (at & 31u));
                        at = base + mid;
                        atomicOr(((unsigned int *)sh) + (at >> 5), 1u << (at & 31u));
                        at = base + hi;
                        atomicOr(((unsigned int *)sh) + (at >> 5), 1u << (at & 31u));
                        base += advance;
                    }
                    const uint32_t a = base + lo, b = base + mid, c = base + hi;
                    const bool validA = a < limit, validB = b < limit, validC = c < limit;
                    if (validA)
                        atomicOr(((unsigned int *)sh) + (a >> 5), 1u << (a & 31u));
                    if (validB)
                        atomicOr(((unsigned int *)sh) + (b >> 5), 1u << (b & 31u));
                    if (validC)
                        atomicOr(((unsigned int *)sh) + (c >> 5), 1u << (c & 31u));
                    // Rank in the original ordered48-event sequence, not a lane ID.
                    const uint32_t ordinal = __popc(__ballot_sync(halfMask, validA)) +
                                             __popc(__ballot_sync(halfMask, validB)) +
                                             __popc(__ballot_sync(halfMask, validC));
                    const uint32_t nextA = __shfl_sync(halfMask, lo, ordinal & 15u, 16);
                    const uint32_t nextB = __shfl_sync(halfMask, mid, ordinal & 15u, 16);
                    const uint32_t nextC = __shfl_sync(halfMask, hi, ordinal & 15u, 16);
                    const uint32_t next = base + (ordinal < 16u   ? nextA
                                                  : ordinal < 32u ? nextB
                                                                  : nextC);
                    if (halfLane == 0) {
                        if (ordinal >= 48u || next < limit || next - limit >= (1u << 17))
                            __trap();
                        uint32_t nextPhase = phase + ordinal;
                        if (nextPhase >= 48u)
                            nextPhase -= 48u;
                        originResidues[pi] = ((next - limit) << 6) | nextPhase;
                    }
                }
                // Both subgroups finish this pair before claiming the next pair.
                __syncwarp(mask);
            }
        }
        {
            /* Item loop, two partitionings (measured 2026-10-06, 2e13, mark-only):
           - SMALL item tables (P0_ITEM_STRIDE_CUT items ~= 40 KB of rows; the
             production depth is 3568 items = 28 KB): per-THREAD contiguous
             slices, so consecutive items of one prime share a setup
             (`pi != cur_pi`) - that reuse is worth 2.5 s of mark at P=15000.
           - LARGE tables: warp-STRIDED, so the warp's 32 lanes read 32
             CONSECUTIVE items = one 128-byte sector.  With contiguous slices
             the lanes sit nitems/nthreads items apart (157 at P=1e6 = 628 B):
             one sector per lane, 32 L1 misses per load, and the table lives in
             L2 (the 64 KB shared tile leaves ~28 KB of L1), so the mark paid a
             full L2 latency per item.  Measured head to head: P=150000
             50.8 -> 25.5 s, P=1e6 251.6 -> 85.1 s, while at P=15000 the
             contiguous slices stay 2.5 s cheaper (15.4 vs 17.9 s) - hence the
             split.  See section 23 of docs/PHASE0_scan_bench.md. */
            uint32_t nthreads = blockDim.x;
            uint32_t it_first = threadIdx.x, it_end = nitems, it_step = nthreads;
            if (nitems <= P0_ITEM_STRIDE_CUT) {
                uint32_t per = nitems / nthreads, rem = nitems - per * nthreads;
                it_first = threadIdx.x * per + (threadIdx.x < rem ? threadIdx.x : rem);
                it_end = it_first + per + (threadIdx.x < rem ? 1u : 0u);
                it_step = 1;
            }
            int cur_pi = -1;
            uint32_t cur_p = 0;
            uint32_t pos_base = 0;
            uint32_t phase_base = 0;
            uint32_t step[8];
            for (uint32_t it = it_first; it < it_end; it += it_step) {
                int pi = (int)itemPidx[it];
                uint32_t k0 = itemK0[it];
                if constexpr (EventWheel)
                    if (unsigned(pi) < pgn_event210::DenseEnd)
                        continue;
                if (pi != cur_pi) {
                    cur_pi = pi;
                    uint64_t p = primes[pi];
                    cur_p = (uint32_t)p;
                    const uint32_t pm30 = cur_p % 30u;
                    uint32_t j;
                    if constexpr (CacheRoots) {
                        if (sieveGridX == 1 && cur_p > 4096 && tile != sieveX) {
                            // p>4096 has exactly one item. The previous tile left
                            // the first unmarked slot and exact wheel phase here.
                            const uint32_t packed = originResidues[pi];
                            pos_base = packed >> 3;
                            j = packed & 7u;
                        } else {
                            uint64_t pv = invp[pi];
                            uint64_t vm;
                            if constexpr (CacheRoots)
                                vm = originResidues[pi];
                            else {
                                uint64_t vh = t_fastmod64(t_hi, p, pv),
                                         vl = t_fastmod64(t_lo, p, pv);
                                vm = t_fastmod64(vh * r64[pi] + vl, p, pv);
                            }
                            uint64_t d = vm ? (p - vm) : 0;
                            uint32_t phi1 = (uint32_t)(1u + d);
                            uint32_t r0 = phi1 % 30u;
                            uint32_t mask = c30_perm[pm30];
                            uint32_t kadj = 0;
                            while (!((mask >> ((r0 + kadj * pm30) % 30u)) & 1u))
                                kadj++;
                            uint32_t phi = phi1 + kadj * cur_p;
                            uint32_t r = phi % 30u;
                            pos_base = (phi / 30u) * 8u + (uint32_t)c30_clsidx[r];
                            j = (uint32_t)c30_clsidx[(r * c30_pinv[pm30]) % 30u];
                        }
                    } else {
                        uint64_t pv = invp[pi];
                        uint64_t vm;
                        if constexpr (CacheRoots)
                            vm = originResidues[pi];
                        else {
                            uint64_t vh = t_fastmod64(t_hi, p, pv), vl = t_fastmod64(t_lo, p, pv);
                            vm = t_fastmod64(vh * r64[pi] + vl, p, pv);
                        }
                        uint64_t d = vm ? (p - vm) : 0;
                        uint32_t phi1 = (uint32_t)(1u + d);
                        uint32_t r0 = phi1 % 30u;
                        uint32_t mask = c30_perm[pm30];
                        uint32_t kadj = 0;
                        while (!((mask >> ((r0 + kadj * pm30) % 30u)) & 1u))
                            kadj++;
                        uint32_t phi = phi1 + kadj * cur_p;
                        uint32_t r = phi % 30u;
                        pos_base = (phi / 30u) * 8u + (uint32_t)c30_clsidx[r];
                        j = (uint32_t)c30_clsidx[(r * c30_pinv[pm30]) % 30u];
                    }
                    phase_base = j;
                    uint32_t k8 = (cur_p / 30u) * 8u;
                    /* & 7u keeps the index in range should a caller ever feed a
                   prime ≡ 0 mod 30 (s_unit maps non-units to 0xFF); the item
                   table only ever holds primes > 13, so this is a guard. */
                    uint32_t ri = s_unit[pm30] & 7u;
                    const uint32_t *tp = &s_step[(j << 6) + (ri << 3)];
#pragma unroll
                    for (int st = 0; st < 8; st++)
                        step[st] = k8 * s_dm8[(j + (uint32_t)st) & 7u] + tp[st];
                }
                uint32_t p32 = pos_base + 256u * cur_p * k0;
                if (p32 >= (uint32_t)tileSlots) {
                    if constexpr (CacheRoots)
                        if (sieveGridX == 1 && cur_p > 4096)
                            originResidues[pi] = ((p32 - uint32_t(tileSlots)) << 3) | phase_base;
                    continue;
                }
#if defined(PGN_MARK_FULL_FAST)
                // Private all-true Depth80000 profile: the Event210 stage owns
                // pi<561, so this queue has p>=4099 and exactly one chunk/prime.
                // Eight positive wheel steps sum to 8*p; the last event is at
                // p32 + (8*p-step[7]). Whole cycles leave phase_base unchanged.
                const uint32_t full_cycle = 8u * cur_p;
                const uint32_t last_offset = full_cycle - step[7];
// Under the pinned bank/step contract this loop terminates after
// at most16 iterations: D=8*p>=32792 and L=524288. The loop
// counter carried no semantic information. Do not unroll the loop.
// This is NOT a malformed-bank repair or a broader input contract.
#pragma unroll 1
                while (p32 + last_offset < uint32_t(tileSlots)) {
#pragma unroll
                    for (int st = 0; st < 8; ++st) {
                        atomicOr(((unsigned int *)sh) + (p32 >> 5), 1u << (p32 & 31u));
                        p32 += step[st];
                    }
                }
#endif
                unsigned tail_phase = 0;
                for (int r8 = 0; r8 < 32; r8++) {
                    int done = 0;
#pragma unroll
                    for (int st = 0; st < 8; st++) {
                        /* positions are strictly monotone (step >= +1), so once
                       past the tile the rest of the chunk is skipped instead
                       of walking 64 guarded iterations - the dominant item
                       cost at deep sieve depths, and identical marks. */
                        if (p32 >= (uint32_t)tileSlots) {
                            tail_phase = unsigned(st);
                            done = 1;
                            break;
                        }
                        atomicOr(((unsigned int *)sh) + (p32 >> 5), 1u << (p32 & 31u));
                        p32 += step[st];
                    }
                    if (done)
                        break;
                }
                if constexpr (CacheRoots)
                    if (sieveGridX == 1 && cur_p > 4096) {
                        // 32 complete wheel cycles advance256*p > tileSlots;
                        // thus this is always the first mark of a later tile.
                        originResidues[pi] =
                            ((p32 - uint32_t(tileSlots)) << 3) | ((phase_base + tail_phase) & 7u);
                    }
            }
        }
        __syncthreads();
        if (wheel_on) {
            const uint64_t P = 8008ull, ASZ = 8128ull;
            if constexpr (!TinyWheel) {
                for (uint32_t w = threadIdx.x; w < 127u; w += blockDim.x)
                    wpat[w] = 0ull;
                __syncthreads();
                for (int wi = 0; wi < 3; wi++) {
                    uint64_t p = (wi == 0) ? 7u : (wi == 1) ? 11u : 13u;
                    uint64_t stride8 = 8u * p;
                    uint64_t nt = ASZ / stride8 + 2u;
                    for (uint64_t idx = threadIdx.x; idx < 8u * nt; idx += blockDim.x) {
                        uint64_t j = idx & 7u, t = idx >> 3;
                        uint64_t pos = (uint64_t)wpj[wi * 8 + (int)j] + t * stride8;
                        if (pos >= ASZ)
                            continue;
                        uint64_t q = pos % P;
                        atomicOr((unsigned long long *)&wpat[q >> 6], 1ull << (q & 63u));
                        if (q < 64u)
                            atomicOr((unsigned long long *)&wpat[(q + P) >> 6],
                                     1ull << ((q + 8u) & 63u));
                    }
                }
                __syncthreads();
            } // Reference-only per-tile wheel rebuild and its barriers.
            uint32_t lowIndex0 = 0;
            if constexpr (WordPresieve)
                lowIndex0 = pgn_lowwords::firstIndex<29>(lowPhase[0]);
            uint32_t lowIndex1 = 0;
            if constexpr (WordPresieve)
                lowIndex1 = pgn_lowwords::firstIndex<31>(lowPhase[1]);
            uint32_t lowIndex2 = 0;
            if constexpr (WordPresieve)
                lowIndex2 = pgn_lowwords::firstIndex<37>(lowPhase[2]);
            uint32_t lowIndex3 = 0;
            if constexpr (WordPresieve)
                lowIndex3 = pgn_lowwords::firstIndex<41>(lowPhase[3]);
            uint32_t lowIndex4 = 0;
            if constexpr (WordPresieve)
                lowIndex4 = pgn_lowwords::firstIndex<43>(lowPhase[4]);
            uint32_t lowIndex5 = 0;
            if constexpr (WordPresieve)
                lowIndex5 = pgn_lowwords::firstIndex<47>(lowPhase[5]);
            uint32_t lowIndex6 = 0;
            if constexpr (WordPresieve)
                lowIndex6 = pgn_lowwords::firstIndex<53>(lowPhase[6]);
            for (uint32_t w = threadIdx.x; w < tileWords; w += blockDim.x) {
                uint64_t val;
                if constexpr (TinyWheel)
                    val = pgn_tinywheel::word(tinyPhase, w);
                else {
                    uint64_t idx = ((uint64_t)64u * w) % P;
                    uint32_t k = (uint32_t)(idx >> 6), r2 = (uint32_t)(idx & 63u);
                    val = (wpat[k] >> r2) | (r2 ? (wpat[k + 1] << (64 - r2)) : 0ull);
                }
#if PGN_PRESIEVE_GROUPS > 0
                val |= pgnPreWord<0>(pgn_pre_phase[0], w);
#if PGN_PRESIEVE_GROUPS >= 3
                val |= pgnPreWord<1>(pgn_pre_phase[1], w) | pgnPreWord<2>(pgn_pre_phase[2], w);
#endif
#if PGN_PRESIEVE_GROUPS >= 5
                val |= pgnPreWord<3>(pgn_pre_phase[3], w) | pgnPreWord<4>(pgn_pre_phase[4], w);
#endif
#endif
                if constexpr (WordPresieve) {
                    val |= __ldg(pgn_lowwords::masks + 0 + lowIndex0);
                    lowIndex0 = pgn_lowwords::nextIndex<29>(lowIndex0);
                    val |= __ldg(pgn_lowwords::masks + 29 + lowIndex1);
                    lowIndex1 = pgn_lowwords::nextIndex<31>(lowIndex1);
                    val |= __ldg(pgn_lowwords::masks + 60 + lowIndex2);
                    lowIndex2 = pgn_lowwords::nextIndex<37>(lowIndex2);
                    val |= __ldg(pgn_lowwords::masks + 97 + lowIndex3);
                    lowIndex3 = pgn_lowwords::nextIndex<41>(lowIndex3);
                    val |= __ldg(pgn_lowwords::masks + 138 + lowIndex4);
                    lowIndex4 = pgn_lowwords::nextIndex<43>(lowIndex4);
                    val |= __ldg(pgn_lowwords::masks + 181 + lowIndex5);
                    lowIndex5 = pgn_lowwords::nextIndex<47>(lowIndex5);
                    val |= __ldg(pgn_lowwords::masks + 228 + lowIndex6);
                    lowIndex6 = pgn_lowwords::nextIndex<53>(lowIndex6);
                }
                sh[w] |= val;
            }
            __syncthreads();
        }
        uint64_t *out = g_bm + tile * tileWords;
        for (uint32_t w = threadIdx.x; w < tileWords; w += blockDim.x)
            out[w] = sh[w];
        __syncthreads();
    }
}
} // namespace segmented_sieve

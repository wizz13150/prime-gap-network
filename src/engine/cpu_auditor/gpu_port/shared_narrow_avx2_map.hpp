#pragma once
// AVX2 mapping of optimized_narrow_euler.cuh (promoted manifest96C600...).
// Same raw-square columns, bucket reciprocal, fold, radix16 stream and verdict.
// SOURCE CANDIDATE ONLY: no correctness or speed qualification yet.
#include <array>
#include <cstdint>
#include "optimized_narrow_euler.cuh"

#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
#include <immintrin.h>
#define PGN_NARROW_AVX2 __attribute__((target("avx2"))) inline
namespace pgn_promoted_avx2 {
using V = __m256i; // Four zero-extended uint32 fields, one per uint64 lane.
struct Value { V x, y, z; };
struct Cache { std::array<optimized_narrow_euler::State, 4> lane{}; };
PGN_NARROW_AVX2 V constant(uint64_t x) { return _mm256_set1_epi64x(static_cast<long long>(x)); }
PGN_NARROW_AVX2 V lo32(V x) { return _mm256_and_si256(x, constant(UINT32_MAX)); }
PGN_NARROW_AVX2 V hi32(V x) { return _mm256_srli_epi64(x, 32); }
PGN_NARROW_AVX2 V add(V a, V b) { return _mm256_add_epi64(a, b); }
PGN_NARROW_AVX2 V sub(V a, V b) { return _mm256_sub_epi64(a, b); }
PGN_NARROW_AVX2 V mul(V a, V b) { return _mm256_mul_epu32(a, b); }
PGN_NARROW_AVX2 V multiply_low(V a, V b) { return lo32(mul(a, b)); }
PGN_NARROW_AVX2 V lanes(const uint64_t *x) {
    return _mm256_set_epi64x(static_cast<long long>(x[3]), static_cast<long long>(x[2]),
                            static_cast<long long>(x[1]), static_cast<long long>(x[0]));
}
PGN_NARROW_AVX2 void add_product(V &l, V &m, V &h, V x, V y) {
    const V p = mul(x, y);
    V s = add(l, lo32(p)); l = lo32(s);
    s = add(add(m, hi32(p)), hi32(s)); m = lo32(s); h = lo32(add(h, hi32(s)));
}
PGN_NARROW_AVX2 V negative_inverse32(V n) {
    V x = n;
    for (unsigned j = 0; j != 4; ++j)
        x = multiply_low(x, lo32(sub(constant(2), multiply_low(n, x))));
    return lo32(sub(constant(0), x));
}
// Exact column aggregation. At most four uint32 low/high halves are summed;
// each vector lane sum is below2^35, so uint64 additions cannot overflow.
// Carry propagation happens once per dependency group, preserving modulo2^96.
PGN_NARROW_AVX2 void add_column_sum(V &l,V &m,V &h,V low_sum,V high_sum){
 V s=add(l,low_sum);l=lo32(s);s=add(add(m,high_sum),hi32(s));m=lo32(s);h=lo32(add(h,hi32(s)));
}
PGN_NARROW_AVX2 Value square_raw(Value a,V n0,V n1,V np,V high){
 const V xx=mul(a.x,a.x),zero=constant(0);V l=lo32(xx),m=hi32(xx),h=zero;
 const V q0=multiply_low(l,np);add_product(l,m,h,q0,n0);l=m;m=h;h=zero;
 const V q0n1=mul(q0,n1),xy=mul(a.x,a.y);
 add_column_sum(l,m,h,add(lo32(q0n1),add(lo32(xy),lo32(xy))),add(hi32(q0n1),add(hi32(xy),hi32(xy))));
 const V q1=multiply_low(l,np);add_product(l,m,h,q1,n0);l=m;m=h;h=zero;
 const V q0h=mul(q0,high),xz=mul(a.x,lo32(add(a.z,a.z))),q1n1=mul(q1,n1),yy=mul(a.y,a.y);
 add_column_sum(l,m,h,add(add(lo32(q0h),lo32(xz)),add(lo32(q1n1),lo32(yy))),add(add(hi32(q0h),hi32(xz)),add(hi32(q1n1),hi32(yy))));
 Value t{l,zero,zero};l=m;m=h;h=zero;
 const V q1h=mul(q1,high),yz=mul(a.y,lo32(add(a.z,a.z)));
 add_column_sum(l,m,h,add(lo32(q1h),lo32(yz)),add(hi32(q1h),hi32(yz)));
 t.y=l;t.z=lo32(add(mul(a.z,a.z),m));return t;
}
PGN_NARROW_AVX2 Value fold(Value t, V n0, V n1, V high, V mu) {
    const V q = hi32(mul(t.z, mu));
    const V p0 = mul(q, n0), p1 = add(mul(q, n1), hi32(p0));
    const V p2 = lo32(add(mul(q, high), hi32(p1)));
    V s = sub(t.x, lo32(p0));
    Value d{lo32(s), constant(0), constant(0)};
    s = sub(sub(t.y, lo32(p1)), _mm256_srli_epi64(s, 63));
    d.y = lo32(s);
    d.z = lo32(sub(sub(t.z, p2), _mm256_srli_epi64(s, 63)));
    return d;
}
PGN_NARROW_AVX2 Value shifted(Value a, V bits) {
    // Same source precondition: every lane has bits<32 and shifted value<2^96.
    const V x = _mm256_sllv_epi64(a.x, bits);
    const V y = _mm256_or_si256(_mm256_sllv_epi64(a.y, bits), hi32(x));
    const V z = _mm256_or_si256(_mm256_sllv_epi64(a.z, bits), hi32(y));
    return {lo32(x), lo32(y), lo32(z)};
}
PGN_NARROW_AVX2 Value shift_fold(Value a, V bits, V n0, V n1, V high, V mu) {
    return fold(shifted(a, bits), n0, n1, high, mu);
}
PGN_NARROW_AVX2 Value square_fold(Value a, V n0, V n1, V np, V high, V mu) {
    return fold(square_raw(a, n0, n1, np, high), n0, n1, high, mu);
}
PGN_NARROW_AVX2 V equal(Value a, Value b) {
    return _mm256_and_si256(_mm256_and_si256(_mm256_cmpeq_epi64(a.x, b.x),
        _mm256_cmpeq_epi64(a.y, b.y)), _mm256_cmpeq_epi64(a.z, b.z));
}
PGN_NARROW_AVX2 Value select(Value no, Value yes, V mask) {
    return {_mm256_blendv_epi8(no.x, yes.x, mask), _mm256_blendv_epi8(no.y, yes.y, mask),
            _mm256_blendv_epi8(no.z, yes.z, mask)};
}

PGN_NARROW_AVX2 unsigned test_four_admitted(const uint64_t low[4], uint32_t high,
                                            Cache &state) {
    uint64_t mu_words[4];
    for (unsigned j = 0; j != 4; ++j) {
        const uint32_t bucket = uint32_t(low[j] >> 36), tag = (high << 28) | bucket;
        auto &cache = state.lane[j];
        if (!cache.muLower || cache.bucketTag != tag) {
            const uint64_t last = (uint64_t(bucket) << 36) | ((uint64_t(1) << 36) - 1);
            cache.muLower = optimized_narrow_euler::build_mu(last, high);
            cache.bucketTag = tag;
        }
        mu_words[j] = cache.muLower;
    }
    const V lows = lanes(low), n0 = lo32(lows), n1 = hi32(lows), h = constant(high);
    const V mu = lanes(mu_words), np = negative_inverse32(n0);
    Value a{constant(0), constant(0), constant(1)};
    const uint32_t prefix = high * 16, chunks = prefix / 26, tail = prefix % 26;
    const V nibble = _mm256_srli_epi64(lows, 60), total = add(constant(tail), nibble);
    for (unsigned j = 0; j != chunks; ++j)
        a = shift_fold(a, constant(26), n0, n1, h, mu);
    const V split = _mm256_cmpgt_epi64(total, constant(26));
    const V first_bits = _mm256_blendv_epi8(total, constant(tail), split);
    a = shift_fold(a, first_bits, n0, n1, h, mu);
    if (_mm256_movemask_pd(_mm256_castsi256_pd(split))) {
        // Unsplitted lanes retain their exact source representative; the extra
        // fold is selected ONLY for lanes whose original branch requires it.
        const V second_bits = _mm256_and_si256(nibble, split);
        a = select(a, shift_fold(a, second_bits, n0, n1, h, mu), split);
    }
    V stream = _mm256_or_si256(_mm256_slli_epi64(
        _mm256_and_si256(lows, constant(~uint64_t(15))), 4), constant(128));
    for (unsigned group = 0; group != 14; ++group) {
        a = square_raw(a, n0, n1, np, h);
        a = square_fold(a, n0, n1, np, h, mu);
        a = square_raw(a, n0, n1, np, h);
        a = square_raw(a, n0, n1, np, h);
        a = shift_fold(a, _mm256_srli_epi64(stream, 60), n0, n1, h, mu);
        stream = _mm256_slli_epi64(stream, 4);
    }
    a = square_raw(a, n0, n1, np, h);
    a = square_fold(a, n0, n1, np, h, mu);
    a = square_raw(a, n0, n1, np, h);
    a = shift_fold(a, _mm256_and_si256(_mm256_srli_epi64(lows, 1), constant(7)), n0, n1, h, mu);
    const V negative = _mm256_cmpeq_epi64(_mm256_and_si256(_mm256_xor_si256(
        _mm256_srli_epi64(lows, 1), _mm256_srli_epi64(lows, 2)), constant(1)), constant(1));
    const V doubled = _mm256_slli_epi64(lows, 1);
    const Value neg1{n0, n1, sub(h, constant(1))};
    const Value neg2{lo32(doubled), hi32(doubled),
        add(sub(add(h, h), constant(1)), _mm256_srli_epi64(lows, 63))};
    const V neg_ok = _mm256_or_si256(equal(a, neg1), equal(a, neg2));
    const V pos_ok = _mm256_or_si256(equal(a, Value{constant(0), constant(0), constant(1)}),
                                    equal(a, Value{n0, n1, add(h, constant(1))}));
    const V verdict = _mm256_blendv_epi8(pos_ok, neg_ok, negative);
    return unsigned(_mm256_movemask_pd(_mm256_castsi256_pd(verdict)));
}


struct CacheEight { std::array<optimized_narrow_euler::State,8> lane{}; };
// Two independent four-lane streams interleaved at each dependency boundary.
PGN_NARROW_AVX2 unsigned test_eight_admitted(const uint64_t low[8],uint32_t high,CacheEight &state){
 uint64_t mus[8];for(unsigned j=0;j<8;j++){const uint32_t bucket=uint32_t(low[j]>>36),tag=(high<<28)|bucket;auto &c=state.lane[j];if(!c.muLower||c.bucketTag!=tag){c.muLower=optimized_narrow_euler::build_mu((uint64_t(bucket)<<36)|((uint64_t(1)<<36)-1),high);c.bucketTag=tag;}mus[j]=c.muLower;}
 V lows[2]={lanes(low),lanes(low+4)},n0[2],n1[2],np[2],mu[2],stream[2];Value a[2];const V h=constant(high);const uint32_t prefix=high*16,chunks=prefix/26,tail=prefix%26;
 for(unsigned j=0;j<2;j++){n0[j]=lo32(lows[j]);n1[j]=hi32(lows[j]);np[j]=negative_inverse32(n0[j]);mu[j]=lanes(mus+4*j);a[j]={constant(0),constant(0),constant(1)};const V nibble=_mm256_srli_epi64(lows[j],60),total=add(constant(tail),nibble);for(unsigned c=0;c<chunks;c++)a[j]=shift_fold(a[j],constant(26),n0[j],n1[j],h,mu[j]);const V split=_mm256_cmpgt_epi64(total,constant(26));a[j]=shift_fold(a[j],_mm256_blendv_epi8(total,constant(tail),split),n0[j],n1[j],h,mu[j]);if(_mm256_movemask_pd(_mm256_castsi256_pd(split)))a[j]=select(a[j],shift_fold(a[j],_mm256_and_si256(nibble,split),n0[j],n1[j],h,mu[j]),split);stream[j]=_mm256_or_si256(_mm256_slli_epi64(_mm256_and_si256(lows[j],constant(~uint64_t(15))),4),constant(128));}
 for(unsigned group=0;group<14;group++){
  for(unsigned j=0;j<2;j++)a[j]=square_raw(a[j],n0[j],n1[j],np[j],h);
  for(unsigned j=0;j<2;j++)a[j]=square_fold(a[j],n0[j],n1[j],np[j],h,mu[j]);
  for(unsigned j=0;j<2;j++)a[j]=square_raw(a[j],n0[j],n1[j],np[j],h);
  for(unsigned j=0;j<2;j++)a[j]=square_raw(a[j],n0[j],n1[j],np[j],h);
  for(unsigned j=0;j<2;j++){a[j]=shift_fold(a[j],_mm256_srli_epi64(stream[j],60),n0[j],n1[j],h,mu[j]);stream[j]=_mm256_slli_epi64(stream[j],4);}
 }
 for(unsigned j=0;j<2;j++)a[j]=square_raw(a[j],n0[j],n1[j],np[j],h);
 for(unsigned j=0;j<2;j++)a[j]=square_fold(a[j],n0[j],n1[j],np[j],h,mu[j]);
 for(unsigned j=0;j<2;j++)a[j]=square_raw(a[j],n0[j],n1[j],np[j],h);
 unsigned result=0;
 for(unsigned j=0;j<2;j++){a[j]=shift_fold(a[j],_mm256_and_si256(_mm256_srli_epi64(lows[j],1),constant(7)),n0[j],n1[j],h,mu[j]);const V negative=_mm256_cmpeq_epi64(_mm256_and_si256(_mm256_xor_si256(_mm256_srli_epi64(lows[j],1),_mm256_srli_epi64(lows[j],2)),constant(1)),constant(1));const V doubled=_mm256_slli_epi64(lows[j],1);const Value neg1{n0[j],n1[j],sub(h,constant(1))},neg2{lo32(doubled),hi32(doubled),add(sub(add(h,h),constant(1)),_mm256_srli_epi64(lows[j],63))};const V neg_ok=_mm256_or_si256(equal(a[j],neg1),equal(a[j],neg2)),pos_ok=_mm256_or_si256(equal(a[j],Value{constant(0),constant(0),constant(1)}),equal(a[j],Value{n0[j],n1[j],add(h,constant(1))}));result|=unsigned(_mm256_movemask_pd(_mm256_castsi256_pd(_mm256_blendv_epi8(pos_ok,neg_ok,negative))))<<(4*j);}
 return result;
}

// Baseline-ISA admission function. Call only after pinning to the worker CPU;
// maintain that affinity for the worker lifetime on heterogeneous machines.
// A constructor-cached CPU feature mask is not a per-current-core observation.
inline bool avx2_on_current_cpu() {
    unsigned a, b, c, d;
    if (__get_cpuid_max(0, nullptr) < 7) return false;
    __cpuid_count(1, 0, a, b, c, d);
    constexpr unsigned required = (1u << 26) | (1u << 27) | (1u << 28);
    if ((c & required) != required) return false; // XSAVE, OSXSAVE, AVX.
    unsigned xcr_lo, xcr_hi;
    __asm__ volatile ("xgetbv" : "=a"(xcr_lo), "=d"(xcr_hi) : "c"(0));
    if ((xcr_lo & 6) != 6) return false; // OS saved SSE and YMM state.
    __cpuid_count(7, 0, a, b, c, d);
    return (b & (1u << 5)) != 0;
}
inline bool evaluate_four(const uint64_t low[4], uint32_t high, Cache &state,
                          unsigned &positive_mask) {
    positive_mask = 0;
    if (!optimized_narrow_euler::admits_high(high)) return false;
    for (unsigned j = 0; j != 4; ++j) if (!(low[j] & 1)) return false;
    if (!avx2_on_current_cpu()) return false;
    positive_mask = test_four_admitted(low, high, state);
    return true;
}
} // namespace pgn_promoted_avx2
#undef PGN_NARROW_AVX2
#endif

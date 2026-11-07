// Private, bounded prime/order stream. No engine or consensus activation.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

namespace pgn_psp_tail {
inline void require(bool b, const char* why) {
    if (!b) throw std::invalid_argument(why);
}
struct Product64 { uint64_t low, high; };
inline Product64 multiply(uint64_t a, uint64_t b) {
#if defined(_MSC_VER) && defined(_M_X64)
    uint64_t high; const auto low = _umul128(a, b, &high); return {low, high};
#elif defined(__SIZEOF_INT128__)
    const auto n = static_cast<unsigned __int128>(a)*b;
    return {uint64_t(n), uint64_t(n >> 64)};
#else
#error Exact 64x64 multiplication is required
#endif
}
inline uint64_t remainder(Product64 a, uint64_t m) {
#if defined(_MSC_VER) && defined(_M_X64)
    uint64_t r; _udiv128(a.high % m, a.low, m, &r); return r;
#else
    return uint64_t(((static_cast<unsigned __int128>(a.high)<<64)|a.low)%m);
#endif
}

// Modulus < 2^63 keeps the Montgomery high-limb addition below 2^64.
// Only setup performs wide division; the exponentiation loop does not.
class Base2Power {
    uint64_t modulus_, negative_inverse_, one_, two_;
    uint64_t reduce(Product64 t) const {
        const auto m = t.low * negative_inverse_;
        const auto v = multiply(m, modulus_);
        const auto low = t.low + v.low;
        const auto u = t.high + v.high + (low < t.low);
        return u >= modulus_ ? u-modulus_ : u;
    }
    uint64_t product(uint64_t a, uint64_t b) const { return reduce(multiply(a,b)); }
public:
    explicit Base2Power(uint64_t modulus): modulus_(modulus) {
        require(modulus > 1 && (modulus&1) && modulus < (uint64_t(1)<<63),
                "Montgomery modulus must be odd and below2^63");
        uint64_t inverse = 1;
        for (unsigned i=0; i<6; ++i) inverse *= 2-modulus*inverse;
        negative_inverse_ = 0-inverse;
        one_ = remainder({0,1}, modulus_);
        two_ = (one_*2)%modulus_;
    }
    uint64_t power(uint64_t exponent) const {
        uint64_t result=one_, base=two_;
        while (exponent) {
            if (exponent&1) result=product(result,base);
            exponent >>= 1;
            if (exponent) base=product(base,base);
        }
        return reduce({result,0});
    }
    bool is_one(uint64_t exponent) const { return power(exponent)==1; }
};

inline uint64_t integer_sqrt(uint64_t n) {
    auto r=uint64_t(std::sqrt(static_cast<long double>(n)));
    while (r && r > n/r) --r;
    while (r+1 <= n/(r+1)) ++r;
    return r;
}
inline std::vector<uint32_t> base_primes(uint32_t maximum) {
    std::vector<uint8_t> composite(size_t(maximum)+1);
    std::vector<uint32_t> out;
    for (uint32_t p=2; p<=maximum; ++p) {
        if (composite[p]) continue;
        out.push_back(p);
        if (uint64_t(p)*p<=maximum)
            for (uint32_t n=p*p; n<=maximum; n+=p) composite[n]=1;
    }
    return out;
}
struct PrimeOrder { uint64_t prime, order; };
struct StreamStats {
    uint64_t begin{},end{},rows{},segments{},peak_array_bytes{},base_primes{};
};

// An internal capability, not a serialized proof or a security boundary.
// Only the exhaustive prime/order constructor below can create a new value.
class CertifiedPrimeOrder {
    PrimeOrder value_;
    explicit CertifiedPrimeOrder(PrimeOrder value):value_(value){}
    template<class Visit>
    friend StreamStats stream_certified(uint64_t,uint64_t,uint32_t,Visit);
public:
    PrimeOrder value()const{return value_;}
};

// Exhaustive primes in [begin,end), with exact minimal ord_p(2). The p-1
// factorization uses the same segmented coordinates, not a growing SPF bank.
// Memory is at most17*segment_span + base-prime buffers, independent of end.
// Raising MaximumPrimeEnd is a separate qualification, not an implicit95bit claim.
inline constexpr uint64_t MaximumPrimeEnd=uint64_t(1)<<35;
inline constexpr uint32_t MaximumSegmentSpan=uint32_t(1)<<20;
namespace detail {
template<bool RecheckOrder,class Visit>
StreamStats stream_impl(uint64_t begin, uint64_t end, uint32_t segment_span, Visit visit) {
    require(begin>=3 && begin<end && end<=MaximumPrimeEnd,
            "Prime/order stream outside bounded factor domain");
    require(segment_span && segment_span<=MaximumSegmentSpan,
            "Prime/order segment memory bound exceeded");
    const auto primes=base_primes(uint32_t(integer_sqrt(end-1)));
    StreamStats stats{begin,end,0,0,0,uint64_t(primes.size())};
    for (uint64_t lo=begin; lo<end;) {
        const auto hi=lo+std::min<uint64_t>(end-lo,segment_span);
        const auto length=size_t(hi-lo);
        std::vector<uint8_t> composite(length);
        for (const auto r:primes) {
            if (uint64_t(r)*r>=hi) break;
            const auto first=std::max<uint64_t>(uint64_t(r)*r,lo+(r-lo%r)%r);
            for (auto p=first; p<hi; p+=r) composite[size_t(p-lo)]=1;
        }
        std::vector<uint64_t> remaining(length),orders(length);
        for (size_t i=0; i<length; ++i)
            if (!composite[i]) remaining[i]=orders[i]=lo+i-1;
        for (const auto r:primes) {
            if (uint64_t(r)*r>hi-2) break;
            auto first=lo+(uint64_t(r)+1-lo%r)%r;
            for (auto p=first; p<hi; p+=r) {
                const auto i=size_t(p-lo);
                if (composite[i] || remaining[i]%r) continue;
                while (remaining[i]%r==0) remaining[i]/=r;
                const Base2Power power(p);
                while (orders[i]%r==0 && power.is_one(orders[i]/r)) orders[i]/=r;
            }
        }
        for (size_t i=0; i<length; ++i) if (!composite[i]) {
            const uint64_t p=lo+i;
            const Base2Power power(p);
            const auto r=remaining[i];
            if (r>1)
                while (orders[i]%r==0 && power.is_one(orders[i]/r)) orders[i]/=r;
            require(orders[i],"Invalid prime/order stream row");
            if constexpr(RecheckOrder)
                require(power.is_one(orders[i]),"Invalid prime/order stream row");
            visit(PrimeOrder{p,orders[i]}); ++stats.rows;
        }
        ++stats.segments;
        stats.peak_array_bytes=std::max(stats.peak_array_bytes,uint64_t(length)*17);
        lo=hi;
    }
    return stats;
}
} // namespace detail

template<class Visit>
StreamStats stream(uint64_t begin,uint64_t end,uint32_t segment_span,Visit visit) {
    return detail::stream_impl<true>(begin,end,segment_span,visit);
}

template<class Visit>
StreamStats stream_certified(uint64_t begin,uint64_t end,uint32_t segment_span,Visit visit) {
    // For each sieve-proven prime, ord_p(2) divides initial p-1. Every
    // accepted division tests 2^(d/r)==1, preserving ord_p(2)|d. Factoring
    // all of p-1 and removing every permitted prime factor makes d minimal.
    // No additional 2^d check is needed at this trusted construction edge.
    return detail::stream_impl<false>(begin,end,segment_span,[&](PrimeOrder row){visit(CertifiedPrimeOrder(row));});
}
} // namespace pgn_psp_tail

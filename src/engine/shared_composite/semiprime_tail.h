// Private scoped cofactor filter. Produces proven composites, not prime claims.
#pragma once
#include "segmented_prime_orders.h"
#include <limits>
#include <span>
namespace pgn_psp_tail {
using Number=Product64;
inline bool less(Number a,Number b) { return a.high<b.high || (a.high==b.high && a.low<b.low); }
inline bool equal(Number a,Number b) { return a.low==b.low && a.high==b.high; }
inline Number predecessor(Number a) { require(a.low||a.high,"zero predecessor"); return {a.low-1,a.high-(a.low==0)}; }
inline uint64_t quotient(Number a,uint64_t p,uint64_t& rem) {
    require(p && a.high<p,"Cofactor quotient exceeds64bits");
#if defined(_MSC_VER) && defined(_M_X64)
    return _udiv128(a.high,a.low,p,&rem);
#else
    const auto value=(static_cast<unsigned __int128>(a.high)<<64)|a.low;
    rem=uint64_t(value%p); return uint64_t(value/p);
#endif
}
class Scope {
    uint64_t cutoff_;
    Number begin_,end_;
public:
    Scope(uint64_t cutoff,Number begin,Number end):cutoff_(cutoff),begin_(begin),end_(end) {
        require(cutoff>=2 && cutoff<=10000000,"Unqualified tail cutoff");
        const auto cube=multiply(cutoff*cutoff,cutoff);
        require(!less(begin,{3,0}) && less(begin,end) && !less(cube,end),
                "Tail completeness requires3<=begin<end<=cutoff^3");
    }
    uint64_t cutoff()const{return cutoff_;}
    Number begin()const{return begin_;}
    Number end()const{return end_;}
};
struct CompositeWitness { Number n;uint64_t factor,cofactor; };
struct RowStats { uint64_t odd_progression_events{},cofactor_passes{}; };

inline void validate_row_shape(uint64_t cutoff,PrimeOrder row,uint64_t max_events) {
    require(row.prime>cutoff && row.prime<MaximumPrimeEnd && (row.prime&1) && row.order && (row.prime-1)%row.order==0,
            "Invalid scoped prime/order row");
    require(max_events && max_events<=1000000,"Tail row work budget exceeded");
}
inline void validate_row(uint64_t cutoff,PrimeOrder row,uint64_t max_events) {
    validate_row_shape(cutoff,row,max_events);
    require(Base2Power(row.prime).is_one(row.order),"Invalid row exponent");
}

// PRECONDITION: row comes from the exact prime/order stream. In scoped residual
// PSPs the other cofactor is prime, making the short test necessary. An emitted
// q need not itself be prime: p*q with p,q>1 is always a safe composite mark.
// Full EJ classification or a bounded consumer remains a separate operation.
// Calling this on only selected p rows DOES NOT establish full tail coverage.
namespace detail {
inline void validate_row(PrimeOrder row) {
    pgn_psp_tail::validate_row(2,row,1000000);
}
template<class Emit>
RowStats visit_validated_row(const Scope& scope,PrimeOrder row,Emit emit,uint64_t max_events) {
    const auto p=row.prime,d=row.order;
    RowStats stats;
    if(!less(multiply(p,p),scope.end()))return stats;
    uint64_t rem;
    auto qmin=quotient(scope.begin(),p,rem);
    require(qmin<std::numeric_limits<uint64_t>::max(),"Cofactor ceil overflow");
    qmin=std::max(p,qmin+(rem!=0));
    const auto qmax=quotient(predecessor(scope.end()),p,rem);
    require(qmax<(uint64_t(1)<<63),"Cofactor outside Montgomery64 scope");
    if(qmin>qmax)return stats;
    const auto first=std::max<uint64_t>(1,(qmin-1)/d+((qmin-1)%d!=0));
    const auto last=(qmax-1)/d;
    if(first>last)return stats;
    for(auto k=first;;++k){
        const auto q=1+k*d;
        if(q&1){
            require(stats.odd_progression_events<max_events,"Tail row event budget reached; coverage incomplete");
            ++stats.odd_progression_events;
            if(Base2Power(q).is_one(p-1)){
                const auto n=multiply(p,q);
                require(!less(n,scope.begin()) && less(n,scope.end()) && q>=p,"Tail ownership failure");
                emit(CompositeWitness{n,p,q});++stats.cofactor_passes;
            }
        }
        if(k==last)break;
    }
    return stats;
}
} // namespace detail

template<class Emit>
RowStats visit_row(const Scope& scope,PrimeOrder row,Emit emit,uint64_t max_events=1000000) {
    require(row.prime>scope.cutoff(),"Invalid scoped prime/order row");
    detail::validate_row(row);
    require(max_events && max_events<=1000000,"Tail row work budget exceeded");
    return detail::visit_validated_row(scope,row,emit,max_events);
}
template<class Emit>
RowStats visit_row(const Scope& scope,CertifiedPrimeOrder certified,Emit emit,uint64_t max_events=1000000) {
    const auto row=certified.value();
    validate_row_shape(scope.cutoff(),row,max_events);
    return detail::visit_validated_row(scope,row,emit,max_events);
}

// One shared factor stream for dispersed ticket intervals. Validate the whole
// bounded scope vector before emitting anything. Coordinates and output indices
// stay in caller order; gaps between scopes are not silently mined or credited.
// The total row event cap applies across all scopes, never once per scope.
namespace detail {
inline void validate_many_scopes(std::span<const Scope> scopes,uint64_t max_events) {
    require(!scopes.empty() && scopes.size()<=64,"Bounded nonempty tail scope vector required");
    require(max_events && max_events<=1000000,"Tail row work budget exceeded");
    for(size_t i=1;i<scopes.size();++i)
        require(!less(scopes[i].begin(),scopes[i-1].end()),"Tail scopes must be sorted and disjoint");
}
template<class Emit>
RowStats visit_many_validated(std::span<const Scope> scopes,PrimeOrder row,Emit emit,uint64_t max_events) {
    RowStats total;
    for(size_t i=0;i<scopes.size();++i) {
        if(row.prime<=scopes[i].cutoff())continue;
        const auto stats=detail::visit_validated_row(scopes[i],row,[&](CompositeWitness w){emit(i,w);},max_events-total.odd_progression_events);
        total.odd_progression_events+=stats.odd_progression_events;
        total.cofactor_passes+=stats.cofactor_passes;
    }
    return total;
}
} // namespace detail
template<class Emit>
RowStats visit_many(std::span<const Scope> scopes,PrimeOrder row,Emit emit,uint64_t max_events=1000000) {
    detail::validate_many_scopes(scopes,max_events);
    detail::validate_row(row);
    return detail::visit_many_validated(scopes,row,emit,max_events);
}
template<class Emit>
RowStats visit_many(std::span<const Scope> scopes,CertifiedPrimeOrder certified,Emit emit,uint64_t max_events=1000000) {
    detail::validate_many_scopes(scopes,max_events);
    const auto row=certified.value();validate_row_shape(2,row,max_events);
    return detail::visit_many_validated(scopes,row,emit,max_events);
}
} // namespace pgn_psp_tail

// Private producer primitive. One complete factor stream, bounded disjoint jobs.
#pragma once
#include "semiprime_tail.h"
#include <array>
#include <span>
#include <vector>

namespace pgn_psp_tail {
struct ScopedWitness { uint32_t owner; CompositeWitness value; };
struct BatchRowStats { uint64_t odd_progression_events{},cofactor_passes{}; };

class DisjointScopes;
class BatchWorkspace {
    friend class DisjointScopes;
    std::vector<ScopedWitness> staged_;
    size_t capacity_;
public:
    explicit BatchWorkspace(size_t maximum_witnesses=4096):capacity_(maximum_witnesses) {
        require(capacity_ && capacity_<=65536,"Invalid batch witness budget");
        staged_.reserve(capacity_); // Once per resident producer, never per row.
    }
    size_t allocated_capacity()const{return staged_.capacity();}
};

class DisjointScopes {
    struct Item { uint32_t owner;Scope scope; };
    std::vector<Item> items_;
    uint64_t cutoff_{},positions_{};
    template<class Emit>
    BatchRowStats visit_validated(PrimeOrder row,BatchWorkspace& workspace,Emit emit,uint64_t max_events)const {
        workspace.staged_.clear();BatchRowStats total;
        for(const auto& item:items_){
            const auto stats=detail::visit_validated_row(item.scope,row,[&](CompositeWitness w){
                require(workspace.staged_.size()<workspace.capacity_,"Batch witness budget reached; row unpublished");
                workspace.staged_.push_back(ScopedWitness{item.owner,w});
            },max_events-total.odd_progression_events);
            total.odd_progression_events+=stats.odd_progression_events;
            total.cofactor_passes+=stats.cofactor_passes;
        }
        for(const auto& witness:workspace.staged_)emit(witness);
        return total;
    }
public:
    explicit DisjointScopes(std::span<const Scope> scopes) {
        require(!scopes.empty() && scopes.size()<=64,"Expected1..64 disjoint scopes");
        cutoff_=scopes.front().cutoff();items_.reserve(scopes.size());
        for(size_t i=0;i<scopes.size();++i){
            const auto& s=scopes[i];require(s.cutoff()==cutoff_,"Batch cutoff mismatch");
            const auto a=s.begin(),b=s.end();
            const Number width{b.low-a.low,b.high-a.high-(b.low<a.low)};
            require(!width.high && width.low<=1000000000000000ULL-positions_,"Batch positions exceed1e15");
            positions_+=width.low;items_.push_back(Item{uint32_t(i),s});
        }
        std::sort(items_.begin(),items_.end(),[](const Item& a,const Item& b){return less(a.scope.begin(),b.scope.begin());});
        for(size_t i=1;i<items_.size();++i)
            require(!less(items_[i].scope.begin(),items_[i-1].scope.end()),"Batch scopes overlap");
    }
    uint64_t positions()const{return positions_;}
    size_t scopes()const{return items_.size();}

    // Completeness still requires every applicable factor row. On validation,
    // event or witness-budget failure this row emits nothing. The caller must
    // publish a whole stream only after its own complete-coverage receipt.
    template<class Emit>
    BatchRowStats visit(PrimeOrder row,BatchWorkspace& workspace,Emit emit,uint64_t max_events=1000000)const {
        validate_row(cutoff_,row,max_events);
        return visit_validated(row,workspace,emit,max_events);
    }
    template<class Emit>
    BatchRowStats visit(CertifiedPrimeOrder certified,BatchWorkspace& workspace,Emit emit,uint64_t max_events=1000000)const {
        const auto row=certified.value();
        validate_row_shape(cutoff_,row,max_events);
        return visit_validated(row,workspace,emit,max_events);
    }
};
} // namespace pgn_psp_tail

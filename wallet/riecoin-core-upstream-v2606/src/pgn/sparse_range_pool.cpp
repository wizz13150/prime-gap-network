#include "sparse_range_pool.h"
#include <stdexcept>
#include <type_traits>

namespace pgn::core {
SparseRangePool::SparseRangePool(Integer origin,Integer width,Integer slots,
    uint32_t selection_window,std::optional<Integer> limit)
    :m_origin(std::move(origin)),m_width(std::move(width)),m_slots(std::move(slots)),
     m_limit(limit.value_or(m_origin+m_width*m_slots)),m_selection_window(selection_window){
    if(m_origin<0||m_width<=0||m_slots<=0)throw std::invalid_argument("Explicit nonnegative origin, positive range width and slot count required");
    if(m_limit<=m_origin+m_width*(m_slots-1)||m_limit>m_origin+m_width*m_slots)
        throw std::invalid_argument("The exact range limit must clip only the last slot");
}
bool SparseRangePool::Occupy(const Integer& slot){
    return slot>=0&&slot<m_slots&&m_occupied.insert(slot).second;
}
bool SparseRangePool::Release(const Integer& slot){
    return slot>=0&&slot<m_slots&&m_occupied.erase(slot)==1;
}
bool SparseRangePool::IsOccupied(const Integer& slot)const{return m_occupied.find(slot)!=m_occupied.end();}
std::optional<RangeSlotSelection> SparseRangePool::SelectFree(const Integer& rank)const{
    if(rank<0||rank>=SelectionCount())return {};
    // If k free slots precede the answer, at most K occupied slots do too:
    // k <= answer <= k+K, regardless of the bit width/cardinality of the domain.
    Integer lower=rank,upper=rank+m_occupied.size();size_t queries{};
    while(lower<upper){
        const Integer middle=(lower+upper)>>1;
        const Integer freeThrough=middle+1-m_occupied.upper_bound_rank(middle);++queries;
        if(freeThrough>rank)upper=middle;else lower=middle+1;
    }
    const Integer start=m_origin+lower*m_width;
    const Integer end=start+m_width;
    return RangeSlotSelection{lower,start,end<m_limit?end:m_limit,queries};
}
std::optional<RangeSlotSelection> SparseRangePool::TakeFree(const Integer& rank){
    static_assert(std::is_nothrow_move_constructible_v<RangeSlotSelection>);
    auto selected=SelectFree(rank);if(!selected)return {};
    if(!m_occupied.insert(selected->slot).second)throw std::logic_error("Sparse range pool requires serialized mutation");
    return selected;
}
}

// MIT. Exact allocator primitive; NOT a chain-validated availability registry.
#pragma once
#include "range_commitment.h"
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/ranked_index.hpp>
#include <optional>

namespace pgn::core {
struct RangeSlotSelection {
    Integer slot,start,end; // half-open [start,end)
    size_t rank_queries{}; // local diagnostic, never protocol serialization
};
// Caller-supplied pool specification/occupancy is deliberately NOT sealed as
// authoritative. Consensus-derived availability, allocation policy, expiry,
// completed reports, randomness quality and fork undo remain separate.
// No fixed integer width or dense allocation proportional to the slot domain.
class SparseRangePool {
    using Occupied=boost::multi_index_container<Integer,boost::multi_index::indexed_by<
        boost::multi_index::ranked_unique<boost::multi_index::identity<Integer>>>>;
    Integer m_origin,m_width,m_slots;
    Integer m_limit;
    uint32_t m_selection_window{};
    Occupied m_occupied;
public:
    SparseRangePool(Integer origin,Integer width,Integer slots,
                    uint32_t selection_window=0,std::optional<Integer> limit={});
    const Integer& Origin()const{return m_origin;}
    const Integer& Width()const{return m_width;}
    const Integer& Slots()const{return m_slots;}
    size_t OccupiedCount()const{return m_occupied.size();}
    Integer FreeCount()const{return m_slots-m_occupied.size();}
    // The rolling pool is the lowest eligible free ranks, not a dense array
    // or an ever-growing queue. Release reinserts an expired slot without
    // increasing this advertised selection count.
    Integer SelectionCount()const{
        const Integer free=FreeCount();
        return m_selection_window&&free>m_selection_window?Integer(m_selection_window):free;
    }
    bool Occupy(const Integer& slot);
    bool Release(const Integer& slot);
    bool IsOccupied(const Integer& slot)const;
    // Exact zero-based order statistic among free slots. O(log(K+1)) ranked
    // queries over K occupied integers, not a search across the full domain.
    // Boost provides the maintained balanced ranked tree; no custom tree.
    std::optional<RangeSlotSelection> SelectFree(const Integer& rank)const;
    // Serialized caller required (e.g. Core cs_main). Strong exception safety:
    // calculate/allocate result first, then maintained strong-guarantee insert.
    std::optional<RangeSlotSelection> TakeFree(const Integer& rank);
};
}

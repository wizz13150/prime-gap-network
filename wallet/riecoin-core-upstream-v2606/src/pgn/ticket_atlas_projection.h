// Copyright (c) 2026 The PGN developers. MIT.
// Read-only display arithmetic. Does not allocate tickets or prove coverage.
#pragma once
#include <pgn/range_commitment.h>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace pgn::core {
enum class AtlasStage : size_t {Assigned,Pending,Checking,Passed,Failed,Count};
inline constexpr std::array<const char*,5> AtlasStageNames{
    "assigned","pending_audits","audits_assigned","audits_passed","audit_failed"};
struct AtlasAmount {uint64_t tickets{};Integer positions{};};
struct AtlasBin {Integer start,end;std::array<AtlasAmount,5> stages;};
class TicketAtlasProjection {
    Integer m_start,m_end,m_span,m_previous_end{};
    bool m_previous{};
    std::vector<AtlasBin> m_bins;
    std::array<AtlasAmount,5> m_totals;
    uint64_t m_all{};
public:
    TicketAtlasProjection(Integer start,Integer end,size_t requested)
        :m_start(std::move(start)),m_end(std::move(end)),m_span(m_end-m_start){
        if(m_start<0||m_span<=0||requested<1||requested>512)
            throw std::invalid_argument("Invalid bounded atlas viewport");
        const size_t n=m_span<requested?m_span.convert_to<size_t>():requested;
        m_bins.reserve(n);
        // The overview renderer uses equal horizontal space per bit band.
        // Aggregate in that same space: a linear first cell across 68..95
        // bits would falsely place its occupied midpoint near 87 bits.
        const bool bit_scale=m_start>0&&boost::multiprecision::msb(m_end-1)>boost::multiprecision::msb(m_start)+1;
        const Integer unit=Integer(1)<<40;
        const auto coordinate=[&](const Integer& value)->Integer{
            const auto k=boost::multiprecision::msb(value);const Integer base=Integer(1)<<k;
            return Integer(k)*unit+(value-base)*unit/base;
        };
        const Integer lower=bit_scale?coordinate(m_start):Integer(0),upper=bit_scale?coordinate(m_end):Integer(0);
        Integer previous=m_start;
        for(size_t i=1;i<=n;++i){
            Integer boundary=m_end;
            if(i<n){
                if(bit_scale){
                    const Integer cut=lower+(upper-lower)*i/n;const unsigned k=(cut/unit).convert_to<unsigned>();
                    const Integer base=Integer(1)<<k;boundary=base+base*(cut%unit)/unit;
                }else boundary=m_start+m_span*i/n;
            }
            // Very small integer domains can quantize adjacent cuts to the
            // same integer. Merge them, preserving an exact covering partition.
            if(boundary>previous){m_bins.push_back({previous,boundary,{}});previous=boundary;}
        }
    }
    // Current schedule order is numeric slot order. Reject overlap/duplicates
    // rather than interpreting two assignments as twice-completed coverage.
    bool Add(const Integer& start,const Integer& end,AtlasStage stage){
        const auto s=static_cast<size_t>(stage);
        if(start<0||end<=start||s>=m_totals.size()||(m_previous&&start<m_previous_end))
            throw std::invalid_argument("Invalid ordered atlas assignment");
        m_previous=true;m_previous_end=end;++m_all;
        const Integer left=std::max(start,m_start),right=std::min(end,m_end);
        if(right<=left)return false;
        auto& total=m_totals[s];++total.tickets;total.positions+=right-left;
        auto first=std::upper_bound(m_bins.begin(),m_bins.end(),left,
            [](const Integer& value,const AtlasBin& bin){return value<bin.end;});
        for(auto it=first;it!=m_bins.end()&&it->start<right;++it){
            auto& bin=*it;const Integer a=std::max(left,bin.start),b=std::min(right,bin.end);
            if(a<b){++bin.stages[s].tickets;bin.stages[s].positions+=b-a;}
        }
        return true;
    }
    const std::vector<AtlasBin>& Bins()const{return m_bins;}
    const std::array<AtlasAmount,5>& Totals()const{return m_totals;}
    uint64_t AllAssignments()const{return m_all;}
};
}

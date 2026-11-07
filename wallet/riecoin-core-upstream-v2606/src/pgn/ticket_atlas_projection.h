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
        for(size_t i=0;i<n;++i)m_bins.push_back({m_start+m_span*i/n,m_start+m_span*(i+1)/n,{}});
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
        const size_t n=m_bins.size();
        // Bins are [floor(span*i/n), floor(span*(i+1)/n)). The usual
        // floor(offset*n/span) is wrong at some indivisible boundaries.
        const size_t first=(((left-m_start+1)*n-1)/m_span).convert_to<size_t>();
        const size_t last=(((right-m_start)*n-1)/m_span).convert_to<size_t>();
        for(size_t i=first;i<=last;++i){
            auto& bin=m_bins.at(i);const Integer a=std::max(left,bin.start),b=std::min(right,bin.end);
            if(a<b){++bin.stages[s].tickets;bin.stages[s].positions+=b-a;}
        }
        return true;
    }
    const std::vector<AtlasBin>& Bins()const{return m_bins;}
    const std::array<AtlasAmount,5>& Totals()const{return m_totals;}
    uint64_t AllAssignments()const{return m_all;}
};
}

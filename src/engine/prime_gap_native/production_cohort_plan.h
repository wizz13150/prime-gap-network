#pragma once
// Private bridge between the exact production range geometry and bounded pages.
// This does not claim that a page covers every pseudoprime or activate a worker.
#include "composite_page_composition.h"
#include "memory_plan.h"

namespace pgn_psp_native {
class ProductionCohortPlan {
public:
    static constexpr uint64_t MaxOwnedLength = 64000000000000ull;
    static constexpr uint64_t DescriptorsPerPage = MaximumCohort / DescriptorSpan;
    static constexpr uint64_t PagePositions = DescriptorsPerPage * DescriptorSpan;
    static constexpr uint64_t OwnersPerPage = DescriptorsPerPage * pgn_psp_tile::TilesPerDescriptor;
    struct Address { uint32_t page, local_owner; };
private:
    pgn_release_plan::Range range_;
    std::vector<Cohort> pages_;
    explicit ProductionCohortPlan(pgn_release_plan::Range range):range_(range) {
        const uint64_t covered=range.length+range.remainder;
        for(uint64_t offset=0;offset<covered;){
            const uint64_t length=std::min(PagePositions,covered-offset);
            const uint64_t low=range.base+offset;
            require(low>=range.base,"Page origin crossed the production low64 window");
            pages_.push_back(Cohort::from({low,range.high},length));
            offset+=length;
        }
    }
public:
    static ProductionCohortPlan from(uint64_t low,uint64_t high,uint64_t length,unsigned k=64){
        require(k==64,"This private bridge retains the admitted K64 profile");
        require(length&&length<=MaxOwnedLength,"Normal64T owned-job bound required");
        const auto range=pgn_release_plan::range(low,high,length,k);
        require(range.valid,range.reason);
        return ProductionCohortPlan(range);
    }
    const pgn_release_plan::Range& range()const{return range_;}
    const std::vector<Cohort>& pages()const{return pages_;}
    uint64_t owners()const{return range_.blocks*pgn_psp_tile::TilesPerDescriptor;}
    Address address(uint64_t owner)const{
        require(owner<owners(),"Physical bitmap owner escaped the job");
        const uint64_t page=owner/OwnersPerPage,local=owner%OwnersPerPage;
        require(page<pages_.size(),"Physical owner has no complement page");
        return {uint32_t(page),uint32_t(local)};
    }
    Address address(uint64_t descriptorLow,uint64_t descriptorHigh,
                    uint32_t logicalY,uint32_t tile)const{
        require(descriptorHigh==range_.high&&descriptorLow>=range_.base,
                "Descriptor origin is outside the active job window");
        const uint64_t displacement=descriptorLow-range_.base;
        require(displacement%DescriptorSpan==0&&tile<pgn_psp_tile::TilesPerDescriptor,
                "Descriptor/tile geometry mismatch");
        const uint64_t descriptor=displacement/DescriptorSpan+logicalY;
        require(descriptor<range_.blocks,"Descriptor halo escaped the active job");
        return address(descriptor*pgn_psp_tile::TilesPerDescriptor+tile);
    }
    void assert_page(size_t index,const CompositePage& page)const{
        require(index<pages_.size(),"Complement page index escaped");
        page.assert_context(pages_[index]);
    }
};
static_assert(ProductionCohortPlan::DescriptorsPerPage==993);
static_assert(ProductionCohortPlan::PagePositions<=MaximumCohort);
static_assert(ProductionCohortPlan::PagePositions%DescriptorSpan==0);
}

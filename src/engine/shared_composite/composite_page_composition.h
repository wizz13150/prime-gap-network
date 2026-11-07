#pragma once
// Private in-process construction, not an admission for arbitrary CSR packets.
#include "native_small_factor_sieve.h"
#include "composite_native_page.h"
#include <string>
namespace pgn_psp_native {
class Cohort {
    Wide start_;uint64_t length_;
    Cohort(Wide start,uint64_t length):start_(start),length_(length){}
public:
    static Cohort from(Wide start,uint64_t length){validate_cohort(start,length);return {start,length};}
    Wide start()const{return start_;}uint64_t length()const{return length_;}
    bool same(const Cohort& other)const{return start_.low==other.start_.low&&start_.high==other.start_.high&&length_==other.length_;}
};
class CompositePage {
    Cohort cohort_;Page data_;
    static void validate(const Cohort& cohort,const Page& data){
        const auto owners=((cohort.length()+DescriptorSpan-1)/DescriptorSpan)*pgn_psp_tile::TilesPerDescriptor;
        require(data.offsets.size()==owners+1&&!data.offsets.empty()&&data.offsets.front()==0
            &&data.slots.size()<=MaximumEvents&&data.offsets.back()==data.slots.size(),"Composite CSR envelope mismatch");
        constexpr uint32_t residues[8]={1,7,11,13,17,19,23,29};
        for(size_t owner=0;owner<owners;++owner){
            require(data.offsets[owner]<=data.offsets[owner+1],"Composite CSR offsets not monotone");
            for(uint32_t i=data.offsets[owner];i<data.offsets[owner+1];++i){
                const auto slot=data.slots[i];require(slot<pgn_psp_tile::TileSlots,"Composite CSR tile escaped");
                const uint64_t absoluteSlot=(owner%pgn_psp_tile::TilesPerDescriptor)*pgn_psp_tile::TileSlots+slot;
                const uint64_t offset=(owner/pgn_psp_tile::TilesPerDescriptor)*DescriptorSpan+(absoluteSlot/8)*30+residues[absoluteSlot%8];
                require(offset<cohort.length(),"Composite CSR last descriptor escaped");
            }
        }
    }
    CompositePage(Cohort cohort,Page data):cohort_(cohort),data_(std::move(data)){validate(cohort_,data_);}
public:
    // The only constructors are canonical generation, exact factor checking,
    // and composition of already validated immutable pages.
    static CompositePage dense(const SmallFactorTable& table,const Cohort& cohort){
        return {cohort,build_page(table,cohort.start(),cohort.length())};
    }
    static CompositePage factored(const std::string& path,const Cohort& cohort){
        const auto start=cohort.start();
        auto verified=pgn_composite_page::load(path,start.low,start.high,cohort.length());
        Page data;data.events=verified.slots.size();data.offsets=std::move(verified.offsets);data.slots=std::move(verified.slots);
        return {cohort,std::move(data)};
    }
    static CompositePage merge(const CompositePage& a,const CompositePage& b){
        require(a.cohort_.same(b.cohort_),"Composite pages belong to different physical cohorts");
        const uint64_t events=uint64_t(a.data_.slots.size())+b.data_.slots.size();
        require(events<=MaximumEvents,"Composed event budget exceeded; no truncation");
        Page data;data.events=events;data.offsets.reserve(a.data_.offsets.size());data.offsets.push_back(0);data.slots.reserve(size_t(events));
        for(size_t owner=0;owner+1<a.data_.offsets.size();++owner){
            for(const auto* input:{&a.data_,&b.data_})
                data.slots.insert(data.slots.end(),input->slots.begin()+input->offsets[owner],input->slots.begin()+input->offsets[owner+1]);
            data.offsets.push_back(uint32_t(data.slots.size()));
        }
        return {a.cohort_,std::move(data)};
    }
    void assert_context(const Cohort& expected)const{require(cohort_.same(expected),"Composite page is stale for current cohort");}
    const Cohort& cohort()const{return cohort_;}
    const std::vector<uint32_t>& offsets()const{return data_.offsets;}
    const std::vector<uint32_t>& slots()const{return data_.slots;}
    uint64_t events()const{return data_.slots.size();}
    constexpr bool complete_psp_coverage()const{return false;}
};
}

#pragma once
// Private alternative representation. No production engine admission changes.
#include "gpu_baseline.cuh"
namespace pgn_psp_native::gpu_bucket {
using gpu_baseline::checked;
__global__ void scatter(const Progression* rows,uint32_t size,Wide start,uint64_t length,
                        uint32_t owners,uint32_t capacity,uint32_t* counts,uint32_t* slots,
                        unsigned long long* total,uint32_t* bad){
    const auto thread=blockIdx.x*blockDim.x+threadIdx.x;
    const auto i=thread>>5,lane=thread&31;if(i>=size)return;
    const auto row=rows[i];uint64_t at=0;
    if(!lane){const auto rem=gpu_baseline::wide_remainder(start,row.step);at=(row.step+row.prime-rem)%row.step;}
    at=__shfl_sync(0xffffffffu,at,0)+uint64_t(lane)*row.step;
    const auto stride=row.step*32;uint32_t events=0;
    for(;at<length;){
        const auto bit=gpu_baseline::Classes[at%30];
        if(bit>=0){
            const auto slot=(at%DescriptorSpan)/30*8+unsigned(bit);
            const auto owner=at/DescriptorSpan*pgn_psp_tile::TilesPerDescriptor+(slot>>pgn_psp_tile::TileBits);
            if(owner>=owners){atomicOr(bad,2u);return;}
            const auto target=atomicAdd(counts+owner,1u);
            // Never truncate silently. An overflow invalidates the entire
            // bucket result; the host invokes the unchanged exact CSR builder.
            if(target<capacity)slots[uint64_t(owner)*capacity+target]=uint32_t(slot)&(pgn_psp_tile::TileSlots-1);
            else atomicOr(bad,1u);
            ++events;
        }
        if(stride>=length-at)break;at+=stride;
    }
    for(int d=16;d;d>>=1)events+=__shfl_down_sync(0xffffffffu,events,d);
    if(!lane&&events)atomicAdd(total,static_cast<unsigned long long>(events));
}
__global__ void append(uint32_t owners,const uint32_t* offsets,const uint32_t* source,
                       uint32_t capacity,uint32_t* counts,uint32_t* slots,uint32_t* bad){
    const auto owner=blockIdx.x*blockDim.x+threadIdx.x;if(owner>=owners)return;
    auto at=counts[owner];const auto begin=offsets[owner],end=offsets[owner+1];
    counts[owner]=at+end-begin;
    for(auto i=begin;i<end;++i,++at){
        if(at<capacity)slots[uint64_t(owner)*capacity+at]=source[i];
        else atomicOr(bad,1u);
    }
}
struct Result {
    const uint32_t* offsets;const uint32_t* slots;const uint32_t* counts;
    uint32_t owners,capacity;uint64_t events,bytes;float GPU_ms;bool fallback;
};
class Workspace {
    gpu_baseline::Scratch<Progression> rules_;
    gpu_baseline::Scratch<uint32_t> bad_,factOffsets_,factSlots_;
    gpu_baseline::Scratch<unsigned long long> total_;
    gpu_baseline::Workspace fallback_;
    bool initialized_{};int device_{};cudaEvent_t began_{},ended_{};
public:
    template<class Allocate>
    Result build(const SmallFactorTable& table,const Cohort& cohort,const CompositePage* extra,
                 Allocate allocate,uint32_t capacity=32){
        const auto start=cohort.start();const auto length=cohort.length();validate_cohort(start,length);
        require(capacity&&capacity<=32,"Private bucket capacity outside bounded envelope");
        require(table.ordinary()==80000&&table.limit()==10000000&&table.rows().size()==656742,"Canonical progression table required");
        const uint64_t extraEvents=extra?extra->events():0;
        if(extra){extra->assert_context(cohort);require(extraEvents<=65536,"Factored page budget exceeded");}
        int current{};checked(cudaGetDevice(&current));
        if(!initialized_){
            auto* rows=rules_.reserve(table.rows().size());checked(cudaMemcpy(rows,table.rows().data(),table.rows().size()*sizeof(Progression),cudaMemcpyHostToDevice));
            checked(cudaEventCreate(&began_));checked(cudaEventCreate(&ended_));device_=current;initialized_=true;
        }else require(current==device_,"Bucket workspace cannot change device");
        const auto owners=uint32_t((length+DescriptorSpan-1)/DescriptorSpan*pgn_psp_tile::TilesPerDescriptor);
        require(owners&&owners<=1048576,"Owner budget exceeded");
        // Counts belong to the returned page, just like slots. A reusable
        // workspace scratch array would corrupt earlier pages in a bank.
        auto* counts=static_cast<uint32_t*>(allocate(uint64_t(owners)*4));
        auto* bad=bad_.reserve(1);auto* total=total_.reserve(1);
        auto* slots=static_cast<uint32_t*>(allocate(uint64_t(owners)*capacity*4));
        auto* factOffsets=extra?factOffsets_.reserve(owners+1):nullptr;
        auto* factSlots=extra?factSlots_.reserve(size_t(std::max<uint64_t>(1,extraEvents))):nullptr;
        if(extra){checked(cudaMemcpy(factOffsets,extra->offsets().data(),(owners+1)*4,cudaMemcpyHostToDevice));if(extraEvents)checked(cudaMemcpy(factSlots,extra->slots().data(),extraEvents*4,cudaMemcpyHostToDevice));}
        checked(cudaMemset(counts,0,owners*4));checked(cudaMemset(bad,0,4));checked(cudaMemset(total,0,8));
        checked(cudaEventRecord(began_));
        scatter<<<uint32_t((table.rows().size()*32+255)/256),256>>>(rules_.reserve(table.rows().size()),uint32_t(table.rows().size()),start,length,owners,capacity,counts,slots,total,bad);
        if(extra)append<<<(owners+255)/256,256>>>(owners,factOffsets,factSlots,capacity,counts,slots,bad);
        checked(cudaGetLastError());checked(cudaEventRecord(ended_));checked(cudaEventSynchronize(ended_));
        unsigned long long events{};uint32_t failed{};float ms{};
        checked(cudaMemcpy(&events,total,8,cudaMemcpyDeviceToHost));checked(cudaMemcpy(&failed,bad,4,cudaMemcpyDeviceToHost));checked(cudaEventElapsedTime(&ms,began_,ended_));
        require(!(failed&~1u),"Bucket owner escaped; exact fallback cannot excuse invalid ownership");
        require(events<=MaximumEvents-extraEvents,"Event budget exceeded; no truncated page");events+=extraEvents;
        const auto bytes=(uint64_t(owners)*capacity+owners)*4;
        if(failed){
            const auto exact=fallback_.build(table,cohort,extra,allocate);
            require(exact.events==events,"Fallback event count differs");
            return {exact.offsets,exact.slots,nullptr,owners,0,events,bytes+exact.bytes,ms+exact.GPU_ms,true};
        }
        return {nullptr,slots,counts,owners,capacity,events,bytes,ms,false};
    }
};
// Same consumer semantics as CSR apply(): marks only certified composites in
// this owner. The caller must reject an overflow result before launching work.
__device__ __forceinline__ void apply(uint64_t* shared,const uint32_t* counts,
                                    const uint32_t* slots,uint32_t owner,uint32_t capacity){
    for(uint32_t i=threadIdx.x;i<counts[owner];i+=blockDim.x){
        const auto s=slots[uint64_t(owner)*capacity+i];
        atomicOr(reinterpret_cast<unsigned long long*>(shared+(s>>6)),1ull<<(s&63u));
    }
}
}

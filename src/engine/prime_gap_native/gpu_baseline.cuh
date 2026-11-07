#pragma once
// Same canonical composite progressions/CSR as build_page(), generated on GPU.
// Include BEFORE resident allocation macros. Workspace owns private reusable
// scratch; caller supplies output allocations under its normal resident owner.
#include <cuda_runtime.h>
#include <cub/device/device_scan.cuh>
#include "composite_page_composition.h"
#include <limits>
#include <string>

namespace pgn_psp_native::gpu_baseline {
inline void checked(cudaError_t code){if(code!=cudaSuccess)throw std::runtime_error(std::string("Composite GPU: ")+cudaGetErrorString(code));}
static __device__ __constant__ int Classes[30]={-1,0,-1,-1,-1,-1,-1,1,-1,-1,-1,2,-1,3,-1,-1,-1,4,-1,5,-1,-1,-1,6,-1,-1,-1,-1,-1,7};
__device__ inline uint64_t wide_remainder(Wide n,uint64_t divisor){
    uint64_t r=n.high<divisor?n.high:n.high%divisor;
    #pragma unroll
    for(int i=63;i>=0;--i){r=(r<<1)|((n.low>>i)&1);if(r>=divisor)r-=divisor;}
    return r;
}
template<bool Scatter>
__global__ void progressions(const Progression* rows,uint32_t size,Wide start,uint64_t length,
                            uint64_t* first,uint32_t owners,uint32_t* counts,const uint32_t* offsets,
                            uint32_t* cursor,uint32_t* slots,uint32_t capacity,
                            unsigned long long* total,uint32_t* bad){
    const auto thread=blockIdx.x*blockDim.x+threadIdx.x;
    const auto i=thread>>5,lane=thread&31;if(i>=size)return;
    const auto row=rows[i];uint64_t at=0;
    if(!lane){
        if constexpr(Scatter)at=first[i];
        else {const auto rem=wide_remainder(start,row.step);at=(row.step+row.prime-rem)%row.step;first[i]=at;}
    }
    at=__shfl_sync(0xffffffffu,at,0)+uint64_t(lane)*row.step;
    const auto stride=row.step*32;uint32_t events=0;
    for(;at<length;){
        const auto bit=Classes[at%30];
        if(bit>=0){
            const auto slot=(at%DescriptorSpan)/30*8+unsigned(bit);
            const auto owner=at/DescriptorSpan*pgn_psp_tile::TilesPerDescriptor+(slot>>pgn_psp_tile::TileBits);
            if(owner>=owners){atomicOr(bad,1u);return;}
            if constexpr(!Scatter){atomicAdd(counts+owner,1u);++events;}
            else {
                const auto target=atomicAdd(cursor+owner,1u);
                if(target>=capacity||target>=offsets[owner+1]){atomicOr(bad,2u);return;}
                slots[target]=uint32_t(slot)&(pgn_psp_tile::TileSlots-1);
            }
        }
        if(stride>=length-at)break;at+=stride;
    }
    if constexpr(!Scatter){
        for(int d=16;d;d>>=1)events+=__shfl_down_sync(0xffffffffu,events,d);
        if(!lane&&events)atomicAdd(total,static_cast<unsigned long long>(events));
    }
}
template<bool Scatter>
__global__ void factored(uint32_t owners,const uint32_t* sourceOffsets,const uint32_t* sourceSlots,
                         uint32_t* counts,const uint32_t* offsets,uint32_t* cursor,uint32_t* slots,
                         uint32_t capacity,uint32_t* bad){
    const auto owner=blockIdx.x*blockDim.x+threadIdx.x;if(owner>=owners)return;
    const auto begin=sourceOffsets[owner],end=sourceOffsets[owner+1];
    if constexpr(!Scatter)counts[owner]+=end-begin;
    else {
        auto target=cursor[owner];
        if(uint64_t(target)+end-begin>capacity||uint64_t(target)+end-begin>offsets[owner+1]){atomicOr(bad,4u);return;}
        for(auto i=begin;i<end;++i)slots[target++]=sourceSlots[i];cursor[owner]=target;
    }
}
__global__ void validate_cursor(uint32_t owners,const uint32_t* offsets,const uint32_t* cursor,uint32_t* bad){
    const auto i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<owners&&cursor[i]!=offsets[i+1])atomicOr(bad,8u);
}

template<class T> class Scratch {
    T* value_{};size_t capacity_{};
public:
    // No destructor calls CUDA after a failed context. The owned process
    // lifetime releases scratch; resizing occurs only on a healthy context.
    T* reserve(size_t count){
        if(count>capacity_){if(value_)checked(cudaFree(value_));value_=nullptr;capacity_=0;
            checked(cudaMalloc(reinterpret_cast<void**>(&value_),count*sizeof(T)));capacity_=count;}
        return value_;
    }
};
struct Result {const uint32_t* offsets;const uint32_t* slots;uint32_t owners;uint64_t events,bytes;float GPU_ms;};
class Workspace {
    Scratch<Progression> rules_;
    Scratch<uint64_t> first_;
    Scratch<uint32_t> counts_,cursor_,bad_,factOffsets_,factSlots_;
    Scratch<unsigned long long> total_;
    Scratch<unsigned char> temporary_;
    bool initialized_{};int device_{};cudaEvent_t began_{},ended_{};
public:
    template<class Allocate>
    Result build(const SmallFactorTable& table,const Cohort& cohort,const CompositePage* extra,Allocate allocate){
        const auto start=cohort.start();const auto length=cohort.length();validate_cohort(start,length);
        require(table.ordinary()==80000&&table.limit()==10000000&&table.rows().size()==656742,"Canonical GPU progression table required");
        const uint64_t extraEvents=extra?extra->events():0;
        if(extra){extra->assert_context(cohort);require(extraEvents<=65536,"Factored page budget exceeded");}
        int current{};checked(cudaGetDevice(&current));
        if(!initialized_){
            auto* rows=rules_.reserve(table.rows().size());
            checked(cudaMemcpy(rows,table.rows().data(),table.rows().size()*sizeof(Progression),cudaMemcpyHostToDevice));
            checked(cudaEventCreate(&began_));checked(cudaEventCreate(&ended_));device_=current;initialized_=true;
        }else require(device_==current,"Composite workspace cannot change its device");
        const auto owners=uint32_t((length+DescriptorSpan-1)/DescriptorSpan*pgn_psp_tile::TilesPerDescriptor);
        require(owners&&owners<=1048576,"Composite GPU owner budget exceeded");
        auto* counts=counts_.reserve(owners+1);auto* cursor=cursor_.reserve(owners+1);
        auto* bad=bad_.reserve(1);auto* total=total_.reserve(1);auto* first=first_.reserve(table.rows().size());
        auto* offsets=static_cast<uint32_t*>(allocate((owners+1)*sizeof(uint32_t)));
        auto* factOffsets=extra?factOffsets_.reserve(owners+1):nullptr;
        auto* factSlots=extra?factSlots_.reserve(size_t(std::max<uint64_t>(1,extraEvents))):nullptr;
        if(extra){checked(cudaMemcpy(factOffsets,extra->offsets().data(),(owners+1)*4,cudaMemcpyHostToDevice));
            if(extraEvents)checked(cudaMemcpy(factSlots,extra->slots().data(),extraEvents*4,cudaMemcpyHostToDevice));}
        checked(cudaMemset(counts,0,(owners+1)*4));checked(cudaMemset(bad,0,4));checked(cudaMemset(total,0,8));
        const auto blocks=uint32_t((table.rows().size()*32+255)/256);
        checked(cudaEventRecord(began_));
        progressions<false><<<blocks,256>>>(rules_.reserve(table.rows().size()),uint32_t(table.rows().size()),start,length,first,owners,counts,offsets,cursor,nullptr,0,total,bad);
        checked(cudaGetLastError());
        unsigned long long events{};checked(cudaMemcpy(&events,total,8,cudaMemcpyDeviceToHost));
        require(events<=MaximumEvents-extraEvents,"Composite GPU event budget reached; no truncated page");events+=extraEvents;
        auto* slots=static_cast<uint32_t*>(allocate(size_t(std::max<unsigned long long>(1,events))*4));
        if(extra)factored<false><<<(owners+255)/256,256>>>(owners,factOffsets,factSlots,counts,offsets,cursor,slots,uint32_t(events),bad);
        size_t bytes{};checked(cub::DeviceScan::ExclusiveSum(nullptr,bytes,counts,offsets,owners+1));
        auto* temporary=temporary_.reserve(bytes);checked(cub::DeviceScan::ExclusiveSum(temporary,bytes,counts,offsets,owners+1));
        checked(cudaMemcpy(cursor,offsets,(owners+1)*4,cudaMemcpyDeviceToDevice));
        progressions<true><<<blocks,256>>>(rules_.reserve(table.rows().size()),uint32_t(table.rows().size()),start,length,first,owners,counts,offsets,cursor,slots,uint32_t(events),total,bad);
        if(extra)factored<true><<<(owners+255)/256,256>>>(owners,factOffsets,factSlots,counts,offsets,cursor,slots,uint32_t(events),bad);
        validate_cursor<<<(owners+255)/256,256>>>(owners,offsets,cursor,bad);
        checked(cudaGetLastError());checked(cudaEventRecord(ended_));checked(cudaEventSynchronize(ended_));
        uint32_t failed{},last{};checked(cudaMemcpy(&failed,bad,4,cudaMemcpyDeviceToHost));
        checked(cudaMemcpy(&last,offsets+owners,4,cudaMemcpyDeviceToHost));
        require(!failed&&last==events,"Composite GPU owner ledger rejected");float ms{};checked(cudaEventElapsedTime(&ms,began_,ended_));
        return {offsets,slots,owners,uint64_t(events),(uint64_t(owners)+1+std::max<uint64_t>(1,events))*4,ms};
    }
};
} // namespace pgn_psp_native::gpu

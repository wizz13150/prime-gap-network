// Copyright (c) 2026 The PGN developers
// GPU arithmetic, SHA256 and RIPEMD160 reused from Ben Richard's BitCrack,
// pinned6bf8059ef075eb1622298395866b0bd02375e1d9 (MIT; retain LICENSE.MIT).
// This adapter has no private-key search/import/export or third-party targets.
#include "pgn_vanity_gpu.h"
#include <cuda_runtime.h>
#include "secp256k1.cuh"
#include "sha256.cuh"
#include "ripemd160.cuh"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Check(cudaError_t e) { if(e!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(e)); }
void Error(char* dst,uint32_t n,const char* message) { if(dst&&n)std::snprintf(dst,n,"%s",message); }
struct State {
    int device{}; uint32_t blocks{},threads{},points{},capacity{},range_count{};
    uint64_t count{},next{}; bool poisoned{},orbit{};
    unsigned int *x{},*y{},*chain{};
    PV_HashRange* ranges{}; PV_OrbitMatch* matches{};
    unsigned long long* counters{}; PV_Point increment{};
    cudaEvent_t begin{},end{};
    ~State() {
        if(!x&&!y&&!chain&&!ranges&&!matches&&!counters&&!begin&&!end)return;
        cudaSetDevice(device); // Never reset a device/context or free others' data.
        cudaFree(x);cudaFree(y);cudaFree(chain);cudaFree(ranges);cudaFree(matches);cudaFree(counters);
        if(begin)cudaEventDestroy(begin);if(end)cudaEventDestroy(end);
    }
};
int Compare(const uint32_t* a,const uint32_t* b) {
    for(int i=0;i<5;++i)if(a[i]!=b[i])return a[i]<b[i]?-1:1;return 0;
}
__device__ bool InRange(const unsigned int* hash,const PV_HashRange& r) {
    int lo=0,hi=0;for(int j=0;j<5;++j) {
        if(!lo&&hash[j]!=r.low[j])lo=hash[j]<r.low[j]?-1:1;
        if(!hi&&hash[j]!=r.high[j])hi=hash[j]<r.high[j]?-1:1;
    }return lo>=0&&hi<=0;
}
__global__ void Search(unsigned int* xs,unsigned int* ys,unsigned int* chain,
    PV_Point increment,const PV_HashRange* ranges,uint32_t range_count,
    PV_OrbitMatch* matches,uint32_t capacity,unsigned long long* counters,
    uint32_t points,uint32_t rounds,uint64_t first,bool orbit) {
    const uint64_t threads=uint64_t(gridDim.x)*blockDim.x;
    const uint64_t lane=uint64_t(blockIdx.x)*blockDim.x+threadIdx.x;
    const uint64_t count=threads*points;
    for(uint32_t round=0;round<rounds;++round) {
        unsigned int inverse[8]={0,0,0,0,0,0,0,1};
        for(uint32_t i=0;i<points;++i) {
            unsigned int x[8],y[8];readInt(xs,i,x);readInt(ys,i,y);
            if(isInfinity(x))atomicAdd(counters+1,1ULL);
            else {
                unsigned int orbit_x[8];copyBigInt(x,orbit_x);
                for(unsigned branch=0;branch<(orbit?3U:1U);++branch) {
                    if(branch)mulModP(_BETA,orbit_x);
                    for(unsigned negative=0;negative<(orbit?2U:1U);++negative) {
                        unsigned int sha[8],digest[5];
                        sha256PublicKeyCompressed(orbit_x,(y[7]&1)^negative,sha);
                        for(int j=0;j<8;++j)sha[j]=endian(sha[j]);
                        ripemd160sha256(sha,digest);
                        for(int j=0;j<5;++j)digest[j]=endian(digest[j]);
                        bool hit=false;
                        for(uint32_t r=0;r<range_count;++r)hit=hit||InRange(digest,ranges[r]);
                        if(hit) {
                            const auto index=atomicAdd(counters,1ULL);
                            if(index<capacity) {
                                auto& item=matches[index];auto& out=item.candidate;
                                out.slot=first+uint64_t(round)*count+uint64_t(i)*threads+lane;
                                copyBigInt(orbit_x,out.point.x);
                                // Reuse the upstream big-endian subtraction.
                                // Its unused negModP helper propagates carry
                                // in the opposite word order; do not import it.
                                if(negative)sub(_P,y,out.point.y);else copyBigInt(y,out.point.y);
                                for(int j=0;j<5;++j)out.hash[j]=digest[j];
                                item.endomorphism=branch;item.negated=negative;
                            }
                        }
                    }
                }
            }
            // Exclude infinity and P=-increment from the inversion product.
            // Multiplying a zero denominator would corrupt every lane member.
            const bool exceptional=isInfinity(x)||(equal(x,increment.x)&&!equal(y,increment.y));
            if(exceptional)writeInt(chain,i,inverse);
            else beginBatchAddWithDouble(increment.x,increment.y,xs,chain,i,i,inverse);
        }
        doBatchInverse(inverse);
        for(int i=int(points)-1;i>=0;--i) {
            unsigned int x[8],y[8],nx[8],ny[8];readInt(xs,i,x);readInt(ys,i,y);
            if(isInfinity(x)) {
                copyBigInt(increment.x,nx);copyBigInt(increment.y,ny);
            } else if(equal(x,increment.x)&&!equal(y,increment.y)) {
                for(int j=0;j<8;++j)nx[j]=ny[j]=0xffffffff;
            } else completeBatchAddWithDouble(increment.x,increment.y,xs,ys,i,i,chain,inverse,nx,ny);
            writeInt(xs,i,nx);writeInt(ys,i,ny);
        }
    }
}
std::string UUID(const cudaUUID_t& uuid) {
    std::string result="GPU-";char b[3];for(int i=0;i<16;++i) {
        if(i==4||i==6||i==8||i==10)result+='-';
        std::snprintf(b,sizeof(b),"%02x",static_cast<unsigned char>(uuid.bytes[i]));result+=b;
    }return result;
}
}
static int Create(const char* uuid,uint32_t blocks,uint32_t threads,uint32_t points,
    const PV_Point* initial,const PV_Point* increment,const PV_HashRange* ranges,
    uint32_t range_count,uint32_t capacity,void** handle,char* error,uint32_t error_capacity,bool orbit) {
    if(handle)*handle=nullptr;
    try {
        if(!handle||!uuid||!initial||!increment||!ranges||!range_count||range_count>32||
           !blocks||blocks>256||threads<32||threads>1024||threads%32||!points||points>256||
           !capacity||capacity>65536)throw std::invalid_argument("Invalid explicit GPU vanity configuration");
        const uint64_t count=uint64_t(blocks)*threads*points;
        if(count>4194304)throw std::invalid_argument("Public GPU point allocation exceeds admission");
        for(uint32_t i=0;i<range_count;++i)
            if(Compare(ranges[i].low,ranges[i].high)>0||(i&&Compare(ranges[i-1].high,ranges[i].low)>=0))
                throw std::invalid_argument("Hash ranges must be ordered, disjoint and nonempty");
        bool increment_infinity=true;for(int i=0;i<8;++i)increment_infinity&=increment->x[i]==0xffffffff;
        if(increment_infinity)throw std::invalid_argument("Finite public increment required");
        int devices{};Check(cudaGetDeviceCount(&devices));int selected=-1;
        for(int i=0;i<devices;++i){cudaDeviceProp properties{};Check(cudaGetDeviceProperties(&properties,i));if(UUID(properties.uuid)==uuid)selected=i;}
        if(selected<0)throw std::invalid_argument("Explicit CUDA device UUID not found");
        Check(cudaSetDevice(selected));
        auto state=std::make_unique<State>();state->device=selected;state->blocks=blocks;state->threads=threads;
        state->points=points;state->count=count;state->capacity=capacity;state->range_count=range_count;state->increment=*increment;state->orbit=orbit;
        std::vector<unsigned int> x(size_t(count)*8),y(size_t(count)*8);
        const uint64_t lanes=uint64_t(blocks)*threads;
        for(uint64_t p=0;p<count;++p)for(int w=0;w<8;++w) {
            const auto index=(p/lanes)*lanes*8+uint64_t(w)*lanes+p%lanes;
            x[index]=initial[p].x[w];y[index]=initial[p].y[w];
        }
        const size_t bytes=size_t(count)*8*sizeof(unsigned int);
        Check(cudaMalloc(&state->x,bytes));Check(cudaMalloc(&state->y,bytes));Check(cudaMalloc(&state->chain,bytes));
        Check(cudaMalloc(&state->ranges,range_count*sizeof(PV_HashRange)));
        Check(cudaMalloc(&state->matches,capacity*sizeof(PV_OrbitMatch)));Check(cudaMalloc(&state->counters,2*sizeof(unsigned long long)));
        Check(cudaMemcpy(state->x,x.data(),bytes,cudaMemcpyHostToDevice));Check(cudaMemcpy(state->y,y.data(),bytes,cudaMemcpyHostToDevice));
        Check(cudaMemcpy(state->ranges,ranges,range_count*sizeof(PV_HashRange),cudaMemcpyHostToDevice));
        Check(cudaEventCreate(&state->begin));Check(cudaEventCreate(&state->end));
        *handle=state.release();return 0;
    }catch(const std::exception& e){Error(error,error_capacity,e.what());return 1;}
}
static int Run(void* handle,uint32_t rounds,void* matches,uint32_t capacity,
    PV_Result* result,char* error,uint32_t error_capacity,bool orbit) {
    if(result)*result={};auto* state=static_cast<State*>(handle);
    try {
        if(!state||state->poisoned||state->orbit!=orbit||!result||!matches||!rounds||rounds>64||capacity<state->capacity)
            throw std::invalid_argument("Invalid or poisoned GPU vanity run");
        const auto slots=state->count*rounds;
        if(state->next>std::numeric_limits<uint64_t>::max()-slots)
            throw std::invalid_argument("GPU vanity slot interval wraps");
        Check(cudaSetDevice(state->device));state->poisoned=true;
        Check(cudaMemset(state->counters,0,2*sizeof(unsigned long long)));Check(cudaEventRecord(state->begin));
        Search<<<state->blocks,state->threads>>>(state->x,state->y,state->chain,state->increment,state->ranges,
            state->range_count,state->matches,state->capacity,state->counters,state->points,rounds,state->next,state->orbit);
        Check(cudaGetLastError());Check(cudaEventRecord(state->end));Check(cudaEventSynchronize(state->end));
        unsigned long long counts[2]{};Check(cudaMemcpy(counts,state->counters,sizeof(counts),cudaMemcpyDeviceToHost));
        result->first_slot=state->next;result->slots=slots;result->infinity=counts[1];result->hashed=(slots-counts[1])*(orbit?6:1);result->found=counts[0];
        Check(cudaEventElapsedTime(&result->kernel_ms,state->begin,state->end));
        if(counts[0]>state->capacity){result->overflow=1;throw std::runtime_error("GPU result overflow; handle poisoned, no complete search claim");}
        if(counts[0]) {
            if(orbit)Check(cudaMemcpy(matches,state->matches,size_t(counts[0])*sizeof(PV_OrbitMatch),cudaMemcpyDeviceToHost));
            else {
                std::vector<PV_OrbitMatch> data(static_cast<size_t>(counts[0]));
                Check(cudaMemcpy(data.data(),state->matches,data.size()*sizeof(PV_OrbitMatch),cudaMemcpyDeviceToHost));
                for(size_t i=0;i<data.size();++i)static_cast<PV_Match*>(matches)[i]=data[i].candidate;
            }
        }
        state->next+=slots;state->poisoned=false;return 0;
    }catch(const std::exception& e){Error(error,error_capacity,e.what());return 1;}
}
extern "C" int PV_Create(const char* uuid,uint32_t b,uint32_t t,uint32_t p,const PV_Point* initial,const PV_Point* inc,
    const PV_HashRange* ranges,uint32_t n,uint32_t capacity,void** handle,char* error,uint32_t error_capacity) {
    return Create(uuid,b,t,p,initial,inc,ranges,n,capacity,handle,error,error_capacity,false);
}
extern "C" int PV_CreateOrbit(const char* uuid,uint32_t b,uint32_t t,uint32_t p,const PV_Point* initial,const PV_Point* inc,
    const PV_HashRange* ranges,uint32_t n,uint32_t capacity,void** handle,char* error,uint32_t error_capacity) {
    return Create(uuid,b,t,p,initial,inc,ranges,n,capacity,handle,error,error_capacity,true);
}
extern "C" int PV_Run(void* handle,uint32_t rounds,PV_Match* matches,uint32_t capacity,PV_Result* result,char* error,uint32_t n) {
    return Run(handle,rounds,matches,capacity,result,error,n,false);
}
extern "C" int PV_RunOrbit(void* handle,uint32_t rounds,PV_OrbitMatch* matches,uint32_t capacity,PV_Result* result,char* error,uint32_t n) {
    return Run(handle,rounds,matches,capacity,result,error,n,true);
}
extern "C" void PV_Destroy(void* handle) { delete static_cast<State*>(handle); }

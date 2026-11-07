#pragma once
#include <cstdint>
#include "psp_tile_layout.h"
#ifdef __CUDACC__
#define PGN_COHORT_HD __host__ __device__
#else
#define PGN_COHORT_HD
#endif
namespace pgn_composite_device {
inline constexpr uint64_t OwnersPerPage=993ull*512;
struct Address {uint32_t page{},local{};};
PGN_COHORT_HD inline bool address(uint64_t owner,uint64_t owners,uint32_t pages,Address& out){
    if(!owners||pages!=(owners+OwnersPerPage-1)/OwnersPerPage||owner>=owners)return false;
    out={uint32_t(owner/OwnersPerPage),uint32_t(owner%OwnersPerPage)};
    return true;
}
struct Page {const uint32_t* offsets{};const uint32_t* slots{};uint32_t owners{};const uint32_t* counts{};uint32_t capacity{};};
struct Bank {const Page* pages{};uint32_t count{};uint64_t owners{},low{},high{};};
#ifdef __CUDACC__
__device__ __forceinline__ void apply(uint64_t* bitmap,const Bank& bank,uint64_t owner){
    Address a;
    if(!address(owner,bank.owners,bank.count,a)){__trap();return;}
    const auto page=bank.pages[a.page];
    if(!page.slots||a.local>=page.owners){__trap();return;}
    if(page.capacity){
        if(!page.counts||page.capacity>32||page.counts[a.local]>page.capacity){__trap();return;}
        for(uint32_t i=threadIdx.x;i<page.counts[a.local];i+=blockDim.x){
            const auto slot=page.slots[uint64_t(a.local)*page.capacity+i];
            if(slot>=pgn_psp_tile::TileSlots){__trap();return;}
            atomicOr(reinterpret_cast<unsigned long long*>(bitmap+(slot>>6)),1ull<<(slot&63u));
        }
    }else{
        if(!page.offsets){__trap();return;}
        pgn_psp_tile::apply(bitmap,page.offsets,page.slots,a.local);
    }
}
#endif
}
#undef PGN_COHORT_HD

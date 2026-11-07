#pragma once
#include <cstdint>
// Native retained class-30 geometry, from segmented_sieve.cuh:
// descriptor30*2^25 integers ->2^28 slots; tile2^19 slots ->8192 u64 words.
// A set bit is a certified composite. This header changes no production engine.
namespace pgn_psp_tile {
inline constexpr uint32_t SlotBits=28,TileBits=19,WordBits=6;
inline constexpr uint32_t DescriptorSlots=1u<<SlotBits,TileSlots=1u<<TileBits;
inline constexpr uint32_t TileWords=TileSlots>>WordBits,TilesPerDescriptor=DescriptorSlots/TileSlots;
struct Coordinate {uint32_t tile,word,bit;};
constexpr Coordinate coordinate(uint32_t slot){return {slot>>TileBits,(slot&(TileSlots-1))>>WordBits,slot&63u};}
constexpr uint32_t slot(Coordinate c){return (c.tile<<TileBits)|(c.word<<WordBits)|c.bit;}
static_assert(TileWords==8192&&TilesPerDescriptor==512);
static_assert(slot(coordinate(DescriptorSlots-1))==DescriptorSlots-1);
#ifdef __CUDACC__
// Host-validated CSR offsets/slots; caller supplies the descriptor/tile owner.
// Insert before the existing final shared-memory barrier/store. OR never
// changes another tile, and duplicate composite events are idempotent.
__device__ __forceinline__ void apply(uint64_t* shared,const uint32_t* offsets,
                                    const uint32_t* localSlots,uint32_t owner){
 const auto begin=offsets[owner],end=offsets[owner+1];
 for(uint32_t i=begin+threadIdx.x;i<end;i+=blockDim.x){const uint32_t s=localSlots[i];atomicOr(reinterpret_cast<unsigned long long*>(shared+(s>>6)),1ull<<(s&63u));}
}
#endif
}

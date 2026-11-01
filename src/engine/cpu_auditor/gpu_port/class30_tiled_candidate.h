#pragma once
#include "class30_sieve_port.h"
#include <array>
#include <algorithm>
namespace pgn_cpu_shared {
// CPU locality mapping of canonical segmented_sieve's bounded shared-memory tiles.
// Identical composite relation; only physical visitation order changes.
inline std::vector<uint64_t> tiledClass30Marks(Number96 origin,uint64_t slots,const std::vector<SievePrime>&bank,uint64_t tileSlots=(1ULL<<19)){
 if(!slots||slots>(1ULL<<28)||!tileSlots||(tileSlots&63))throw std::runtime_error("Invalid canonicaltileextent");
 std::vector<uint64_t>bits((slots+63)/64,0);std::vector<std::array<uint32_t,8>>next(bank.size());
 for(size_t k=0;k<bank.size();k++){
  const auto&e=bank[k];uint64_t p=e.p,r=(origin.low%p+uint64_t(origin.high%p)*e.r64)%p;
  for(unsigned c=0;c<8;c++)next[k][c]=uint32_t(8*((p-(r+Classes30[c])%p)%p)*e.inverse30% (8*p)+c);
 }
 for(uint64_t base=0;base<slots;base+=tileSlots){
  const uint64_t end=std::min(slots,base+tileSlots);
  for(size_t k=0;k<bank.size();k++)for(unsigned c=0;c<8;c++){
   uint64_t position=next[k][c],stride=8ULL*bank[k].p;
   for(;position<end;position+=stride)bits[position>>6]|=1ULL<<(position&63);
   next[k][c]=uint32_t(position);
  }
 }
 if(slots&63)bits.back()|=~0ULL<<(slots&63);
 return bits;
}
}

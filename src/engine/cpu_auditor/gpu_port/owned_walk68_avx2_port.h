#pragma once
#include "owned_walk68_port.h"
#include "shared_narrow_avx2_map.hpp"
#include <array>
namespace pgn_cpu_shared {
struct WalkLane68 {
 enum Phase{Initial,Backward,Forward,Done};Phase phase=Initial;uint64_t hi=0;int64_t p=-1,s=-1,q=-1,c=-1,query=-1;bool boundary=false;
 void prepare(const Class30Bitmap&bitmap,const uint32_t steps[8]){
  for(;;){
   if(phase==Done)return;
   if(phase==Initial){if(c<0){boundary=true;phase=Done;}else query=c;return;}
   if(phase==Backward){if(uint64_t(p)>=hi){phase=Done;return;}if(s>=int64_t(bitmap.slots())){boundary=true;phase=Done;return;}s=bitmap.previous(s);if(s<0){boundary=true;phase=Done;return;}if(s==p){q=p+steps[p&7];phase=Forward;continue;}query=s;return;}
   q=bitmap.next(q);if(q<0){boundary=true;phase=Done;return;}query=q;return;
  }
 }
 void accept(bool positive,const Class30Bitmap&bitmap,const uint32_t steps[8],uint32_t minimum,WalkReceipt&out){
  if(phase==Initial){if(positive){p=c;s=p+steps[p&7];phase=Backward;}else c=bitmap.next(c+1);}
  else if(phase==Backward){if(positive){p=s;++out.jumps;s=p+steps[p&7];}else --s;}
  else if(phase==Forward){if(positive){auto a=bitmap.number(p),b=bitmap.number(q);if(b.low-a.low>=minimum){if(out.gaps.size()>=4095)throw std::runtime_error("Canonicalresultcapacity exceeded");out.gaps.push_back({a,b});}p=q;++out.jumps;s=p+steps[p&7];phase=Backward;}else ++q;}
  prepare(bitmap,steps);
 }
};
// Eight independent canonical owner slices, one sharedbitmap and one pointbatch.
// Admission is made once on the pinnedworker before entering this hot operation.
inline WalkReceipt walk68x4(const Class30Bitmap&bitmap,uint64_t ownedLo,uint64_t ownedHi,uint32_t minimum){
 if(minimum<30||ownedLo>=ownedHi||ownedHi>bitmap.slots())throw std::runtime_error("Invalid SIMD ownedwalk");
 int8_t index[30];for(auto&x:index)x=-1;for(unsigned j=0;j<8;j++)index[Classes30[j]]=int8_t(j);uint32_t steps[8];scout_wheel::window_steps(minimum,Classes30,index,steps);
 WalkReceipt out;std::array<WalkLane68,8>lanes;const uint64_t span=ownedHi-ownedLo;
 for(unsigned j=0;j<8;j++){uint64_t lo=ownedLo+span*j/8;lanes[j].hi=ownedLo+span*(j+1)/8;if(lo==lanes[j].hi)lanes[j].phase=WalkLane68::Done;else{lanes[j].c=bitmap.next(int64_t(lo));lanes[j].prepare(bitmap,steps);}}
 pgn_promoted_avx2::CacheEight cache;
 for(;;){
  uint64_t lows[8]{};uint32_t high=0;unsigned active=0,first=0;
  for(unsigned j=0;j<8;j++)if(lanes[j].phase!=WalkLane68::Done){auto n=bitmap.number(lanes[j].query);if(n.high<8||n.high>=16){out.unsupported_family=true;return out;}if(active&&n.high!=high)throw std::runtime_error("SIMDpacket crossesword; rebindrequired");high=n.high;lows[j]=n.low;if(!active)first=j;active|=1u<<j;}
  if(!active)break;
  for(unsigned j=0;j<8;j++)if(!(active&(1u<<j)))lows[j]=lows[first];
  const unsigned positive=pgn_promoted_avx2::test_eight_admitted(lows,high,cache);
  for(unsigned j=0;j<8;j++)if(active&(1u<<j)){++out.tests;lanes[j].accept((positive>>j)&1,bitmap,steps,minimum,out);}
 }
 for(const auto&lane:lanes)out.query_right_boundary_needed|=lane.boundary;
 return out;
}
}

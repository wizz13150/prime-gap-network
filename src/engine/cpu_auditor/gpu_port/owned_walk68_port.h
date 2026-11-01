#pragma once
#include "shared_scalar68.h"
#include "class30_sieve_port.h"
namespace pgn_cpu_shared {
struct CandidateGap{Number96 lower,upper;};
struct WalkReceipt{uint64_t tests=0,jumps=0;bool query_right_boundary_needed=false,unsupported_family=false;std::vector<CandidateGap>gaps;};
// Port of candidate_walk.cuh w_owned_slice; no prime-proof claim for EJ anchors.
// Caller verifies every reported pair and repairs exact logical seams beforecredit.
inline WalkReceipt walk68(const Class30Bitmap&bitmap,uint64_t ownedLo,uint64_t ownedHi,uint32_t minimum){
 if(minimum<30||ownedLo>=ownedHi||ownedHi>bitmap.slots())throw std::runtime_error("Invalid ownedwalk domain");
 int8_t index[30];for(auto&x:index)x=-1;for(unsigned i=0;i<8;i++)index[Classes30[i]]=int8_t(i);
 uint32_t steps[8];scout_wheel::window_steps(minimum,Classes30,index,steps);WalkReceipt out;NarrowOwner owner;
 auto test=[&](int64_t slot){const auto n=bitmap.number(uint64_t(slot));++out.tests;int value=euler68(n.low,n.high,owner);if(value<0)out.unsupported_family=true;return value;};
 int64_t c=bitmap.next(int64_t(ownedLo));while(c>=0){int result=test(c);if(out.unsupported_family)return out;if(result)break;c=bitmap.next(c+1);}
 if(c<0){out.query_right_boundary_needed=true;return out;}
 int64_t p=c,s=p+steps[p&7];
 for(;;){
  if(uint64_t(p)>=ownedHi)return out;
  if(s>=int64_t(bitmap.slots())){out.query_right_boundary_needed=true;return out;}
  s=bitmap.previous(s);if(s<0){out.query_right_boundary_needed=true;return out;}
  if(s==p){
   int64_t q=p+steps[p&7];
   for(;;){q=bitmap.next(q);if(q<0){out.query_right_boundary_needed=true;return out;}int result=test(q);if(out.unsupported_family)return out;if(result){const auto a=bitmap.number(p),b=bitmap.number(q);if(b.low-a.low>=minimum){if(out.gaps.size()>=4095)throw std::runtime_error("Canonicalretained resultcapacity exceeded; prefixuncredited");out.gaps.push_back({a,b});}p=q;++out.jumps;s=p+steps[p&7];break;}++q;}
  }else{
   int result=test(s);if(out.unsupported_family)return out;if(result){p=s;++out.jumps;s=p+steps[p&7];}else --s;
  }
 }
}
}

#pragma once
#include "reported_gap_verify68_port.h"
#include <algorithm>
#include <chrono>
#include "owned_walk68_avx2_port.h"
#include "class30_tiled_candidate.h"
#include "cpu_composite_overlay.h"
namespace pgn_cpu_shared {
inline int compare(Number96 a,Number96 b){if(a.high!=b.high)return a.high<b.high?-1:1;if(a.low!=b.low)return a.low<b.low?-1:1;return 0;}
inline Number96 addSmall(Number96 n,uint64_t delta){uint64_t lo=n.low+delta;return {lo,n.high+uint32_t(lo<n.low)};}
inline Number96 subtractSmall(Number96 n,uint64_t delta){uint64_t lo=n.low-delta;return {lo,n.high-uint32_t(n.low<delta)};}
inline uint32_t remainder30(Number96 n){return uint32_t((n.low%30+(n.high%30)*16)%30);}
inline bool exactAt68Seam(Number96 n){const int r=pgn_mr96::prime12(n.low,n.high);if(r<0)throw std::runtime_error("Canonical exactseam outside provedMR13bound");return r==1;}
struct ExactPair{Number96 lower,upper;};
// CPU mapping of qualifiedhost boundary_pair; exact sharedMR13 suffices at68 seams.
inline ExactPair exactPair68Seam(Number96 end){
 Number96 lower=subtractSmall(end,(end.low&1)?2:1),upper=(end.low&1)?end:addSmall(end,1);
 while(!exactAt68Seam(lower))lower=subtractSmall(lower,2);
 while(!exactAt68Seam(upper))upper=addSmall(upper,2);
 return {lower,upper};
}
inline bool ownedRetained(const ExactPair&gap,Number96 start,Number96 end,uint32_t minimum){
 if(compare(start,gap.lower)>0||compare(gap.lower,end)>=0)return false;
 const uint32_t highDifference=gap.upper.high-gap.lower.high;
 if(highDifference>=2||(highDifference==1&&gap.upper.low>=gap.lower.low))return true;
 return gap.upper.low-gap.lower.low>=minimum;
}
struct Owned68Result{uint64_t positions=0,retained=0,tests=0,jumps=0,composite_new_marks=0;uint32_t exact_prefix_positions=0;bool complete=false;double sieve_seconds=0,walk_seconds=0,verify_seconds=0,boundary_seconds=0,complement_seconds=0;std::vector<ExactPair> best;};
// The normal report orders records by gap DESC, lower ASC. The top100 of a
// union is contained in the union of its top100 sets, so each piece stays bounded.
inline void retainOwned(Owned68Result&result,const ExactPair&gap){
 const uint32_t high=gap.upper.high-gap.lower.high-uint32_t(gap.upper.low<gap.lower.low);
 if(high||compare(gap.lower,gap.upper)>=0)throw std::runtime_error("Unsupported reportedgap width; prefixuncredited");
 ++result.retained;result.best.push_back(gap);
 std::sort(result.best.begin(),result.best.end(),[](const ExactPair&a,const ExactPair&b){const uint64_t ga=a.upper.low-a.lower.low,gb=b.upper.low-b.lower.low;return ga!=gb?ga>gb:compare(a.lower,b.lower)<0;});
 if(result.best.size()>100)result.best.resize(100);
}
// Bounded auditpiece of the sharedpipeline. No range-scanningFLINT/MR backend.
// Boundaries are exact; the inherited sourcePRP coverage contract is not strengthened.
inline Owned68Result countOwned68(Number96 start,uint64_t length,uint32_t minimum,const std::vector<SievePrime>&bank,bool simd4=false,bool tiled=false,const CpuCompositeOverlay* complement=nullptr){
 if(start.high<8||start.high>=16||!length||length>100000000||length-1>UINT64_MAX-start.low||minimum<30)throw std::runtime_error("Unsupported sharedCPU68 ownedpiece");
 if(complement)complement->assertCovers(start,length);
 const Number96 end=addSmall(start,length);Owned68Result result;
  if(length<30){const auto phase=std::chrono::steady_clock::now();const auto pair=exactPair68Seam(end);if(ownedRetained(pair,start,end,minimum))retainOwned(result,pair);result.positions=length;result.exact_prefix_positions=uint32_t(length);result.complete=true;result.boundary_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-phase).count();return result;}
 Number96 nativeStart=start;uint64_t remaining=length;
 const uint32_t remainder=remainder30(start);
 if(start.low<remainder){
  const uint64_t prefix=std::min<uint64_t>(remaining,30-remainder);const Number96 prefixEnd=addSmall(start,prefix);
   const auto phase=std::chrono::steady_clock::now();const auto crossing=exactPair68Seam(prefixEnd);result.boundary_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-phase).count();if(ownedRetained(crossing,start,prefixEnd,minimum))retainOwned(result,crossing);
  result.exact_prefix_positions=uint32_t(prefix);nativeStart=prefixEnd;remaining-=prefix;
 }
 if(remaining){
  const uint32_t back=remainder30(nativeStart);if(nativeStart.low<back)throw std::runtime_error("Unrepaired class30prefix");
  const Number96 origin{subtractSmall(nativeStart,back)};
  const uint64_t remainingToLast=remaining-1+(nativeStart.low-origin.low);
  const uint64_t slots=scout_extent::candidate_slots(remainingToLast,1ULL<<28,Classes30);
  if(!slots)throw std::runtime_error("Nonempty nativepiece has no class30extent");
  auto phase=std::chrono::steady_clock::now();Class30Bitmap bitmap=tiled?Class30Bitmap(origin,slots,tiledClass30Marks(origin,slots,bank)):Class30Bitmap(origin,slots,bank);result.sieve_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-phase).count();
  if(complement){phase=std::chrono::steady_clock::now();result.composite_new_marks=complement->apply(bitmap);result.complement_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-phase).count();}
  phase=std::chrono::steady_clock::now();const auto walked=simd4?walk68x4(bitmap,0,slots,minimum):walk68(bitmap,0,slots,minimum);
  result.walk_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-phase).count();bitmap.markComputeUse();
  if(walked.unsupported_family)throw std::runtime_error("Unsupported family reached; prefixuncredited");
  // Verify every owned internalrecord before any of this piece may commit.
  phase=std::chrono::steady_clock::now();uint64_t internal=0;
  for(const auto&gap:walked.gaps){
   if(compare(gap.lower,nativeStart)<0||compare(gap.lower,end)>=0||compare(gap.upper,end)>=0)continue;
   if(verifyReportedGap68(gap)!=ReportedGapCheck::Verified)throw std::runtime_error("Canonicalreported-gap check rejected; prefixuncredited");
    ++internal;retainOwned(result,ExactPair{gap.lower,gap.upper});
  }
   result.verify_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-phase).count();result.tests=walked.tests;result.jumps=walked.jumps;
   phase=std::chrono::steady_clock::now();const auto crossing=exactPair68Seam(end);result.boundary_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-phase).count();if(ownedRetained(crossing,nativeStart,end,minimum))retainOwned(result,crossing);
 }
 result.positions=length;result.complete=true;return result;
}
}

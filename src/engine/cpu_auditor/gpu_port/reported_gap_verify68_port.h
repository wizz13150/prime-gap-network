#pragma once
#include "owned_walk68_port.h"
#if !defined(__CUDACC__)
#define __host__
#define __device__
#define __forceinline__ inline
#define PGN_CPU_PORT_HOST_QUALIFIERS
#endif
#include "../../prime_gap_native/pgn_mr96.cuh"
#ifdef PGN_CPU_PORT_HOST_QUALIFIERS
#undef __host__
#undef __device__
#undef __forceinline__
#undef PGN_CPU_PORT_HOST_QUALIFIERS
#endif
namespace pgn_cpu_shared {
enum class ReportedGapCheck{Verified,Rejected,NeedsCanonicalSeam,UnsupportedFamily};
// Direct CPU mapping of pgn_graph_verify_optimized_width<68,...>.
// No descriptor credit is allowed until ALL reportedpairs pass and hostseams commit.
inline ReportedGapCheck verifyReportedGap68(const CandidateGap&gap){
 if(gap.lower.high<8||gap.lower.high>=16||gap.upper.high<8||gap.upper.high>=16)return ReportedGapCheck::UnsupportedFamily;
 if(gap.lower.high!=gap.upper.high)return ReportedGapCheck::NeedsCanonicalSeam;
 if(gap.upper.low<=gap.lower.low)return ReportedGapCheck::Rejected;
 if(pgn_mr96::prime12(gap.lower.low,gap.lower.high)!=1||pgn_mr96::prime12(gap.upper.low,gap.upper.high)!=1)return ReportedGapCheck::Rejected;
 NarrowOwner owner;
 for(uint64_t n=gap.lower.low+1;n<gap.upper.low;++n){
  const uint32_t high=gap.lower.high;
  if(!(n&1)||(n%3+high%3)%3==0||(n%5+high%5)%5==0)continue;
  const int candidate=euler68(n,high,owner);
  if(candidate<0)return ReportedGapCheck::UnsupportedFamily;
  if(candidate&&pgn_mr96::screen13(n,high)!=0)return ReportedGapCheck::Rejected;
 }
 return ReportedGapCheck::Verified;
}
}

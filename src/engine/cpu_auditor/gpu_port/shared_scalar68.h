#pragma once
// CPU adapter of the exact canonical source body, not a replacement predicate.
// Canonical manifest96C600D3486DED57B400FCD4428E904F6A3743E49F587DC85E93847D9FFDDE13.
#include "../../prime_gap_native/optimized_narrow_euler.cuh"
#include "../../prime_gap_native/window_steps.h"
#include "shared_scalar64_mapping.h"
#include "../../prime_gap_native/candidate_extent.h"
namespace pgn_cpu_shared {
struct NarrowOwner { optimized_narrow_euler::State arithmetic; };
// This is an Euler/Jacobi candidate decision. A positive is not a prime proof.
//68-bit first-stage admission; no generic fallback for unsupported families.
inline int euler68(uint64_t low,uint32_t high,NarrowOwner&owner){
 if(high<8||high>=16)return -1;
 switch(high){
  case 8:return pgn_cpu_scalar64::test(low,8u,owner.arithmetic)?1:0;
  case 9:return pgn_cpu_scalar64::test(low,9u,owner.arithmetic)?1:0;
  case 10:return pgn_cpu_scalar64::test(low,10u,owner.arithmetic)?1:0;
  case 11:return pgn_cpu_scalar64::test(low,11u,owner.arithmetic)?1:0;
  case 12:return pgn_cpu_scalar64::test(low,12u,owner.arithmetic)?1:0;
  case 13:return pgn_cpu_scalar64::test(low,13u,owner.arithmetic)?1:0;
  case 14:return pgn_cpu_scalar64::test(low,14u,owner.arithmetic)?1:0;
  case 15:return pgn_cpu_scalar64::test(low,15u,owner.arithmetic)?1:0;
 }
 return -1;
}
}

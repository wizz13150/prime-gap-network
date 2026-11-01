#include "candidate_extent.h"
#include "window_steps.h"
// GPU-owned interval search with exact endpoint proof before publication.
// Runtime publication uses the existing phase0 source protocol, not network credit.
#include "exact_prime_proof.h"
#include <cuda_runtime.h>
#include "resident_runtime.h"
#include "resident_protocol.h"
#include "portable_platform.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>
#ifdef __linux__
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>
#endif
#include "search_pipeline.cuh"
#include "memory_plan.h"
#include "engine_specs.h"
#ifndef PGN_VERIFY_GAPS
#error Production source adapter requires GPU gap certificates
#endif
#define CHECK(call)                                                                                \
    do {                                                                                           \
        auto e = (call);                                                                           \
        if (e != cudaSuccess) {                                                                    \
            std::fprintf(stderr, "PGN CUDA %s line%d\n", cudaGetErrorString(e), __LINE__);         \
            return 2;                                                                              \
        }                                                                                          \
    } while (0)
constexpr uint64_t Words = (1ULL << 28) / 64, BlockNumbers = 30ULL << 25;
#ifndef PGN_DESCRIPTOR_BLOCKS
#define PGN_DESCRIPTOR_BLOCKS 64
#endif
static unsigned K = PGN_DESCRIPTOR_BLOCKS, SlotBlocks = K + 1;
static const void *selectWalkKernel(uint64_t high,bool clipped) {
 if(high>=4ULL&&high<5ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<67,4ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<67,4ULL,false,false>);
 }
 if(high>=5ULL&&high<6ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<67,5ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<67,5ULL,false,false>);
 }
 if(high>=6ULL&&high<7ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<67,6ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<67,6ULL,false,false>);
 }
 if(high>=7ULL&&high<8ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<67,7ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<67,7ULL,false,false>);
 }
 if(high>=8ULL&&high<9ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,8ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,8ULL,false,false>);
 }
 if(high>=9ULL&&high<10ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,9ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,9ULL,false,false>);
 }
 if(high>=10ULL&&high<11ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,10ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,10ULL,false,false>);
 }
 if(high>=11ULL&&high<12ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,11ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,11ULL,false,false>);
 }
 if(high>=12ULL&&high<13ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,12ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,12ULL,false,false>);
 }
 if(high>=13ULL&&high<14ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,13ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,13ULL,false,false>);
 }
 if(high>=14ULL&&high<15ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,14ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,14ULL,false,false>);
 }
 if(high>=15ULL&&high<16ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,15ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<68,15ULL,false,false>);
 }
 if(high>=16ULL&&high<17ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,16ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,16ULL,false,false>);
 }
 if(high>=17ULL&&high<18ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,17ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,17ULL,false,false>);
 }
 if(high>=18ULL&&high<19ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,18ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,18ULL,false,false>);
 }
 if(high>=19ULL&&high<20ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,19ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,19ULL,false,false>);
 }
 if(high>=20ULL&&high<21ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,20ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,20ULL,false,false>);
 }
 if(high>=21ULL&&high<22ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,21ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,21ULL,false,false>);
 }
 if(high>=22ULL&&high<23ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,22ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,22ULL,false,false>);
 }
 if(high>=23ULL&&high<24ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,23ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,23ULL,false,false>);
 }
 if(high>=24ULL&&high<25ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,24ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,24ULL,false,false>);
 }
 if(high>=25ULL&&high<26ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,25ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,25ULL,false,false>);
 }
 if(high>=26ULL&&high<27ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,26ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,26ULL,false,false>);
 }
 if(high>=27ULL&&high<28ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,27ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,27ULL,false,false>);
 }
 if(high>=28ULL&&high<29ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,28ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,28ULL,false,false>);
 }
 if(high>=29ULL&&high<30ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,29ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,29ULL,false,false>);
 }
 if(high>=30ULL&&high<31ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,30ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,30ULL,false,false>);
 }
 if(high>=31ULL&&high<32ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,31ULL,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<69,31ULL,false,false>);
 }
 if(high>=32ULL&&high<64ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<70,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<70,UINT64_MAX,false,false>);
 }
 if(high>=64ULL&&high<128ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<71,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<71,UINT64_MAX,false,false>);
 }
 if(high>=128ULL&&high<232ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<72,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<72,UINT64_MAX,false,false>);
 }
 if(high>=232ULL&&high<256ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<72,UINT64_MAX,true,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<72,UINT64_MAX,true,false>);
 }
 if(high>=256ULL&&high<335ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<73,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<73,UINT64_MAX,false,false>);
 }
 if(high>=335ULL&&high<512ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<73,UINT64_MAX,true,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<73,UINT64_MAX,true,false>);
 }
 if(high>=512ULL&&high<703ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<74,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<74,UINT64_MAX,false,false>);
 }
 if(high>=703ULL&&high<1024ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<74,UINT64_MAX,true,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<74,UINT64_MAX,true,false>);
 }
 if(high>=1024ULL&&high<1148ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<75,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<75,UINT64_MAX,false,false>);
 }
 if(high>=1148ULL&&high<2048ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<75,UINT64_MAX,true,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<75,UINT64_MAX,true,false>);
 }
 if(high>=2048ULL&&high<4096ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<76,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<76,UINT64_MAX,false,false>);
 }
 if(high>=4096ULL&&high<8192ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<77,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<77,UINT64_MAX,false,false>);
 }
 if(high>=8192ULL&&high<16384ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<78,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<78,UINT64_MAX,false,false>);
 }
 if(high>=16384ULL&&high<23167ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<79,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<79,UINT64_MAX,false,false>);
 }
 if(high>=23167ULL&&high<32768ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<79,UINT64_MAX,true,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<79,UINT64_MAX,true,false>);
 }
 if(high>=32768ULL&&high<65536ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<80,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<80,UINT64_MAX,false,false>);
 }
 if(high>=65536ULL&&high<131072ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<81,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<81,UINT64_MAX,false,false>);
 }
 if(high>=131072ULL&&high<196608ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<82,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<82,UINT64_MAX,false,false>);
 }
 if(high>=196608ULL&&high<262144ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<82,UINT64_MAX,true,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<82,UINT64_MAX,true,false>);
 }
 if(high>=262144ULL&&high<524288ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<83,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<83,UINT64_MAX,false,false>);
 }
 if(high>=524288ULL&&high<1048576ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<84,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<84,UINT64_MAX,false,false>);
 }
 if(high>=1048576ULL&&high<2097152ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<85,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<85,UINT64_MAX,false,false>);
 }
 if(high>=2097152ULL&&high<3145728ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<86,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<86,UINT64_MAX,false,false>);
 }
 if(high>=3145728ULL&&high<4194304ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<86,UINT64_MAX,true,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<86,UINT64_MAX,true,false>);
 }
 if(high>=4194304ULL&&high<5242880ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<87,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<87,UINT64_MAX,false,false>);
 }
 if(high>=5242880ULL&&high<8388608ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<87,UINT64_MAX,true,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<87,UINT64_MAX,true,false>);
 }
 if(high>=8388608ULL&&high<16777216ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<88,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<88,UINT64_MAX,false,false>);
 }
 if(high>=16777216ULL&&high<33554432ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<89,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<89,UINT64_MAX,false,false>);
 }
 if(high>=33554432ULL&&high<67108864ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<90,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<90,UINT64_MAX,false,false>);
 }
 if(high>=67108864ULL&&high<134217728ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<91,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<91,UINT64_MAX,false,false>);
 }
 if(high>=134217728ULL&&high<206941441ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<92,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<92,UINT64_MAX,false,false>);
 }
 if(high>=206941441ULL&&high<268435456ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<92,UINT64_MAX,true,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<92,UINT64_MAX,true,false>);
 }
 if(high>=268435456ULL&&high<292659403ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<93,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<93,UINT64_MAX,false,false>);
 }
 if(high>=292659403ULL&&high<536870912ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<93,UINT64_MAX,true,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<93,UINT64_MAX,true,false>);
 }
 if(high>=536870912ULL&&high<585318807ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<94,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<94,UINT64_MAX,false,false>);
 }
 if(high>=585318807ULL&&high<1073741824ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<94,UINT64_MAX,true,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<94,UINT64_MAX,true,false>);
 }
 if(high>=1073741824ULL&&high<1342177280ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<95,UINT64_MAX,false,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<95,UINT64_MAX,false,false>);
 }
 if(high>=1342177280ULL&&high<2147483648ULL){
  if(clipped)return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<95,UINT64_MAX,true,true>);
  return reinterpret_cast<const void *>(pgn_graph_walk_optimized_width<95,UINT64_MAX,true,false>);
 }
 return nullptr;
}
static const void *selectVerifyKernel(uint64_t high) {
 if(high>=4ULL&&high<5ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<67,4ULL,false>);
 }
 if(high>=5ULL&&high<6ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<67,5ULL,false>);
 }
 if(high>=6ULL&&high<7ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<67,6ULL,false>);
 }
 if(high>=7ULL&&high<8ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<67,7ULL,false>);
 }
 if(high>=8ULL&&high<9ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<68,8ULL,false>);
 }
 if(high>=9ULL&&high<10ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<68,9ULL,false>);
 }
 if(high>=10ULL&&high<11ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<68,10ULL,false>);
 }
 if(high>=11ULL&&high<12ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<68,11ULL,false>);
 }
 if(high>=12ULL&&high<13ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<68,12ULL,false>);
 }
 if(high>=13ULL&&high<14ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<68,13ULL,false>);
 }
 if(high>=14ULL&&high<15ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<68,14ULL,false>);
 }
 if(high>=15ULL&&high<16ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<68,15ULL,false>);
 }
 if(high>=16ULL&&high<17ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,16ULL,false>);
 }
 if(high>=17ULL&&high<18ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,17ULL,false>);
 }
 if(high>=18ULL&&high<19ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,18ULL,false>);
 }
 if(high>=19ULL&&high<20ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,19ULL,false>);
 }
 if(high>=20ULL&&high<21ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,20ULL,false>);
 }
 if(high>=21ULL&&high<22ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,21ULL,false>);
 }
 if(high>=22ULL&&high<23ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,22ULL,false>);
 }
 if(high>=23ULL&&high<24ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,23ULL,false>);
 }
 if(high>=24ULL&&high<25ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,24ULL,false>);
 }
 if(high>=25ULL&&high<26ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,25ULL,false>);
 }
 if(high>=26ULL&&high<27ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,26ULL,false>);
 }
 if(high>=27ULL&&high<28ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,27ULL,false>);
 }
 if(high>=28ULL&&high<29ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,28ULL,false>);
 }
 if(high>=29ULL&&high<30ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,29ULL,false>);
 }
 if(high>=30ULL&&high<31ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,30ULL,false>);
 }
 if(high>=31ULL&&high<32ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<69,31ULL,false>);
 }
 if(high>=32ULL&&high<64ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<70,UINT64_MAX,false>);
 }
 if(high>=64ULL&&high<128ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<71,UINT64_MAX,false>);
 }
 if(high>=128ULL&&high<232ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<72,UINT64_MAX,false>);
 }
 if(high>=232ULL&&high<256ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<72,UINT64_MAX,true>);
 }
 if(high>=256ULL&&high<335ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<73,UINT64_MAX,false>);
 }
 if(high>=335ULL&&high<512ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<73,UINT64_MAX,true>);
 }
 if(high>=512ULL&&high<703ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<74,UINT64_MAX,false>);
 }
 if(high>=703ULL&&high<1024ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<74,UINT64_MAX,true>);
 }
 if(high>=1024ULL&&high<1148ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<75,UINT64_MAX,false>);
 }
 if(high>=1148ULL&&high<2048ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<75,UINT64_MAX,true>);
 }
 if(high>=2048ULL&&high<4096ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<76,UINT64_MAX,false>);
 }
 if(high>=4096ULL&&high<8192ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<77,UINT64_MAX,false>);
 }
 if(high>=8192ULL&&high<16384ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<78,UINT64_MAX,false>);
 }
 if(high>=16384ULL&&high<23167ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<79,UINT64_MAX,false>);
 }
 if(high>=23167ULL&&high<32768ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<79,UINT64_MAX,true>);
 }
 if(high>=32768ULL&&high<65536ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<80,UINT64_MAX,false>);
 }
 if(high>=65536ULL&&high<131072ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<81,UINT64_MAX,false>);
 }
 if(high>=131072ULL&&high<196608ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<82,UINT64_MAX,false>);
 }
 if(high>=196608ULL&&high<262144ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<82,UINT64_MAX,true>);
 }
 if(high>=262144ULL&&high<524288ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<83,UINT64_MAX,false>);
 }
 if(high>=524288ULL&&high<1048576ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<84,UINT64_MAX,false>);
 }
 if(high>=1048576ULL&&high<2097152ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<85,UINT64_MAX,false>);
 }
 if(high>=2097152ULL&&high<3145728ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<86,UINT64_MAX,false>);
 }
 if(high>=3145728ULL&&high<4194304ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<86,UINT64_MAX,true>);
 }
 if(high>=4194304ULL&&high<5242880ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<87,UINT64_MAX,false>);
 }
 if(high>=5242880ULL&&high<8388608ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<87,UINT64_MAX,true>);
 }
 if(high>=8388608ULL&&high<16777216ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<88,UINT64_MAX,false>);
 }
 if(high>=16777216ULL&&high<33554432ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<89,UINT64_MAX,false>);
 }
 if(high>=33554432ULL&&high<67108864ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<90,UINT64_MAX,false>);
 }
 if(high>=67108864ULL&&high<134217728ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<91,UINT64_MAX,false>);
 }
 if(high>=134217728ULL&&high<206941441ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<92,UINT64_MAX,false>);
 }
 if(high>=206941441ULL&&high<268435456ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<92,UINT64_MAX,true>);
 }
 if(high>=268435456ULL&&high<292659403ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<93,UINT64_MAX,false>);
 }
 if(high>=292659403ULL&&high<536870912ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<93,UINT64_MAX,true>);
 }
 if(high>=536870912ULL&&high<585318807ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<94,UINT64_MAX,false>);
 }
 if(high>=585318807ULL&&high<1073741824ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<94,UINT64_MAX,true>);
 }
 if(high>=1073741824ULL&&high<1342177280ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<95,UINT64_MAX,false>);
 }
 if(high>=1342177280ULL&&high<2147483648ULL){
  return reinterpret_cast<const void *>(pgn_graph_verify_optimized_width<95,UINT64_MAX,true>);
 }
 return nullptr;
}
static const char *pgn_release_backend_name(uint64_t high) {
    const auto *engine = scout::engineForHigh(high);
    return engine ? engine->name : "optimized_unavailable";
}

constexpr unsigned Capacity = 4096;
static unsigned pgnActiveGapMin = 960;

struct Number128 {
    uint64_t low, high;
};
static bool parseNumber(const std::string &s, Number128 &out) {
    if (s.empty() || s.size() > 39)
        return false;
    uint32_t w[4]{};
    for (char c : s) {
        if (c < '0' || c > '9')
            return false;
        uint64_t carry = unsigned(c - '0');
        for (auto &v : w) {
            const uint64_t p = uint64_t(v) * 10 + carry;
            v = uint32_t(p);
            carry = p >> 32;
        }
        if (carry)
            return false;
    }
    out = {uint64_t(w[0]) | (uint64_t(w[1]) << 32), uint64_t(w[2]) | (uint64_t(w[3]) << 32)};
    return true;
}
static std::string decimal(Number128 n) {
    uint32_t w[4] = {uint32_t(n.low), uint32_t(n.low >> 32), uint32_t(n.high),
                     uint32_t(n.high >> 32)};
    std::string s;
    do {
        uint64_t rem = 0;
        for (int i = 3; i >= 0; --i) {
            const uint64_t p = (rem << 32) | w[i];
            w[i] = uint32_t(p / 10);
            rem = p % 10;
        }
        s.push_back(char('0' + rem));
    } while (w[0] || w[1] || w[2] || w[3]);
    std::reverse(s.begin(), s.end());
    return s;
}
#ifdef __linux__
// Private frontend ownership. Install before ANY CUDA option dispatch; exec
// and parent-death races are closed by checking getppid after PR_SET_PDEATHSIG.
static int pgnInstallOwnerPidGuard(int argc, char **argv) {
    pid_t expected = 0;
    bool supplied = false;
    for (int a = 1; a < argc; ++a) {
        if (std::strcmp(argv[a], "--owner-pid"))
            continue;
        if (supplied || a + 1 >= argc) {
            std::fprintf(stderr, "Invalid private owner PID; no GPU touched\n");
            return 1;
        }
        supplied = true;
        Number128 value{};
        if (!parseNumber(argv[++a], value) || value.high || value.low > 2147483647ULL ||
            value.low == 1) {
            std::fprintf(stderr, "Private owner PID must be0 or2..2147483647; no GPU touched\n");
            return 1;
        }
        expected = static_cast<pid_t>(value.low);
    }
    if (!expected)
        return 0; // Native standalone default: ownership guard disabled.
    if (::signal(SIGTERM, SIG_DFL) == SIG_ERR) {
        std::fprintf(stderr, "Cannot set owner-death signal disposition; no GPU touched\n");
        return 1;
    }
    sigset_t unblocked;
    ::sigemptyset(&unblocked);
    ::sigaddset(&unblocked, SIGTERM);
    if (::sigprocmask(SIG_UNBLOCK, &unblocked, nullptr) != 0) {
        std::fprintf(stderr, "Cannot unblock owner-death signal; no GPU touched\n");
        return 1;
    }
    if (::prctl(PR_SET_PDEATHSIG, static_cast<unsigned long>(SIGTERM), 0UL, 0UL, 0UL) != 0) {
        std::fprintf(stderr, "Cannot install owner-death guard; no GPU touched\n");
        return 1;
    }
    if (::getppid() != expected) {
        std::fprintf(stderr, "Private owner PID no longer matches parent; no GPU touched\n");
        return 1;
    }
    return 0;
}
#endif

static uint64_t observedNs() {
    return pgn_platform::observedNs();
}
struct Checkpoint {
    char magic[8];
    uint64_t low, high, length, positions, tests, gaps, batches;
};
static bool exists(const std::string &file) {
    return pgn_platform::exists(file);
}
#include "checkpoint_publish.h"
static bool writeCheckpoint(const std::string &file, Checkpoint value) {
    static_assert(sizeof(Checkpoint) == 64, "PGNS_GAP32 exact byte contract");
    const auto result = pgn_checkpoint::publish(file, &value, sizeof(value));
    pgn_checkpoint::report(result);
    return result.ok;
}

#ifndef PGN_RELEASE_VERSION
#include "scout_version.h"
#define PGN_RELEASE_VERSION SCOUT_VERSION
#endif
#if __has_include("worker_diagnostics.cuh")
#include "worker_diagnostics.cuh"
#define PGN_RELEASE_HAS_DIAGNOSTIC 1
#define PGN_RELEASE_HAS_DIAGNOSTIC_JSON "true"
#else
#define PGN_RELEASE_HAS_DIAGNOSTIC 0
#define PGN_RELEASE_HAS_DIAGNOSTIC_JSON "false"
#endif
static void printHelp() {
    std::puts(
        "Prime Gap Scout CUDA worker " PGN_RELEASE_VERSION " (local source search; no network PGN)\n"
        "Usage: pgn68-worker --engine walk --start DECIMAL --length DECIMAL [--device 0]\n"
        "       [--gap-min 960] [--emit-min-gap 960] [--state PATH] [--log PATH] [--stop-file "
        "PATH]\n"
        "       --plan --start DECIMAL --length DECIMAL [--device 0] [--gap-min 960]\n"
        "       [--progress SECONDS] [--state-every SECONDS] [--no-records]\n"
        "       --help | --version | --capabilities | --device-info\n"
        "       --self-test [--output-directory PATH]\n"
        "       --fermat-diagnostic [--output-directory PATH]\n"
        "Profile: 67..95-bit inputs (high words4..2147483647), start >=73786976294838206464 and\n"
        "end <=39614081257132168796771975168, plus existing low-window/bitmap halo guards.\n"
        "Actual gap minimum is a runtime parameter,954 or greater. One low64 source window per "
        "Native chunk.\n"
        "One visible GPU, device 0; CUDA sm86 or sm120 with mapped host memory.\n"
        "No clock, power, voltage or fan controls. Diagnostics run before search allocations.");
}
static std::string jsonString(const char *text) {
    std::string out = "\"";
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
        if (*p == '"' || *p == '\\') {
            out.push_back('\\');
            out.push_back(char(*p));
        } else if (*p < 32) {
            char b[7];
            std::snprintf(b, sizeof(b), "\\u%04x", unsigned(*p));
            out += b;
        } else
            out.push_back(char(*p));
    }
    return out + '"';
}
static std::string uuidString(const cudaUUID_t &uuid) {
    static const char hex[] = "0123456789abcdef";
    std::string out = "GPU-";
    for (unsigned i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out += '-';
        const unsigned b = static_cast<unsigned char>(uuid.bytes[i]);
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}
static int printDeviceInfo() {
    int count = 0, runtime = 0, driver = 0;
    CHECK(cudaGetDeviceCount(&count));
    CHECK(cudaRuntimeGetVersion(&runtime));
    CHECK(cudaDriverGetVersion(&driver));
    std::printf("{\"schema\":\"pgn-device-info-1\",\"runtime_version\":%d,\"driver_version\":%d,"
                "\"visible_count\":%d,\"devices\":[",
                runtime, driver, count);
    for (int i = 0; i < count; ++i) {
        cudaDeviceProp p{};
        CHECK(cudaGetDeviceProperties(&p, i));
        const bool supported = (p.major == 8 && p.minor == 6) || (p.major == 12 && p.minor == 0);
        std::printf("%s{\"index\":%d,\"model\":%s,\"uuid\":%s,\"cc_major\":%d,\"cc_minor\":%d,"
                    "\"vram_bytes\":%llu,\"multiprocessors\":%d,\"mapped_host_memory\":%s,\"shared_"
                    "optin_bytes\":%zu,\"supported_cc\":%s}",
                    i ? "," : "", i, jsonString(p.name).c_str(),
                    jsonString(uuidString(p.uuid).c_str()).c_str(), p.major, p.minor,
                    static_cast<unsigned long long>(p.totalGlobalMem), p.multiProcessorCount,
                    p.canMapHostMemory ? "true" : "false",
                    static_cast<size_t>(p.sharedMemPerBlockOptin), supported ? "true" : "false");
    }
    std::puts("]}");
    return 0;
}
static int admitDevice() {
    CHECK(cudaSetDeviceFlags(cudaDeviceMapHost));
    int visible = 0;
    CHECK(cudaGetDeviceCount(&visible));
    if (visible != 1) {
        std::fprintf(stderr, "Exactly one visible GPU is required; select it with "
                             "CUDA_VISIBLE_DEVICES (worker --device 0).\n");
        return 1;
    }
    CHECK(cudaSetDevice(0));
    cudaDeviceProp property{};
    CHECK(cudaGetDeviceProperties(&property, 0));
    const bool supported = (property.major == 8 && property.minor == 6) ||
                           (property.major == 12 && property.minor == 0);
    if (!supported) {
        std::fprintf(stderr,
                     "Unsupported CUDA compute capability %d.%d; this release supports sm86 and "
                     "sm120 only.\n",
                     property.major, property.minor);
        return 1;
    }
#ifdef PGN_RELEASE_BUILD_ARCH
    if (PGN_RELEASE_BUILD_ARCH != 0 &&
        property.major * 10 + property.minor != PGN_RELEASE_BUILD_ARCH) {
        std::fprintf(stderr, "Worker architecture sm%d does not match GPU sm%d%d.\n",
                     PGN_RELEASE_BUILD_ARCH, property.major, property.minor);
        return 1;
    }
#endif
    if (!property.canMapHostMemory || property.maxThreadsPerBlock < 512 ||
        property.sharedMemPerBlockOptin < 99024) {
        std::fprintf(stderr, "GPU requires mapped host memory, 512-thread CTAs and >=99024 bytes "
                             "opt-in shared memory per CTA.\n");
        return 1;
    }
    return 0;
}

struct PgnHostTables {
    std::vector<uint64_t> primes, reciprocals, r64;
    std::vector<uint32_t> item_pidx, item_k0;
    uint32_t wheel_primes[3]{};
    PgnHostTables() {
        constexpr unsigned Depth = 80000;
        std::vector<unsigned char> composite(Depth + 1);
        for (unsigned i = 2; i * i <= Depth; ++i)
            if (!composite[i])
                for (unsigned j = i * i; j <= Depth; j += i)
                    composite[j] = 1;
        for (unsigned p = 7; p <= Depth; p += 2)
            if (!composite[p]) {
                const unsigned index = unsigned(primes.size());
                primes.push_back(p);
                reciprocals.push_back(UINT64_MAX / p);
                r64.push_back((UINT64_MAX % p + 1) % p);
                if (p == 7)
                    wheel_primes[0] = index;
                if (p == 11)
                    wheel_primes[1] = index;
                if (p == 13)
                    wheel_primes[2] = index;
                if (p > PgnPreMax)
                    for (unsigned chunk = 0; chunk < ((1u << 19) / p + 2 + 255) / 256; ++chunk) {
                        item_pidx.push_back(index);
                        item_k0.push_back(chunk);
                    }
            }
    }
};
static uint64_t pgnPreAllocationBytes() {
    uint64_t bytes = 0;
    for (unsigned group = 0; group < PGN_PRESIEVE_GROUPS; ++group)
        bytes += uint64_t((8 * PgnPreProducts[group] + 63) / 64 + 1) * 8;
    return bytes;
}
static pgn_release_plan::Memory pgnMemoryPlan(const pgn_release_plan::Range &range,
                                              const PgnHostTables &tables, uint64_t freeBytes) {
    static_assert(sizeof(PgnWalkDescriptor) == 40 && sizeof(PgnWalkGraphState) == 12 &&
                      sizeof(w_gaprec_t) == 16,
                  "Plan/allocation ABI must agree");
    return pgn_release_plan::memory(range, K, PgnDescriptorLanes, tables.primes.size(),
                                    tables.item_pidx.size(), pgnPreAllocationBytes(),
                                    sizeof(PgnWalkDescriptor), sizeof(PgnWalkGraphState),
                                    sizeof(w_gaprec_t), freeBytes);
}
static void printMemoryPlan(Number128 requested, uint64_t length,
                            const pgn_release_plan::Range &range,
                            const pgn_release_plan::Memory &memory, uint64_t freeBytes,
                            uint64_t totalBytes) {
#ifdef PGN_RELEASE_BUILD_ARCH
    constexpr unsigned buildArch = PGN_RELEASE_BUILD_ARCH;
#else
    constexpr unsigned buildArch = 0;
#endif
    const char *reason = !range.valid  ? range.reason
                         : memory.fits ? "ok"
                                       : "insufficient_free_device_memory";
    std::printf(
        "{\"schema\":\"pgn-memory-plan-1\",\"gap_runtime_schema\":\"PGN_NATIVE_GAP_RUNTIME_V1\",\"optimized_domain_schema\":\"SCOUT_WORD_DOMAINS_V1\",\"optimized_high_begin\":4,\"optimized_high_end\":2147483648,\"word_tail_clipping\":true,\"minimum_gap_floor\":30,\"bounded_scan\":true,"
        "\"gap_min\":%u,\"arithmetic_backend\":\"%s\",\"profile_k\":%u,\"build_arch\":%u,\"range_"
        "valid\":%s,\"fits\":%s,\"reason\":%s,\"start\":%s,\"length\":%s,\"high_word\":%llu,\"base_"
        "low\":%llu,\"floor_remainder\":%llu,\"blocks\":%llu,\"descriptor_count\":%llu,\"bitmap_"
        "bytes\":%llu,\"result_bytes\":%llu,\"proofs_bytes\":%llu,\"descriptor_bytes\":%llu,"
        "\"other_device_bytes\":%llu,\"device_allocation_bytes\":%llu,\"margin_bytes\":%llu,"
        "\"required_device_bytes\":%llu,\"free_device_bytes\":%llu,\"total_device_bytes\":%llu,"
        "\"mapped_host_bytes\":%llu,\"max_descriptor_count_fit\":%llu,\"max_length_fit\":%llu}\n",
        pgnActiveGapMin, pgn_release_backend_name(requested.high), K, buildArch,
        range.valid ? "true" : "false", memory.fits ? "true" : "false", jsonString(reason).c_str(),
        jsonString(decimal(requested).c_str()).c_str(),
        jsonString(decimal({length, 0}).c_str()).c_str(), (unsigned long long)requested.high,
        (unsigned long long)range.base, (unsigned long long)range.remainder,
        (unsigned long long)range.blocks, (unsigned long long)range.count,
        (unsigned long long)memory.bitmap, (unsigned long long)memory.results,
        (unsigned long long)memory.proofs, (unsigned long long)memory.descriptors,
        (unsigned long long)memory.other, (unsigned long long)memory.allocation,
        (unsigned long long)pgn_release_plan::Margin, (unsigned long long)memory.required,
        (unsigned long long)freeBytes, (unsigned long long)totalBytes,
        (unsigned long long)memory.mapped, (unsigned long long)memory.maxDescriptors,
        (unsigned long long)memory.maxLength);
}

static void printCapabilities() {
    std::fputs(
        "{\"schema\":\"pgn-engine-module-1\",\"id\":\"cuda-continuous-source-v1\",\"protocol\":"
        "\"phase0-source-1\",\"host_refill\":false,\"host_verification\":true,\"whole_job_gpu_"
        "owned\":false,\"cuda_arch\":[\"sm86\",\"sm120\"],\"mono_gpu\":true,\"network_pgn\":false,"
        "\"prime_certificate\":\"GPU MR13 screening; exact FLINT endpoint proof above "
        "deterministic MR13 "
        "bound\",\"coverage\":\"SOURCE_PRP_INDEPENDENT_PROOF_PENDING\",\"single_low64_window\":"
        "true,\"supported_high_word_range\":[4,2147483647],\"bits_min\":67,\"bits_max\":95,\"gap_"
        "min\":960,\"gap_min_floor\":954,\"whole70_state_bytes\":8,\"whole71_state_bytes\":8,\"gap_"
        "min_dynamic\":true,\"checkpoint_schema\":\"PGNS_GAP32_64B\",\"memory_plan\":true,\"max_"
        "source_blocks\":4194304,\"block_numbers\":1006632960,\"emit_min_gap_default\":960",
        stdout);
    std::printf(",\"profile_k\":%u,\"self_test_compiled\":" PGN_RELEASE_HAS_DIAGNOSTIC_JSON
                ",\"optimized_literal_high_words\":[",
                K);
    bool first = true;
    for (const auto &e : scout::Engines) {
        if (!first)
            std::fputc(',', stdout);
        first = false;
        std::printf("%llu", (unsigned long long)e.highFirst);
    }
    std::fputs("],\"retained_points\":[", stdout);
    first = true;
    for (const auto &e : scout::Engines) {
        if (!first)
            std::fputc(',', stdout);
        first = false;
        std::printf("{\"bits\":%u,\"high\":%llu,\"auto_gap_min\":%u,\"state_bytes\":%u}", e.bits,
                    (unsigned long long)e.highFirst, e.automaticGap, e.stateBytes);
    }
    std::fputs("],\"optimized_coverage_complete\":false,\"optimized_high_domains\":["
               "{\"bits\":67,\"first\":4,\"end\":8},"
               "{\"bits\":68,\"first\":8,\"end\":16}] }\n", stdout);
}
static int pgnWorkerMain(int argc, char **argv) {
    pgnActiveGapMin = 960; // Every command starts from the documented default.
#ifdef __linux__
    const int ownerGuard = pgnInstallOwnerPidGuard(argc, argv);
    if (ownerGuard)
        return ownerGuard;
#endif
    std::string startText, lengthText, stopFile, stateFile, logFile, diagnosticOutput = ".";
    unsigned gapMin = 960, emitMinGap = 960;
    bool diagnostic = false, diagnosticBenchmark = false, planOnly = false;
    for (int a = 1; a < argc; ++a) {
        const std::string option = argv[a];
        if (option == "--help" || option == "-h") {
            printHelp();
            return 0;
        }
        if (option == "--version") {
            std::puts("pgn68-worker " PGN_RELEASE_VERSION);
            return 0;
        }
        if (option == "--device-info")
            return printDeviceInfo();
        if (option == "--plan") {
            planOnly = true;
            continue;
        }
        if (option == "--capabilities") {
            printCapabilities();
            return 0;
        }
        if (option == "--self-test" || option == "--fermat-diagnostic") {
            if (diagnostic) {
                std::fprintf(stderr, "Select one diagnostic mode.\n");
                return 1;
            }
            diagnostic = true;
            diagnosticBenchmark = option == "--fermat-diagnostic";
            continue;
        }
        if (option == "--no-records")
            continue;
        if (a + 1 >= argc) {
            std::fprintf(stderr, "Missing value for %s\n", option.c_str());
            return 1;
        }
        const std::string value = argv[++a];
        if (option == "--output-directory")
            diagnosticOutput = value;
        else if (option == "--start")
            startText = value;
        else if (option == "--length")
            lengthText = value;
        else if (option == "--stop-file")
            stopFile = value;
        else if (option == "--state")
            stateFile = value;
        else if (option == "--log")
            logFile = value;
#ifdef __linux__
        else if (option == "--owner-pid") {
            continue;
        } // Validated by pre-CUDA ownership pass.
#endif
        else if (option == "--gap-min") {
            Number128 minimum{};
            if (!parseNumber(value, minimum) || minimum.high || minimum.low < 30 ||
                minimum.low > 2147483647ULL) {
                std::fprintf(stderr, "gap-min must be an integer30..2147483647\n");
                return 1;
            }
            gapMin = unsigned(minimum.low);
            emitMinGap = gapMin;
            pgnActiveGapMin = gapMin;
        } else if (option == "--emit-min-gap") {
            Number128 minimum{};
            if (!parseNumber(value, minimum) || minimum.high || minimum.low < 30 ||
                minimum.low > UINT32_MAX) {
                std::fprintf(stderr, "emit-min-gap must be an integer30..4294967295\n");
                return 1;
            }
            emitMinGap = unsigned(minimum.low);
        } else if (option == "--engine") {
            if (value != "walk")
                return 1;
        } else if (option == "--device") {
            if (value != "0")
                return 1;
        } else if (option == "--progress" || option == "--state-every") {
            char *end = nullptr;
            const double n = std::strtod(value.c_str(), &end);
            if (!end || *end || !(n > 0))
                return 1;
        } else {
            std::fprintf(stderr, "Unsupported engine option %s\n", option.c_str());
            return 1;
        }
    }
    if (diagnostic) {
        if (planOnly) {
            std::fprintf(stderr, "Diagnostics cannot be combined with --plan.\n");
            return 1;
        }
        if (!startText.empty() || !lengthText.empty() || !stateFile.empty() || !logFile.empty() ||
            !stopFile.empty()) {
            std::fprintf(stderr, "Diagnostics cannot be combined with a search job.\n");
            return 1;
        }
        const int admitted = admitDevice();
        if (admitted)
            return admitted;
#if PGN_RELEASE_HAS_DIAGNOSTIC
        return pgn_release_diagnostic(0, diagnosticOutput.c_str(), diagnosticBenchmark);
#else
        std::fprintf(stderr, "Diagnostic implementation is absent from this build; rebuild with "
                             "worker_diagnostics.cuh.\n");
        return 2;
#endif
    }
    Number128 requested{}, len{};
    if (!parseNumber(startText, requested) || !parseNumber(lengthText, len) || len.high) {
        std::fprintf(stderr, "Invalid decimal source interval\n");
        return 1;
    }
    const uint64_t length = len.low;
    if (!scout::engineForHigh(requested.high)) {
        std::fprintf(stderr, "No validated optimized engine for this interval; no GPU work started. "
                             "The saved range and checkpoint are retained.\n");
        return 7;
    }
    const auto originalRange = pgn_release_plan::range(requested.low, requested.high, length, K);
    if (!originalRange.valid) {
        if (planOnly)
            printMemoryPlan(requested, length, originalRange, {}, 0, 0);
        else
            std::fprintf(stderr, "Invalid source interval: %s\n", originalRange.reason);
        return 1;
    }
    PgnHostTables hostTables;
    if (hostTables.primes.size() != 7834) {
        std::fprintf(stderr, "Rollingroots requires exact7834prime table\n");
        return 1;
    }
    if (planOnly) {
        if (!stateFile.empty() || !logFile.empty() || !stopFile.empty()) {
            std::fprintf(stderr, "--plan cannot mutate or use search checkpoint/log/stop files.\n");
            return 1;
        }
        const int admitted = admitDevice();
        if (admitted)
            return admitted;
        size_t freeBytes = 0, totalBytes = 0;
        CHECK(cudaMemGetInfo(&freeBytes, &totalBytes));
        printMemoryPlan(requested, length, originalRange,
                        pgnMemoryPlan(originalRange, hostTables, freeBytes), freeBytes, totalBytes);
        return 0;
    }
    std::puts("[pgn-smallword7] primes=29,31,37,41,43,47,53 table_bytes=2248 all_marks_retained=1 "
              "original_item_skip_retained=1");
    Checkpoint saved{{'P', 'G', 'N', 'S', char(gapMin), char(gapMin >> 8), char(gapMin >> 16),
                      char(gapMin >> 24)},
                     requested.low,
                     requested.high,
                     length,
                     0,
                     0,
                     0,
                     0};
    if (exists(stateFile)) {
        std::ifstream input(std::filesystem::u8path(stateFile), std::ios::binary);
        Checkpoint prior{};
        input.read(reinterpret_cast<char *>(&prior), sizeof(prior));
        if (!input || std::memcmp(prior.magic, saved.magic, 8) || prior.low != saved.low ||
            prior.high != saved.high || prior.length != length || prior.positions >= length) {
            std::fprintf(stderr, "Checkpoint identity incompatible; preserved, no restart\n");
            return 1;
        }
        saved = prior;
    }
    const uint64_t remaining = length - saved.positions,
                   resumeLow = requested.low + saved.positions;
    const auto resumedRange = pgn_release_plan::range(resumeLow, requested.high, remaining, K);
    if (!resumedRange.valid) {
        std::fprintf(stderr, "Invalid resumed interval: %s\n", resumedRange.reason);
        return 1;
    }
    const uint64_t remainder = resumedRange.remainder, baseLow = resumedRange.base,
                   blocks = resumedRange.blocks;
    const unsigned Count = unsigned(resumedRange.count);
    const int admitted = admitDevice();
    if (admitted)
        return admitted;
    size_t freeBytes = 0, totalBytes = 0;
    CHECK(cudaMemGetInfo(&freeBytes, &totalBytes));
    const auto memoryPlan = pgnMemoryPlan(resumedRange, hostTables, freeBytes);
    if (!memoryPlan.fits) {
        printMemoryPlan({resumeLow, requested.high}, remaining, resumedRange, memoryPlan, freeBytes,
                        totalBytes);
        std::fprintf(stderr,
                     "Required GPU bytes=%llu, free=%llu, total=%llu, K=%u, descriptors=%u; no "
                     "search allocation or job launched\n",
                     (unsigned long long)memoryPlan.required, (unsigned long long)freeBytes,
                     (unsigned long long)totalBytes, K, Count);
        return 1;
    }
    scout::ExactPrimeProof exactProof;
    if (scout::needsEndpointProof(requested.low, requested.high))
        exactProof.initialize();
    std::printf("[pgn-selected-backend] family=%s high=%llu gap_min=%u profile_k=%u "
                "coverage=67_95_all_high_words\n",
                pgn_release_backend_name(requested.high), (unsigned long long)requested.high,
                gapMin, K);
    PgnWalkGraphBuffers buffers{};
    buffers.gap_min = gapMin;
    buffers.result_capacity = Capacity;
    buffers.blocks_per_slot = SlotBlocks;
    const unsigned GapMin = gapMin;
    auto &primes = hostTables.primes;
    auto &reciprocals = hostTables.reciprocals;
    auto &r64 = hostTables.r64;
    auto &item_pidx = hostTables.item_pidx;
    auto &item_k0 = hostTables.item_k0;
    auto &wheel_primes = hostTables.wheel_primes;
    const uint32_t classes[8] = {1, 7, 11, 13, 17, 19, 23, 29};
    int8_t class_index[30];
    std::memset(class_index, -1, sizeof(class_index));
    uint32_t permutation[30]{}, inverse[30]{}, steps[8]{};
    for (unsigned j = 0; j < 8; ++j)
        class_index[classes[j]] = int8_t(j);
    for (unsigned p = 0; p < 30; ++p)
        if (class_index[p] >= 0) {
            for (unsigned j = 0; j < 8; ++j)
                permutation[p] |= 1u << ((p * classes[j]) % 30);
            for (unsigned x = 1; x < 30; ++x)
                if ((p * x) % 30 == 1)
                    inverse[p] = x;
        }
    scout_wheel::window_steps(GapMin,classes,class_index,steps);
    CHECK(cudaMemcpyToSymbol(c30_clsidx, class_index, sizeof(class_index)));
    CHECK(cudaMemcpyToSymbol(c30_perm, permutation, sizeof(permutation)));
    CHECK(cudaMemcpyToSymbol(c30_pinv, inverse, sizeof(inverse)));

    CHECK(cudaFuncSetAttribute(pgn_graph_mark, cudaFuncAttributeMaxDynamicSharedMemorySize, 65536));
    // Give all graph kernels the same shared/L1 preference.
    CHECK(
        cudaFuncSetAttribute(pgn_graph_mark, cudaFuncAttributePreferredSharedMemoryCarveout, 100));
    const void *selectedWalk = selectWalkKernel(requested.high,resumedRange.tailClipped);
    const void *selectedVerify = selectVerifyKernel(requested.high);
    CHECK(cudaFuncSetAttribute(selectedWalk, cudaFuncAttributePreferredSharedMemoryCarveout, 100));
    CHECK(
        cudaFuncSetAttribute(selectedVerify, cudaFuncAttributePreferredSharedMemoryCarveout, 100));
    CHECK(cudaFuncSetAttribute(pgn_graph_advance, cudaFuncAttributePreferredSharedMemoryCarveout,
                               100));

#if PGN_PRESIEVE_GROUPS > 0
    const uint64_t *prePointers[5]{};
    for (unsigned group = 0; group < PGN_PRESIEVE_GROUPS; ++group) {
        const auto table = pgnMakePreTable(group);
        uint64_t *deviceTable = nullptr;
        CHECK(cudaMalloc(&deviceTable, table.size() * 8));
        CHECK(cudaMemcpy(deviceTable, table.data(), table.size() * 8, cudaMemcpyHostToDevice));
        prePointers[group] = deviceTable;
    }
    CHECK(cudaMemcpyToSymbol(pgn_pre_tables, prePointers, sizeof(prePointers)));
#endif
    std::vector<PgnWalkDescriptor> descriptors(Count);
    for (unsigned i = 0; i < Count; ++i) {
        const uint64_t first = uint64_t(i) * K;
        const unsigned owned = unsigned(std::min<uint64_t>(K, blocks - first));
        descriptors[i] = {baseLow + first * BlockNumbers, requested.high,
                          owned + (first + owned < blocks ? 1u : 0u), owned};
        auto &descriptor=descriptors[i];
        const uint64_t span=uint64_t(descriptor.bitmap_blocks)*BlockNumbers;
        if(span>=(1ULL<<36)||!span)return 2;
        const auto last=span-1>UINT64_MAX-descriptor.low?UINT64_MAX:descriptor.low+span-1;
        const auto prepared=scout_arithmetic::prepare_domain(last,uint32_t(requested.high));
        descriptor.arithmetic_mu=prepared.muLower;descriptor.arithmetic_coefficient=prepared.coefficient;
        const auto requestedLast=requested.low+(length-1);
        if(descriptor.low>requestedLast)return 2;
        descriptor.scan_slots=scout_extent::candidate_slots(requestedLast-descriptor.low,uint64_t(descriptor.bitmap_blocks)<<28,classes);
    }
    uint64_t *d_primes{}, *d_reciprocals{}, *d_r64{};
    uint32_t *d_items{}, *d_chunks{}, *d_wheel{}, *d_steps{};
    if (primes.size() != 7834) {
        std::fprintf(stderr, "Rollingroots requires exact7834prime table\n");
        return 1;
    }
    std::vector<uint32_t> rootSteps;
    rootSteps.reserve(primes.size());
    for (uint64_t p : primes)
        rootSteps.push_back(uint32_t((1ULL * ((1ULL << 19) / 8) * 30) % p));
    uint32_t *d_rootSteps{};
    CHECK(cudaMalloc(&d_rootSteps, rootSteps.size() * sizeof(uint32_t)));
    CHECK(cudaMemcpy(d_rootSteps, rootSteps.data(), rootSteps.size() * sizeof(uint32_t),
                     cudaMemcpyHostToDevice));
    buffers.root_steps = d_rootSteps;

    PgnWalkDescriptor *d_descriptors{};
    PgnWalkGraphState *d_state{};
    unsigned *d_visits{};
    unsigned *h_flags{}, *d_flags{};
    w_gaprec_t *h_results{};
    unsigned long long *h_stats{};
    PgnWalkMailbox mailbox{};
    const uint64_t resultBytes = uint64_t(Count) * Capacity * sizeof(w_gaprec_t),
                   bitmapBytes = uint64_t(PgnBitmapSlots) * SlotBlocks * Words * 8;
    if (bitmapBytes != memoryPlan.bitmap || resultBytes != memoryPlan.results) {
        std::fprintf(stderr,
                     "Memory ledger disagrees with source allocation sizes; no job launched\n");
        return 1;
    }
    CHECK(cudaMalloc(&d_primes, primes.size() * 8));
    CHECK(cudaMalloc(&d_reciprocals, primes.size() * 8));
    CHECK(cudaMalloc(&d_r64, r64.size() * 8));
    CHECK(cudaMalloc(&d_items, item_pidx.size() * 4));
    CHECK(cudaMalloc(&d_chunks, item_k0.size() * 4));
    CHECK(cudaMalloc(&d_wheel, sizeof(wheel_primes)));
    CHECK(cudaMalloc(&d_steps, sizeof(steps)));
    CHECK(cudaMalloc(&buffers.bitmaps, bitmapBytes));
    CHECK(cudaMalloc(&buffers.results, resultBytes));
    CHECK(cudaMalloc(&buffers.stats, uint64_t(Count) * 16));
    CHECK(cudaMalloc(&buffers.proofs, uint64_t(Count) * Capacity * 4));
    CHECK(cudaMalloc(&d_descriptors, uint64_t(Count) * sizeof(PgnWalkDescriptor)));
    CHECK(cudaMalloc(&d_state, PgnDescriptorLanes * sizeof(PgnWalkGraphState)));
    CHECK(cudaMalloc(&d_visits, uint64_t(Count) * 4));
    CHECK(
        cudaHostAlloc(&h_flags, uint64_t(Count + PgnDescriptorLanes + 1) * 4, cudaHostAllocMapped));
    CHECK(cudaHostAlloc(&h_results, resultBytes, cudaHostAllocMapped));
    CHECK(cudaHostAlloc(&h_stats, uint64_t(Count) * 16, cudaHostAllocMapped));
    CHECK(cudaHostGetDevicePointer(&d_flags, h_flags, 0));
    CHECK(cudaHostGetDevicePointer(&mailbox.results, h_results, 0));
    CHECK(cudaHostGetDevicePointer(&mailbox.stats, h_stats, 0));
    mailbox.ready = d_flags;
    mailbox.stop = d_flags + Count;
    mailbox.done = d_flags + Count + 1;
    CHECK(cudaMemcpy(d_primes, primes.data(), primes.size() * 8, cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(d_reciprocals, reciprocals.data(), primes.size() * 8, cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(d_r64, r64.data(), r64.size() * 8, cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(d_items, item_pidx.data(), item_pidx.size() * 4, cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(d_chunks, item_k0.data(), item_k0.size() * 4, cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(d_wheel, wheel_primes, sizeof(wheel_primes), cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(d_steps, steps, sizeof(steps), cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(d_descriptors, descriptors.data(), uint64_t(Count) * sizeof(PgnWalkDescriptor),
                     cudaMemcpyHostToDevice));
    buffers.primes = d_primes;
    buffers.invp = d_reciprocals;
    buffers.r64 = d_r64;
    buffers.item_pidx = d_items;
    buffers.item_k0 = d_chunks;
    buffers.wheel_primes = d_wheel;
    buffers.window_steps = d_steps;
    buffers.item_count = unsigned(item_pidx.size());
    CHECK(cudaMemset(buffers.bitmaps, 0, bitmapBytes));
    CHECK(cudaMemset(buffers.results, 0, resultBytes));
    CHECK(cudaMemset(buffers.stats, 0, uint64_t(Count) * 16));
    CHECK(cudaMemset(buffers.proofs, 0, uint64_t(Count) * Capacity * 4));
    CHECK(cudaMemset(d_visits, 0, uint64_t(Count) * 4));
    PgnWalkGraphState initial[PgnDescriptorLanes]{};
    for (unsigned lane = 0; lane < PgnDescriptorLanes; ++lane)
        initial[lane] = {lane, Count, 0};
    CHECK(cudaMemcpy(d_state, initial, sizeof(initial), cudaMemcpyHostToDevice));
    std::memset(h_flags, 0, uint64_t(Count + PgnDescriptorLanes + 1) * 4);
    for (unsigned lane = Count; lane < PgnDescriptorLanes; ++lane)
        h_flags[Count + 1 + lane] = 1u;
    std::memset(h_results, 0, resultBytes);
    std::memset(h_stats, 0, uint64_t(Count) * 16);

    int markerDevice = 0, markerSms = 0;
    CHECK(cudaGetDevice(&markerDevice));
    CHECK(cudaDeviceGetAttribute(&markerSms, cudaDevAttrMultiProcessorCount, markerDevice));
    int sharedBlockLimit = 0;
    CHECK(cudaDeviceGetAttribute(&sharedBlockLimit, cudaDevAttrMaxSharedMemoryPerBlockOptin,
                                 markerDevice));
    cudaFuncAttributes markerAttributes{}, walkerAttributes{};
    CHECK(cudaFuncGetAttributes(&markerAttributes, pgn_graph_mark));
    CHECK(cudaFuncGetAttributes(&walkerAttributes, selectedWalk));
    std::printf("[pgn-frontier-carry] threshold=4096 one_item_prime_next_position_phase=1 "
                "retained_small_prime_residues=1\n");
    std::printf(
        "[pgn-contiguous-sieve-cohorts] physical_ctas=%d logical_ctas_per_full_descriptor=%u "
        "cohorts_per_bitmap=16 tiles_per_cohort=32 marker_registers=%d walker_registers=%d "
        "static_shared=%zu dynamic_shared=65536 tile_coverage_unchanged=1\n",
        markerSms, 16u * SlotBlocks, markerAttributes.numRegs, walkerAttributes.numRegs,
        markerAttributes.sharedSizeBytes);
    // A compiler may reserve additional per-block shared storage on a newer
    // architecture. Admission depends on the actual function attributes and
    // device limit, rather than one architecture's exact static byte count.
    int walkerBlocks=0;
    CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(&walkerBlocks,selectedWalk,512,0));
    std::printf("[pgn-walker-resource-admission] registers=%d threads=512 resident_blocks_per_SM=%d\n",walkerAttributes.numRegs,walkerBlocks);
    if (markerSms < 1 || markerAttributes.numRegs > 48 || walkerBlocks < 1 ||
        sharedBlockLimit <= 0 ||
        markerAttributes.sharedSizeBytes + 65536 > size_t(sharedBlockLimit)) {
        std::fprintf(stderr,
                     "Search kernels exceed the device's register/shared-memory admission: "
                     "static=%zu dynamic=65536 block_limit=%d; no launch\n",
                     markerAttributes.sharedSizeBytes, sharedBlockLimit);
        return 2;
    }
    uint32_t *event210Allocation = nullptr;
    const std::vector<uint64_t> originalPrefixPrimes(primes.begin(), primes.begin() + 4200);
    CHECK(pgn_event210::upload(originalPrefixPrimes, &event210Allocation));
    std::puts("[pgn-dense-pair16] primes_per_warp=2 lanes_per_prime=16 events_per_prime=48 "
              "all96events_retained=1 complete_cycle_atomic_warp_instructions=3_before4=1");
    std::puts(
        "[pgn-event210-hybrid] useful_events_only=1 bitmap_class30_unchanged=1 "
        "independent_wheel7_witness=1 prefix_bytes=1612800 frontier_bits=23 SOURCE_unchanged=1");
    cudaGraph_t graph{};
    cudaGraphExec_t executable{};
    CHECK(cudaGraphCreate(&graph, 0));
    unsigned zero = 0, ahead = PgnDescriptorLanes;
    PgnWalkMailbox mailboxes[PgnDescriptorLanes]{};
    for (auto &laneMailbox : mailboxes)
        laneMailbox = mailbox;
    for (unsigned lane = 0; lane < PgnDescriptorLanes; ++lane) {
        auto *laneState = d_state + lane;
        mailboxes[lane].done = d_flags + Count + 1 + lane;
        cudaGraphConditionalHandle handle{};
        CHECK(cudaGraphConditionalHandleCreate(&handle, graph, lane < Count ? 1u : 0u,
                                               cudaGraphCondAssignDefault));
        auto mark = [&](cudaGraph_t target, unsigned *distance, cudaGraphNode_t *node) {
            void *args[] = {&d_descriptors, &laneState, &buffers, distance};
            cudaGraphNodeParams params{};
            params.type = cudaGraphNodeTypeKernel;
            params.kernel.func = reinterpret_cast<void *>(pgn_graph_mark);
            params.kernel.gridDim = dim3(markerSms);
            params.kernel.blockDim = dim3(512);
            params.kernel.sharedMemBytes = 65536;
            params.kernel.kernelParams = args;
            return cudaGraphAddNode(node, target, nullptr, 0, &params);
        };
        cudaGraphNode_t first{}, loopNode{}, walk{}, proof{}, nextMark{}, advance{};
        CHECK(mark(graph, &zero, &first));
        cudaGraphNodeParams loop{};
        loop.type = cudaGraphNodeTypeConditional;
        loop.conditional.handle = handle;
        loop.conditional.type = cudaGraphCondTypeWhile;
        loop.conditional.size = 1;
        CHECK(cudaGraphAddNode(&loopNode, graph, &first, 1, &loop));
        const auto body = loop.conditional.phGraph_out[0];
        void *args[] = {&d_descriptors, &laneState, &buffers, &zero};
        cudaGraphNodeParams work{};
        work.type = cudaGraphNodeTypeKernel;
        work.kernel.func = const_cast<void *>(selectedWalk);
        work.kernel.gridDim = dim3(2u * unsigned(markerSms));
        work.kernel.blockDim = dim3(512);
        work.kernel.kernelParams = args;
        CHECK(cudaGraphAddNode(&walk, body, nullptr, 0, &work));
        work.kernel.func = const_cast<void *>(selectedVerify);
        work.kernel.gridDim = dim3(16);
        work.kernel.blockDim = dim3(64);
        CHECK(cudaGraphAddNode(&proof, body, &walk, 1, &work));
        // The future writer owns another bitmap version. Its write may overlap
        // this walk; advance still joins proof and future readiness. The previous
        // iteration's completed walk freed the version now reused by this writer.
        CHECK(mark(body, &ahead, &nextMark));
        const cudaGraphNode_t dependencies[] = {nextMark, proof};
        void *advanceArgs[] = {&laneState, &d_visits, &buffers, &mailboxes[lane], &handle};
        work.kernel.func = reinterpret_cast<void *>(pgn_graph_advance);
        work.kernel.gridDim = dim3(1);
        work.kernel.blockDim = dim3(1);
        work.kernel.kernelParams = advanceArgs;
        CHECK(cudaGraphAddNode(&advance, body, dependencies, 2, &work));
    }
    std::printf("[pgn-readwrite-ring] lanes=%u bitmap_slots=%u bitmap_bytes=%llu "
                "reader_writer_disjoint=1 logical_slices_unchanged=1\n",
                PgnDescriptorLanes, PgnBitmapSlots, (unsigned long long)bitmapBytes);
    CHECK(cudaGraphInstantiate(&executable, graph, nullptr, nullptr, 0));
    cudaStream_t stream{};
    CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
    cudaEvent_t began{}, ended{};
    CHECK(cudaEventCreate(&began));
    CHECK(cudaEventCreate(&ended));
    CHECK(cudaDeviceSynchronize());
    std::printf(
        "[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=%u original_start=%s length=%llu "
        "proof=EXACT_ENDPOINTS_GPU_INTERIOR coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING\n",
        Count, startText.c_str(), (unsigned long long)length);
    std::ofstream gapLog;
    if (!logFile.empty()) {
        gapLog.open(std::filesystem::u8path(logFile), std::ios::app);
        if (!gapLog) {
            std::fprintf(stderr, "Source log cannot open; no launch\n");
            return 1;
        }
    }
    CHECK(cudaEventRecord(began, stream));
    CHECK(cudaGraphLaunch(executable, stream));
    CHECK(cudaEventRecord(ended, stream));
    const auto submitted = cudaStreamQuery(stream);
    if (submitted != cudaSuccess && submitted != cudaErrorNotReady) {
        std::fprintf(stderr, "Initial dispatch failed\n");
        return 2;
    }
    unsigned next = 0;
    uint64_t tests = saved.tests, gaps = saved.gaps, emittedGaps = 0, batches = saved.batches,
             positions = saved.positions, lastSaved = observedNs();
    bool stop = false;
    volatile unsigned *flags = h_flags;
    // The GPU publishes immutable completed descriptors. Host consumption proves
    // rare endpoints above the MR13 bound before crediting a durable prefix.
    for (;;) {
        pgn_platform::memoryBarrier();
        while (next < Count && flags[next]) {
            pgn_platform::memoryBarrier();
            const auto *rows = h_results + uint64_t(next) * Capacity;
            const unsigned count = rows[0].gap;
            if (count >= Capacity)
                return 4;
            const auto d = descriptors[next];
            const uint64_t ownedLength = uint64_t(d.owned_blocks) * BlockNumbers;
            // Validate the complete descriptor before emitting any of its results or
            // advancing its durable prefix. A screen positive is never a proof above
            // the deterministic MR13 bound; rejection leaves this prefix uncredited.
            for (unsigned row = 1; row <= count; ++row) {
                const auto g = rows[row];
                if (g.slot < resumeLow || g.slot < d.low || g.slot - d.low >= ownedLength ||
                    g.slot < requested.low || g.slot - requested.low >= length ||
                    uint64_t(g.gap) >= length - (g.slot - requested.low))
                    continue;
                if (scout::needsEndpointProof(g.slot, requested.high) ||
                    scout::needsEndpointProof(g.slot + g.gap, requested.high)) {
                    if (!exactProof.prime(decimal({g.slot, requested.high})) ||
                        !exactProof.prime(decimal({g.slot + g.gap, requested.high}))) {
                        std::fprintf(stderr, "Exact endpoint proof rejected a candidate; "
                                             "descriptor prefix not credited\n");
                        return 8;
                    }
                }
            }
            for (unsigned row = 1; row <= count; ++row) {
                const auto g = rows[row];
                if (g.slot < resumeLow || g.slot < d.low || g.slot - d.low >= ownedLength ||
                    g.slot < requested.low || g.slot - requested.low >= length ||
                    uint64_t(g.gap) >= length - (g.slot - requested.low))
                    continue;
                ++gaps;
                if (g.gap < emitMinGap)
                    continue;
                const std::string p = decimal({g.slot, requested.high}),
                                  q = decimal({g.slot + g.gap, requested.high});
                const double merit =
                    double(g.gap) /
                    std::log(double(requested.high) * 18446744073709551616.0 + double(g.slot));
                char line[400];
                std::snprintf(line, sizeof(line),
                              "[phase0-gpu] GAP merit=%.4f gap=%u lower=%s upper=%s verified=1 "
                              "proof=EXACT_ENDPOINTS_EULER_COMPOSITE record=off table=unknown\n",
                              merit, g.gap, p.c_str(), q.c_str());
                std::fputs(line, stdout);
                if (gapLog)
                    gapLog << line;
                ++emittedGaps;
            }
            tests += h_stats[2 * next];
            ++batches;
            ++next;
            positions =
                saved.positions +
                std::min<uint64_t>(remaining, uint64_t(next) * K * BlockNumbers - remainder);
            std::printf("[pgn-counters] time_ns=%llu positions=%llu gaps=%llu tests=%llu "
                        "batches=%llu processed_gaps=%llu emitted_gaps=%llu\n",
                        (unsigned long long)observedNs(), (unsigned long long)positions,
                        (unsigned long long)gaps, (unsigned long long)tests,
                        (unsigned long long)batches, (unsigned long long)gaps,
                        (unsigned long long)emittedGaps);
            if (observedNs() - lastSaved > 30000000000ULL) {
                auto checkpoint = saved;
                checkpoint.positions = positions;
                checkpoint.tests = tests;
                checkpoint.gaps = gaps;
                checkpoint.batches = batches;
                if (!writeCheckpoint(stateFile, checkpoint))
                    return 5;
                lastSaved = observedNs();
            }
        }
        bool allDone = true, failed = false;
        for (unsigned lane = 0; lane < PgnDescriptorLanes; ++lane) {
            const unsigned done = flags[Count + 1 + lane];
            allDone = allDone && done;
            failed = failed || done > 1u;
        }
        if (failed && !stop) {
            pgn_platform::memoryBarrier();
            flags[Count] = 1u;
            pgn_platform::memoryBarrier();
            stop = true;
        }
        if (allDone) {
            pgn_platform::memoryBarrier();
            if (next < Count && flags[next])
                continue;
            break;
        }
        if (!stop && exists(stopFile)) {
            pgn_platform::memoryBarrier();
            flags[Count] = 1;
            pgn_platform::memoryBarrier();
            stop = true;
        }
        pgn_platform::sleepMs(1);
    }
    CHECK(cudaEventSynchronize(ended));
    float gpuMilliseconds = 0;
    CHECK(cudaEventElapsedTime(&gpuMilliseconds, began, ended));
    std::printf("[pgn-gpu-service] lanes=%u descriptor_blocks=%u completed_positions=%llu "
                "elapsed_ms=%.6f scope=WHOLE_FINITE_GPU_GRAPH_NOT_CPU_PUBLICATION\n",
                PgnDescriptorLanes, K, (unsigned long long)(positions - saved.positions),
                gpuMilliseconds);
    for (unsigned lane = 0; lane < PgnDescriptorLanes; ++lane)
        if (flags[Count + 1 + lane] != 1) {
            std::fprintf(stderr, "GPU lane%u failed closed: status%u, accepted prefix%u\n", lane,
                         flags[Count + 1 + lane], next);
            return 6;
        }
    if (next < Count) {
        auto checkpoint = saved;
        checkpoint.positions = positions;
        checkpoint.tests = tests;
        checkpoint.gaps = gaps;
        checkpoint.batches = batches;
        if (!writeCheckpoint(stateFile, checkpoint))
            return 5;
        std::printf("[phase0-gpu] stopped positions=%llu checkpoint=%s\n",
                    (unsigned long long)positions, stateFile.c_str());
        return 0;
    }
    if (positions != length) {
        std::fprintf(stderr, "Exact final positions mismatch\n");
        return 7;
    }
    const auto archiveResult = pgn_checkpoint::archive(stateFile);
    pgn_checkpoint::report(archiveResult);
    if (!archiveResult.ok)
        return 5;
    std::printf("[phase0-gpu] gaps reported=%llu verification_failures=0 "
                "records_new=0\n[phase0-gpu] --- end ---\n",
                (unsigned long long)gaps);
    return 0;
}

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Runtime K is selected by the shipped profile filename, with one exact code body.
    {
        std::string name = argc ? argv[0] : "";
        const auto kpos = name.rfind("-k");
        if (kpos != std::string::npos) {
            const char *p = name.c_str() + kpos + 2;
            char *end = nullptr;
            const unsigned value = unsigned(std::strtoul(p, &end, 10));
            if ((value == 8 || value == 16 || value == 32 || value == 64) && end &&
                (*end == 0 || std::string(end) == ".exe")) {
                K = value;
                SlotBlocks = K + 1;
            }
        }
    }
    try {
        std::string directory, nonce;
        std::vector<std::string> current;
        current.reserve(argc);
        for (int index = 0; index < argc; ++index) {
            const std::string option = argv[index];
            if (index && (option == "--resident-directory" || option == "--resident-nonce")) {
                if (++index >= argc)
                    throw std::runtime_error("Missing resident option");
                (option == "--resident-directory" ? directory : nonce) = argv[index];
            } else {
                current.push_back(option);
            }
        }
        if (directory.empty()) {
            if (!nonce.empty())
                throw std::runtime_error("Resident nonce without directory");
            return pgnWorkerMain(argc, argv);
        }
        const auto folder = std::filesystem::u8path(directory);
        if (!scout_resident::valid_nonce(nonce) || !std::filesystem::is_directory(folder))
            throw std::runtime_error("Invalid resident owner directory/nonce");
        scout_resident::arena.enabled = true;
        uint64_t sequence = 0;
        std::filesystem::path stop;
        for (;;) {
            for (size_t index = 1; index + 1 < current.size(); ++index) {
                if (current[index] == "--stop-file")
                    stop = std::filesystem::u8path(current[index + 1]);
            }
            scout_resident::arena.begin();
            std::vector<char*> pointers;
            for (auto& argument : current)
                pointers.push_back(argument.data());
            pointers.push_back(nullptr);
            const auto began = std::chrono::steady_clock::now();
            const int code = pgnWorkerMain(int(current.size()), pointers.data());
            if (code == 0) {
                const auto cleanup = scout_resident::arena.end();
                if (cleanup != cudaSuccess) {
                    std::fprintf(stderr, "Resident resource closure failed: %s\n",
                                 cudaGetErrorString(cleanup));
                    return 9;
                }
            }
            const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - began).count();
            scout_resident::result(folder, sequence, nonce, code, uint64_t(duration),
                                   scout_resident::arena.device_bytes());
            std::fflush(stdout);
            if (code)
                return code;
            if (std::filesystem::exists(folder / "shutdown") ||
                (!stop.empty() && std::filesystem::exists(stop)))
                return 0;
            ++sequence;
            const auto request = scout_resident::frame_path(folder, sequence, ".args");
            while (!std::filesystem::exists(request)) {
                if (std::filesystem::exists(folder / "shutdown") ||
                    (!stop.empty() && std::filesystem::exists(stop)))
                    return 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            auto next = scout_resident::read_request(request);
            for (const auto& argument : next.args) {
                if (argument == "--resident-directory" || argument == "--resident-nonce" ||
                    argument == "--self-test" || argument == "--fermat-diagnostic")
                    throw std::runtime_error("Unsupported command in resident search owner");
            }
            nonce = std::move(next.nonce);
            current.resize(1);
            for (auto& argument : next.args)
                current.push_back(std::move(argument));
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "PGN host failure: %s; process exits without further CUDA calls\n",
                     error.what());
        return 8;
    } catch (...) {
        std::fprintf(stderr,
                     "PGN unknown host failure; process exits without further CUDA calls\n");
        return 8;
    }
}

#pragma once
#include <type_traits>
#include "optimized_narrow_euler.cuh"
#include "optimized_euler69.cuh"
#include "optimized_radix3.cuh"
#include "optimized_euler72.cuh"
#include "optimized_radix3_upper.cuh"
#include "optimized_euler75.cuh"
#include "optimized_pairs76_77.cuh"
#include "optimized_pairs78_79.cuh"
#include "optimized_pairs81_87.cuh"
#include "optimized_pairs88_94.cuh"
#include "optimized_float80.cuh"
#include "optimized_retained95_low.cuh"
#include "optimized_retained95.cuh"

#if defined(__CUDACC__)
#define ENGINE_HD __host__ __device__ __forceinline__
#else
#define ENGINE_HD inline
#endif
namespace scout_arithmetic {
struct ReciprocalState{uint32_t muLower=0;};
struct PreparedState{uint32_t muLower=0,coefficient=0;};
template<unsigned Bits>using State=typename std::conditional<(Bits<=68),optimized_narrow_euler::State,
 typename std::conditional<(Bits<=75||Bits==95),ReciprocalState,PreparedState>::type>::type;
template<unsigned Bits>ENGINE_HD State<Bits> start_state(uint32_t mu,uint32_t coefficient){
 State<Bits>s{};if constexpr(Bits>=69)s.muLower=mu;
 if constexpr(Bits>=76&&Bits<=94)s.coefficient=coefficient;return s;
}
template<unsigned Bits,uint64_t FixedHigh,bool Upper>ENGINE_HD bool test(uint64_t low,uint32_t actual_high,State<Bits>&s){
 static_assert(Bits>=67&&Bits<=95,"Supported search width");
 constexpr bool Fixed=FixedHigh!=UINT64_MAX;
 const uint32_t high=Fixed?uint32_t(FixedHigh):actual_high;
 if constexpr(Bits<=68)return optimized_narrow_euler::test(low,high,s);
 else if constexpr(Bits==69){static_assert(Fixed&&FixedHigh>=16&&FixedHigh<32,"Compile-time69-bit highword specialization");return optimized_euler69::test_prepared<uint32_t(FixedHigh)>(low,s.muLower);}
 else if constexpr(Bits<=71)return optimized_radix3::test_prepared<Bits>(low,high,s.muLower);
 else if constexpr(Bits==72)return optimized_euler72::test_variant<Upper>(low,high,s.muLower);
 else if constexpr(Bits<=74)return optimized_radix3_upper::test_variant<Bits,Upper>(low,high,s.muLower);
 else if constexpr(Bits==75)return optimized_euler75::test_variant<Upper>(low,high,s.muLower);
 else if constexpr(Bits<=77)return optimized_pairs76_77::test_prepared<Bits>(low,high,s.muLower,s.coefficient);
 else if constexpr(Bits<=79)return optimized_pairs78_79::test_prepared<Bits,Upper>(low,high,s.muLower,s.coefficient);
 else if constexpr(Bits==80)return optimized_float80::test_prepared(low,high,s.muLower,s.coefficient);
 else if constexpr(Bits<=87)return optimized_pairs81_87::test_prepared<Bits,Upper>(low,high,s.muLower,s.coefficient);
 else if constexpr(Bits<=94)return optimized_pairs88_94::test_prepared<Bits,Upper>(low,high,s.muLower,s.coefficient);
 else if constexpr(Upper)return optimized_retained95::test_reflected(low,high,s.muLower);
 else return optimized_retained95_low::test_prepared(low,high,s.muLower);
}
template<unsigned Bits,unsigned Cache>inline PreparedState prepare(uint64_t last_low,uint32_t high){
 PreparedState s{};
 if constexpr(Bits<=68)return s;
 else if constexpr(Cache-Bits<32)s.muLower=montgomery_columns::reciprocal<Cache>(last_low,high);
 else{const auto mu=extended_columns::reciprocal<Bits,Cache>(last_low,high);s.muLower=uint32_t(mu);s.coefficient=uint32_t(mu>>32);}
 if constexpr(Bits==76||Bits==77||Bits==79)s.coefficient=seed_schedule::make<Bits>(high);
 else if constexpr(Bits==78)s.coefficient=seed_schedule::make_for_prefix<78>(high,(high>>6)>>1);
 else if constexpr(Bits==80)s.coefficient=optimized_float80::coefficientBits(s.muLower);
 else if constexpr(Bits>=81&&Bits<=87)s.coefficient=optimized_pairs81_87::coefficient_bits<Bits>(s.muLower);
 return s;
}
struct Domain{unsigned bits;uint32_t high_begin,high_end,cache_bits;bool upper;uint64_t fixed_high;};
inline constexpr Domain Domains[]={
 {67,4u,5u,96,false,4ULL},
 {67,5u,6u,96,false,5ULL},
 {67,6u,7u,96,false,6ULL},
 {67,7u,8u,96,false,7ULL},
 {68,8u,9u,96,false,8ULL},
 {68,9u,10u,96,false,9ULL},
 {68,10u,11u,96,false,10ULL},
 {68,11u,12u,96,false,11ULL},
 {68,12u,13u,96,false,12ULL},
 {68,13u,14u,96,false,13ULL},
 {68,14u,15u,96,false,14ULL},
 {68,15u,16u,96,false,15ULL},
 {69,16u,17u,96,false,16ULL},
 {69,17u,18u,96,false,17ULL},
 {69,18u,19u,96,false,18ULL},
 {69,19u,20u,96,false,19ULL},
 {69,20u,21u,96,false,20ULL},
 {69,21u,22u,96,false,21ULL},
 {69,22u,23u,96,false,22ULL},
 {69,23u,24u,96,false,23ULL},
 {69,24u,25u,96,false,24ULL},
 {69,25u,26u,96,false,25ULL},
 {69,26u,27u,96,false,26ULL},
 {69,27u,28u,96,false,27ULL},
 {69,28u,29u,96,false,28ULL},
 {69,29u,30u,96,false,29ULL},
 {69,30u,31u,96,false,30ULL},
 {69,31u,32u,96,false,31ULL},
 {70,32u,64u,96,false,UINT64_MAX},
 {71,64u,128u,96,false,UINT64_MAX},
 {72,128u,232u,96,false,UINT64_MAX},
 {72,232u,256u,96,true,UINT64_MAX},
 {73,256u,335u,104,false,UINT64_MAX},
 {73,335u,512u,104,true,UINT64_MAX},
 {74,512u,703u,104,false,UINT64_MAX},
 {74,703u,1024u,105,true,UINT64_MAX},
 {75,1024u,1148u,106,false,UINT64_MAX},
 {75,1148u,2048u,106,true,UINT64_MAX},
 {76,2048u,4096u,107,false,UINT64_MAX},
 {77,4096u,8192u,108,false,UINT64_MAX},
 {78,8192u,16384u,109,false,UINT64_MAX},
 {79,16384u,23167u,110,false,UINT64_MAX},
 {79,23167u,32768u,110,true,UINT64_MAX},
 {80,32768u,65536u,111,false,UINT64_MAX},
 {81,65536u,131072u,112,false,UINT64_MAX},
 {82,131072u,196608u,113,false,UINT64_MAX},
 {82,196608u,262144u,113,true,UINT64_MAX},
 {83,262144u,524288u,114,false,UINT64_MAX},
 {84,524288u,1048576u,115,false,UINT64_MAX},
 {85,1048576u,2097152u,116,false,UINT64_MAX},
 {86,2097152u,3145728u,117,false,UINT64_MAX},
 {86,3145728u,4194304u,117,true,UINT64_MAX},
 {87,4194304u,5242880u,118,false,UINT64_MAX},
 {87,5242880u,8388608u,118,true,UINT64_MAX},
 {88,8388608u,16777216u,119,false,UINT64_MAX},
 {89,16777216u,33554432u,120,false,UINT64_MAX},
 {90,33554432u,67108864u,121,false,UINT64_MAX},
 {91,67108864u,134217728u,122,false,UINT64_MAX},
 {92,134217728u,206941441u,123,false,UINT64_MAX},
 {92,206941441u,268435456u,125,true,UINT64_MAX},
 {93,268435456u,292659403u,124,false,UINT64_MAX},
 {93,292659403u,536870912u,126,true,UINT64_MAX},
 {94,536870912u,585318807u,126,false,UINT64_MAX},
 {94,585318807u,1073741824u,128,true,UINT64_MAX},
 {95,1073741824u,1342177280u,126,false,UINT64_MAX},
 {95,1342177280u,2147483648u,126,true,UINT64_MAX},
};
inline const Domain*domain_for(uint32_t high){for(const auto&d:Domains)if(high>=d.high_begin&&high<d.high_end)return &d;return nullptr;}
inline PreparedState prepare_domain(uint64_t last_low,uint32_t high){
 if(high>=4u&&high<5u)return prepare<67,96>(last_low,high);
 if(high>=5u&&high<6u)return prepare<67,96>(last_low,high);
 if(high>=6u&&high<7u)return prepare<67,96>(last_low,high);
 if(high>=7u&&high<8u)return prepare<67,96>(last_low,high);
 if(high>=8u&&high<9u)return prepare<68,96>(last_low,high);
 if(high>=9u&&high<10u)return prepare<68,96>(last_low,high);
 if(high>=10u&&high<11u)return prepare<68,96>(last_low,high);
 if(high>=11u&&high<12u)return prepare<68,96>(last_low,high);
 if(high>=12u&&high<13u)return prepare<68,96>(last_low,high);
 if(high>=13u&&high<14u)return prepare<68,96>(last_low,high);
 if(high>=14u&&high<15u)return prepare<68,96>(last_low,high);
 if(high>=15u&&high<16u)return prepare<68,96>(last_low,high);
 if(high>=16u&&high<17u)return prepare<69,96>(last_low,high);
 if(high>=17u&&high<18u)return prepare<69,96>(last_low,high);
 if(high>=18u&&high<19u)return prepare<69,96>(last_low,high);
 if(high>=19u&&high<20u)return prepare<69,96>(last_low,high);
 if(high>=20u&&high<21u)return prepare<69,96>(last_low,high);
 if(high>=21u&&high<22u)return prepare<69,96>(last_low,high);
 if(high>=22u&&high<23u)return prepare<69,96>(last_low,high);
 if(high>=23u&&high<24u)return prepare<69,96>(last_low,high);
 if(high>=24u&&high<25u)return prepare<69,96>(last_low,high);
 if(high>=25u&&high<26u)return prepare<69,96>(last_low,high);
 if(high>=26u&&high<27u)return prepare<69,96>(last_low,high);
 if(high>=27u&&high<28u)return prepare<69,96>(last_low,high);
 if(high>=28u&&high<29u)return prepare<69,96>(last_low,high);
 if(high>=29u&&high<30u)return prepare<69,96>(last_low,high);
 if(high>=30u&&high<31u)return prepare<69,96>(last_low,high);
 if(high>=31u&&high<32u)return prepare<69,96>(last_low,high);
 if(high>=32u&&high<64u)return prepare<70,96>(last_low,high);
 if(high>=64u&&high<128u)return prepare<71,96>(last_low,high);
 if(high>=128u&&high<232u)return prepare<72,96>(last_low,high);
 if(high>=232u&&high<256u)return prepare<72,96>(last_low,high);
 if(high>=256u&&high<335u)return prepare<73,104>(last_low,high);
 if(high>=335u&&high<512u)return prepare<73,104>(last_low,high);
 if(high>=512u&&high<703u)return prepare<74,104>(last_low,high);
 if(high>=703u&&high<1024u)return prepare<74,105>(last_low,high);
 if(high>=1024u&&high<1148u)return prepare<75,106>(last_low,high);
 if(high>=1148u&&high<2048u)return prepare<75,106>(last_low,high);
 if(high>=2048u&&high<4096u)return prepare<76,107>(last_low,high);
 if(high>=4096u&&high<8192u)return prepare<77,108>(last_low,high);
 if(high>=8192u&&high<16384u)return prepare<78,109>(last_low,high);
 if(high>=16384u&&high<23167u)return prepare<79,110>(last_low,high);
 if(high>=23167u&&high<32768u)return prepare<79,110>(last_low,high);
 if(high>=32768u&&high<65536u)return prepare<80,111>(last_low,high);
 if(high>=65536u&&high<131072u)return prepare<81,112>(last_low,high);
 if(high>=131072u&&high<196608u)return prepare<82,113>(last_low,high);
 if(high>=196608u&&high<262144u)return prepare<82,113>(last_low,high);
 if(high>=262144u&&high<524288u)return prepare<83,114>(last_low,high);
 if(high>=524288u&&high<1048576u)return prepare<84,115>(last_low,high);
 if(high>=1048576u&&high<2097152u)return prepare<85,116>(last_low,high);
 if(high>=2097152u&&high<3145728u)return prepare<86,117>(last_low,high);
 if(high>=3145728u&&high<4194304u)return prepare<86,117>(last_low,high);
 if(high>=4194304u&&high<5242880u)return prepare<87,118>(last_low,high);
 if(high>=5242880u&&high<8388608u)return prepare<87,118>(last_low,high);
 if(high>=8388608u&&high<16777216u)return prepare<88,119>(last_low,high);
 if(high>=16777216u&&high<33554432u)return prepare<89,120>(last_low,high);
 if(high>=33554432u&&high<67108864u)return prepare<90,121>(last_low,high);
 if(high>=67108864u&&high<134217728u)return prepare<91,122>(last_low,high);
 if(high>=134217728u&&high<206941441u)return prepare<92,123>(last_low,high);
 if(high>=206941441u&&high<268435456u)return prepare<92,125>(last_low,high);
 if(high>=268435456u&&high<292659403u)return prepare<93,124>(last_low,high);
 if(high>=292659403u&&high<536870912u)return prepare<93,126>(last_low,high);
 if(high>=536870912u&&high<585318807u)return prepare<94,126>(last_low,high);
 if(high>=585318807u&&high<1073741824u)return prepare<94,128>(last_low,high);
 if(high>=1073741824u&&high<1342177280u)return prepare<95,126>(last_low,high);
 if(high>=1342177280u&&high<2147483648u)return prepare<95,126>(last_low,high);
 assert(false&&"Unsupported arithmetic domain");return{};
}
}
#undef ENGINE_HD

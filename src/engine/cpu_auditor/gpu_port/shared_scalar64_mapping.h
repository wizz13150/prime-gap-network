#pragma once
// CPU-native64 physical mapping of the SAME canonical R=2^64 raw/fold/EJ schedule.
#include <cstdint>
#include <cassert>
#include "optimized_narrow_euler.cuh"
namespace pgn_cpu_scalar64 {
using Wide=unsigned __int128;
struct Value{uint64_t low;uint32_t high;};
inline Value fromSource(optimized_narrow_euler::Value a){return {uint64_t(a.x)|(uint64_t(a.y)<<32),a.z};}
inline optimized_narrow_euler::Value toSource(Value a){return {uint32_t(a.low),uint32_t(a.low>>32),a.high};}
inline uint64_t negativeInverse64(uint64_t n){uint64_t x=n;for(unsigned j=0;j<5;j++)x*=2-n*x;return uint64_t(0)-x;}
inline Value raw(Value a,uint64_t n,uint32_t nh,uint64_t np){
 const Wide xx=Wide(a.low)*a.low;const uint64_t q=uint64_t(xx)*np;const Wide qn=Wide(q)*n;
 const uint64_t sum=uint64_t(xx)+uint64_t(qn),carry=sum<uint64_t(xx);assert(sum==0);
 Wide middle=(xx>>64)+(qn>>64)+carry;middle+=Wide(a.low)*uint32_t(a.high+a.high);middle+=Wide(q)*nh;
 return {uint64_t(middle),uint32_t(Wide(a.high)*a.high+(middle>>64))};
}
inline Value fold(Value a,uint64_t n,uint32_t nh,uint32_t mu){const uint32_t q=uint32_t((uint64_t(a.high)*mu)>>32);const Wide product=Wide(q)*n+((Wide(q)*nh)<<64);const Wide value=(Wide(a.high)<<64)|a.low,d=value-product;return{uint64_t(d),uint32_t(d>>64)};}
inline Value shifted(Value a,unsigned bits){assert(bits<32);const Wide value=((Wide(a.high)<<64)|a.low)<<bits;assert((value>>96)==0);return{uint64_t(value),uint32_t(value>>64)};}
inline Value shiftFold(Value a,unsigned bits,uint64_t n,uint32_t nh,uint32_t mu){return fold(shifted(a,bits),n,nh,mu);}
inline Value squareFold(Value a,uint64_t n,uint32_t nh,uint64_t np,uint32_t mu){return fold(raw(a,n,nh,np),n,nh,mu);}
inline bool equal(Value a,Value b){return a.low==b.low&&a.high==b.high;}
template<class Cache>inline bool test(uint64_t n,uint32_t high,Cache &cache){
 assert(optimized_narrow_euler::admits_high(high));if(!(n&1))return false;const uint32_t bucket=uint32_t(n>>36),tag=(high<<28)|bucket;if(!cache.muLower||cache.bucketTag!=tag){cache.muLower=optimized_narrow_euler::build_mu((uint64_t(bucket)<<36)|((uint64_t(1)<<36)-1),high);cache.bucketTag=tag;}const uint32_t mu=cache.muLower;const uint64_t np=negativeInverse64(n);Value a{0,1};const uint32_t prefix=high*16,chunks=prefix/26,tail=prefix%26,nibble=uint32_t(n>>60);for(unsigned j=0;j<chunks;j++)a=shiftFold(a,26,n,high,mu);if(tail+nibble<=26)a=shiftFold(a,tail+nibble,n,high,mu);else{a=shiftFold(a,tail,n,high,mu);a=shiftFold(a,nibble,n,high,mu);}uint64_t stream=((n&~uint64_t(15))<<4)|128;
 for(unsigned group=0;group<14;group++){a=raw(a,n,high,np);a=squareFold(a,n,high,np,mu);a=raw(a,n,high,np);a=raw(a,n,high,np);a=shiftFold(a,uint32_t(stream>>60),n,high,mu);stream<<=4;}
 a=raw(a,n,high,np);a=squareFold(a,n,high,np,mu);a=raw(a,n,high,np);a=shiftFold(a,uint32_t((n>>1)&7),n,high,mu);
 if(((n>>1)^(n>>2))&1)return equal(a,{n,high-1})||equal(a,{n<<1,2*high-1+uint32_t(n>>63)});
 return equal(a,{0,1})||equal(a,{n,high+1});
}
}

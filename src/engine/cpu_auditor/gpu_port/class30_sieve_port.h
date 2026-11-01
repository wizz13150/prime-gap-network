#pragma once
// CPU physical mapping of canonical class30_sieve_body and PgnHostTables.
// Same7834 primes7..80000, classes, bitmap polarity and congruence relation.
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <limits>
#include <utility>
namespace pgn_cpu_shared {
inline constexpr uint32_t Classes30[8]={1,7,11,13,17,19,23,29};
struct Number96{uint64_t low;uint32_t high;};
struct SievePrime{uint32_t p,inverse30;uint64_t reciprocal,r64;};
inline std::vector<SievePrime> canonicalPrimeBank(){
 constexpr unsigned Depth=80000;std::vector<unsigned char>composite(Depth+1);
 for(unsigned i=2;i*i<=Depth;i++)if(!composite[i])for(unsigned j=i*i;j<=Depth;j+=i)composite[j]=1;
 std::vector<SievePrime>out;
 for(unsigned p=7;p<=Depth;p+=2)if(!composite[p]){
  unsigned k=0;while((1+k*p)%30)++k;
  out.push_back({p,(1+k*p)/30,UINT64_MAX/p,(UINT64_MAX%p+1)%p});
 }
 if(out.size()!=7834){throw std::runtime_error("Canonical7834 primebank mismatch");}
 return out;
}
class Class30Bitmap{
 Number96 origin_;uint64_t slots_;std::vector<uint64_t>composite_;
public:
 Class30Bitmap(Number96 origin,uint64_t slots,std::vector<uint64_t>marks):origin_(origin),slots_(slots),composite_(std::move(marks)){if(!slots||slots>(1ULL<<28)||composite_.size()!=(slots+63)/64||(origin.low%30+(origin.high%30)*16)%30)throw std::runtime_error("Invalid tiledbitmap extent/origin");}
 Class30Bitmap(Number96 origin,uint64_t slots,const std::vector<SievePrime>&bank):origin_(origin),slots_(slots),composite_((slots+63)/64,0){
  if(!slots||slots>(1ULL<<28)||(origin.low%30+(origin.high%30)*16)%30)throw std::runtime_error("Invalid canonical class30 origin/extent");
  for(const auto&entry:bank){const uint64_t p=entry.p;const uint64_t residue=(origin.low%p+uint64_t(origin.high%p)*entry.r64)%p;
   for(unsigned c=0;c<8;c++){
    const uint64_t r=(residue+Classes30[c])%p;
    const uint64_t cycle=((p-r)%p)*entry.inverse30%p;
    for(uint64_t index=8*cycle+c;index<slots;index+=8*p)composite_[index>>6]|=1ULL<<(index&63);
   }
  }
  if(slots&63)composite_.back()|=~0ULL<<(slots&63);
 }
 uint64_t slots()const{return slots_;}
 bool marked(uint64_t i)const{if(i>=slots_)throw std::runtime_error("Bitmap index outsidequery");return(composite_[i>>6]>>(i&63))&1;}
 Number96 number(uint64_t i)const{if(i>=slots_)throw std::runtime_error("Candidate outsidequery");uint64_t offset=30*(i>>3)+Classes30[i&7],lo=origin_.low+offset;return {lo,origin_.high+uint32_t(lo<origin_.low)};}
 int64_t next(int64_t position)const{
  if(position<0||uint64_t(position)>=slots_){return -1;}
  uint64_t w=uint64_t(position)>>6,word=~composite_[w]&(~0ULL<<(position&63));
  for(;;){if(word){uint64_t i=w*64+__builtin_ctzll(word);return i<slots_?int64_t(i):-1;}if(++w>=composite_.size())return -1;word=~composite_[w];}
 }
 int64_t previous(int64_t position)const{
  if(position<0){return -1;}
  if(uint64_t(position)>=slots_)position=int64_t(slots_-1);
  int64_t w=position>>6;uint64_t word=~composite_[w]&(~0ULL>>(63-(position&63)));
  for(;;){if(word)return w*64+63-__builtin_clzll(word);if(--w<0)return -1;word=~composite_[w];}
 }
};
}

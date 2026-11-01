#include "engine/cuda/pgn_walk_continuation.h"
#include <vector>
#include <utility>
#include <iostream>
#include <random>
#include <algorithm>
struct Ops {
 const std::vector<bool>& candidates;const std::vector<bool>& primes;int jump;
 std::vector<int>& trace;std::vector<std::pair<int,int>>& gaps;
 std::int64_t next(std::int64_t x)const{for(;x<static_cast<std::int64_t>(candidates.size());++x)if(candidates[x])return x;return -1;}
 std::int64_t previous(std::int64_t x)const{for(x=std::min(x,static_cast<std::int64_t>(candidates.size()-1));x>=0;--x)if(candidates[x])return x;return -1;}
 bool prime(std::int64_t x)const{trace.push_back(static_cast<int>(x));return primes[x];}
 int shift(std::int64_t)const{return jump;}
 void gap(std::int64_t p,std::int64_t q)const{gaps.emplace_back(p,q);}
};
std::pair<unsigned long long,unsigned long long> reference(int slots,int jobs,int job,const Ops& ops){
 const int length=slots/jobs,lo=job*length,hi=job+1==jobs?slots:lo+length;
 if(!length||lo>=hi)return {};unsigned long long tests=0,jumps=0;
 auto c=ops.next(lo);while(c>=0){++tests;if(ops.prime(c))break;c=ops.next(c+1);}if(c<0)return {tests,jumps};
 auto p=c,s=p+ops.shift(p);if(s>=slots)return {tests,jumps};
 for(;;){if(p>=hi)break;s=ops.previous(s);if(s<0)break;
  if(s==p){auto q=p+ops.shift(p);for(;;){q=ops.next(q);if(q<0)return {tests,jumps};++tests;if(ops.prime(q)){ops.gap(p,q);p=q;++jumps;s=p+ops.shift(p);if(s>=slots)return {tests,jumps};break;}++q;}continue;}
  ++tests;if(ops.prime(s)){p=s;++jumps;s=p+ops.shift(p);if(s>=slots)break;}else --s;
 }return {tests,jumps};
}
int main(){std::mt19937 rng(0x83968);unsigned trials=0;
 for(int trial=0;trial<300;++trial){const int slots=1+rng()%513,jobs=1+rng()%32,jump=1+rng()%65;
  std::vector<bool> candidates(slots),primes(slots);for(int i=0;i<slots;++i){candidates[i]=rng()%4!=0;primes[i]=candidates[i]&&rng()%7==0;}
  for(int job=0;job<jobs;++job){std::vector<int>a,b;std::vector<std::pair<int,int>>ga,gb;Ops referenceOps{candidates,primes,jump,a,ga},resumedOps{candidates,primes,jump,b,gb};
   const auto expected=reference(slots,jobs,job,referenceOps);auto state=pgn_walk::startWalk(slots,jobs,job,resumedOps);int steps=0;
   while(state.phase!=pgn_walk::Finished){pgn_walk::stepWalk(state,resumedOps);const auto checkpoint=state;state=checkpoint;if(++steps>10000)return 1;}
   if(a!=b||ga!=gb||expected.first!=state.tests||expected.second!=state.jumps){std::cerr<<"FAIL trial="<<trial<<" job="<<job<<'\n';return 2;}
   std::vector<int> uniformTrace;std::vector<std::pair<int,int>> uniformGaps;
   Ops uniformOps{candidates,primes,jump,uniformTrace,uniformGaps};
   auto uniform=pgn_walk::startWalk(slots,jobs,job,uniformOps);std::int64_t coordinate;int requests=0;
   while(pgn_walk::requestWalkTest(uniform,uniformOps,coordinate)){
    const bool prime=uniformOps.prime(coordinate);pgn_walk::acceptWalkTest(uniform,uniformOps,prime);
    if(++requests>10000)return 3;
   }
   if(uniformTrace!=a||uniformGaps!=ga||uniform.tests!=expected.first||uniform.jumps!=expected.second){std::cerr<<"FAIL uniform arithmetic point trial="<<trial<<" job="<<job<<'\n';return 4;}++trials;
  }
 }
 std::cout<<"PASS exact walk continuation every primitive boundary: "<<trials<<" partitions, identical ordered PRP inputs/gaps/tests/jumps; empty/no-prime/remainder/tail cases\n";
}

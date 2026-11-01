#include <pgn/gold_epoch_policy.h>
#include <cassert>
#include <iostream>
#include <numeric>
using namespace pgn::core;
int main(){
 const CAmount coin=PGN_ATOMIC_UNITS_PER_COIN;
 const GoldEpochDefinition rules{200000,10,171*coin,32*coin,24*coin,8*coin,125*coin,1000*coin,40*65536};
 assert(GoldEpochDefinitionValid(rules));
 const auto first=QuoteGoldEpoch(rules,0),last=QuoteGoldEpoch(rules,199999),next=QuoteGoldEpoch(rules,200000);
 assert(first&&last&&next&&first->gold==171*coin&&first->silver==32*coin&&first->bronze==24*coin&&first->red==8*coin&&first->audit==125*coin/10);
 assert(last->era==0&&next->era==1&&next->gold==171*coin/2&&next->audit==125*coin/20);
 const auto lifetime=GoldEpochLifetimeOrdinaryBudget(rules);assert(lifetime&&*lifetime==989999999989120000LL);
 uint64_t checks=8;
 for(uint64_t era=0;era<63;++era){
  CAmount audit{};for(uint64_t i=0;i<10;++i){const auto q=QuoteGoldEpoch(rules,era*200000+i);assert(q&&q->gold>=0&&q->audit>=0);audit+=q->audit;++checks;}
  assert(audit==CAmount(uint64_t(125*coin)>>era));++checks;
 }
 for(size_t tasks=1;tasks<=10000;++tasks){const auto split=SplitAuditPot(125*coin,tasks);assert(split&&std::accumulate(split->begin(),split->end(),CAmount{})==125*coin&&split->front()-split->back()<=1);++checks;}
 assert(SplitAuditPot(125*coin,0)->empty());assert(!SplitAuditPot(-1,1));assert(!SplitAuditPot(1,262145));
 const auto exhausted=QuoteGoldEpoch(rules,std::numeric_limits<uint64_t>::max());assert(exhausted&&exhausted->gold==0&&exhausted->audit==0);
 auto invalid=rules;invalid.audit_period=13;assert(!QuoteGoldEpoch(invalid,0));
 std::cout<<"{\"PASS\":true,\"checks\":"<<checks+5<<",\"lifetime_ordinary_atomic\":\""<<*lifetime<<"\",\"scope\":\"Exact era/pot arithmetic, not block payout acceptance\"}\n";
}

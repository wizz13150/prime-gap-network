#undef NDEBUG
#include <pgn/gold_epoch_policy.h>
#include <cassert>
#include <array>
#include <chrono>
#include <cstdio>
#include <limits>
using namespace pgn::core;
int main(){
 const CAmount pot=125*COIN;const uint64_t tasks=5000000;
 const auto started=std::chrono::steady_clock::now();CAmount paid=0;uint64_t pages=0;
 for(uint64_t first=0;first<tasks;){const auto count=size_t(std::min(uint64_t(1024),tasks-first));const auto page=SplitAuditPotPage(pot,tasks,first,count);assert(page&&page->size()==count);CAmount sum=0;for(const auto amount:*page)sum+=amount;assert(QuoteAuditPotRange(pot,tasks,first,count)==sum);paid+=sum;first+=count;++pages;}
 assert(paid==pot&&*QuoteAuditPotRange(pot,tasks,0,tasks)==pot);assert(*QuoteAuditPotRange(pot,tasks,0,1)==125*PGN_ATOMIC_UNITS_PER_COIN/5000000);
 for(const CAmount money:std::array<CAmount,8>{0,1,2,3,10,100,125*COIN,PGN_MAX_SUPPLY_ATOMIC})for(const uint64_t n:std::array<uint64_t,6>{1,2,3,100,5000000,std::numeric_limits<uint64_t>::max()}){
  const auto split=n/2;const auto left=QuoteAuditPotRange(money,n,0,split),right=QuoteAuditPotRange(money,n,split,n-split);assert(left&&right&&*left+*right==money);assert(QuoteAuditPotRange(money,n,n,0)==0);assert(!QuoteAuditPotRange(money,n,n,1));
 }
 for(size_t n=1;n<=1000;++n){const auto legacy=SplitAuditPot(125*COIN+17,n),page=SplitAuditPotPage(125*COIN+17,n,0,n);assert(legacy&&page&&*legacy==*page);}
 assert(!SplitAuditPotPage(pot,tasks,0,1025));assert(!SplitAuditPotPage(pot,tasks,tasks-2,3));assert(!QuoteAuditPotRange(-1,tasks,0,1));assert(!QuoteAuditPotRange(PGN_MAX_SUPPLY_ATOMIC+1,tasks,0,1));assert(!QuoteAuditPotRange(pot,0,0,1));assert(QuoteAuditPotRange(pot,0,0,0)==0);assert(SplitAuditPotPage(pot,0,0,0)->empty());
 // Remainder belongs to a fixed canonical ordinal, so page boundaries do not
 // change ownership of a single atomic unit.
 const auto a=SplitAuditPotPage(10,3,0,1),b=SplitAuditPotPage(10,3,1,2);assert(a&&b&&(*a)[0]==4&&(*b)[0]==3&&(*b)[1]==3);
 const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
 std::printf("{\"PASS\":true,\"logical_tasks\":%llu,\"bounded_pages\":%llu,\"maximum_page_amounts\":1024,\"paid_atomic\":%lld,\"milliseconds\":%.3f,\"scope\":\"exact allocation only; not actual payout admission\"}\n",(unsigned long long)tasks,(unsigned long long)pages,(long long)paid,ms);
}

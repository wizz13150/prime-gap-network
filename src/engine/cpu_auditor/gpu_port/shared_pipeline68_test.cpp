#include <cstdint>
#include <iostream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#endif
#include "reported_gap_verify68_port.h"
int main(){
#ifdef _WIN32
 if(!SetProcessAffinityMask(GetCurrentProcess(),1))return 3;
#else
 cpu_set_t set;CPU_ZERO(&set);CPU_SET(0,&set);if(sched_setaffinity(0,sizeof(set),&set))return 3;
#endif
 const auto bank=pgn_cpu_shared::canonicalPrimeBank();std::string action;uint32_t high;
 while(std::cin>>action>>high){
  if(action=="BITMAP"){
   uint64_t origin,slots;std::cin>>origin>>slots;pgn_cpu_shared::Class30Bitmap bitmap({origin,high},slots,bank);
   std::cout<<"{\"kind\":\"bitmap\",\"prime_count\":"<<bank.size()<<",\"marks\":\"";for(uint64_t i=0;i<slots;i++)std::cout<<(bitmap.marked(i)?'1':'0');std::cout<<"\"}\n";
  }else if(action=="FRAME"){
   uint64_t origin,start,end,queryEnd;uint32_t minimum;std::cin>>origin>>start>>end>>queryEnd>>minimum;
   if(!(origin<=start&&start<end&&end<=queryEnd))return 4;
   const uint64_t slots=scout_extent::candidate_slots(queryEnd-1-origin,1ULL<<28,pgn_cpu_shared::Classes30),ownedHi=scout_extent::candidate_slots(end-1-origin,slots,pgn_cpu_shared::Classes30);
   pgn_cpu_shared::Class30Bitmap bitmap({origin,high},slots,bank);const auto result=pgn_cpu_shared::walk68(bitmap,0,ownedHi,minimum);
   std::cout<<"{\"kind\":\"frame\",\"boundary_needed\":"<<(result.query_right_boundary_needed?"true":"false")<<",\"unsupported\":"<<(result.unsupported_family?"true":"false")<<",\"gaps\":[";bool first=true;
   for(const auto&gap:result.gaps){if(gap.lower.high!=high||gap.lower.low<start||gap.lower.low>=end||gap.upper.high!=high||gap.upper.low>=end)continue;const auto check=pgn_cpu_shared::verifyReportedGap68(gap);if(check!=pgn_cpu_shared::ReportedGapCheck::Verified)return 5;if(!first)std::cout<<',';first=false;std::cout<<"[\""<<gap.lower.low<<"\",\""<<gap.upper.low<<"\"]";}
   std::cout<<"],\"tests\":"<<result.tests<<",\"jumps\":"<<result.jumps<<",\"scientific_engine\":false}\n";
  }else return 2;
 }
 return 0;
}

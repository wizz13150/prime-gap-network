#include "ui/native_history_caption.h"
#include <iostream>
namespace {
unsigned checks{},failures{};
void check(bool ok,const char* name){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<name<<'\n';}}
}
int main(){
 const auto time=pgn::ui::nativeHistoryCaption(58416,6701885,4699094,524292,true);
 const auto length=pgn::ui::nativeHistoryCaption(58416,6701885,4699094,524292,false);
 check(time==length,"axis choice must not split retained count");
 check(time.contains("NATIVE ALL 6701885"),"one complete retained count");
 check(time.count("NATIVE ALL")==1,"single ALL field");
 check(time.contains("CURRENT 58416")&&time.contains("IMPORTED 524292"),"existing provenance scopes preserved");
 check(!time.contains("DATED")&&!time.contains("4699094")&&!time.contains("2002791"),"no source-time classification counters");
 const auto explanation=pgn::ui::nativeHistoryExplanation(6701885,4699094,true);
 check(explanation.contains("6701885")&&!explanation.contains("4699094")&&!explanation.contains("2002791"),"tooltip has one retained count");
 check(explanation.contains("No timestamp is invented")&&explanation.contains("LENGTH"),"TIME eligibility remains truthful");
 check(!pgn::ui::nativeHistoryExplanation(10,10,true).contains("without a source time"),"no invented missing dates");
 check(pgn::ui::nativeHistoryCaption(0,0,0,0,true).contains("NATIVE ALL 0"),"genuine zero preserved");
 check(!pgn::ui::nativeHistoryCaption(0,0,0,0,true).contains("DATED"),"empty archive not split");
  const auto awaitingCurrent=pgn::ui::nativeHistoryCaption(-1,31014247,4699094,524292,false);
  check(awaitingCurrent.contains("CURRENT LOADING")&&awaitingCurrent.contains("NATIVE ALL 31014247"),"missing current run cannot hide an independently loaded full archive");
  for(const auto caption:{pgn::ui::nativeHistoryCaption(0,-1,0,0,true),pgn::ui::nativeHistoryCaption(0,1,2,0,true),pgn::ui::nativeHistoryCaption(-2,1,0,0,true),pgn::ui::nativeHistoryCaption(0,1,-1,0,true),pgn::ui::nativeHistoryCaption(0,1,0,-1,true)})
  check(caption.contains("UNAVAILABLE"),"invalid count must not become zero or a valid total");
 for(const auto total:{9007199254740993LL,99999999999LL,9223372036854775807LL})
  check(pgn::ui::nativeHistoryCaption(0,total,0,0,true).contains(QString::number(total)),"exact 64-bit count without cap/float");
 check(pgn::ui::nativeHistoryCaption(0,100,0,0,true)==pgn::ui::nativeHistoryCaption(0,100,100,0,true),"date acquisition cannot change retained count");
 check(pgn::ui::nativeHistoryExplanation(100,0,false).contains("All retained gaps"),"complete archive remains available");
 std::cout<<"Native history caption "<<checks<<" checks / "<<failures<<" failures; one retained count, no date split or invented timestamp\n";
 return failures?1:0;
}

#include "mining/gap_rate_window.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using pgn::mining::GapRateWindow;
int main(){int n=0;auto check=[&](bool b){++n;if(!b)throw std::runtime_error(std::to_string(n));};GapRateWindow w;
 check(!w.observe("a",true,0,1000,3));check(!w.observe("a",true,1'000'000'000,2000,3));auto r=w.observe("a",true,10'000'000'000,11000,8);check(bool(r));check(std::abs(r->perSecond-.5)<1e-10);check(std::abs(r->perBillionPositions-500000)<1e-6);check(r->spanNs==10'000'000'000);auto repeated=w.observe("a",true,10'000'000'000,11000,8);check(repeated->perSecond==r->perSecond);check(!w.observe("a",false,10'000'000'000,11000,8));check(!w.observe("a",true,20'000'000'000,20000,20));check(!w.observe("b",true,30'000'000'000,30000,30));check(!w.observe("b",true,0,0,0));for(std::uint64_t i=1;i<=180;++i)r=w.observe("b",true,i*1'000'000'000,i*1000,i);check(r->spanNs==60'000'000'000);check(std::abs(r->perSecond-1)<1e-10);check(std::abs(r->perBillionPositions-1e6)<1e-6);check(!w.observe("b",true,180'000'000'000,181000,180));std::cout<<"PASS "<<n<<" gap mean checks\n";
}

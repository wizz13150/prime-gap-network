#include "ui/render/rate_gauge_scale.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
int main(){int n=0;auto check=[&](bool v){++n;if(!v)throw std::runtime_error(std::to_string(n));};pgn::ui::RateGaugeScale a,b;auto x=a.observe(1e12,"run1",1),y=b.observe(1e12,"run1",1);check(x&&y&&x->fraction==y->fraction);check(x->fraction>.9&&x->fraction<1);a.observe(1e15,"run1",2);auto reset=a.observe(1e12,"run2",3);check(reset->fraction==x->fraction);check(a.observe(1e12,"run2",3)->fraction==reset->fraction);a.observe(1e15,"run2",4);check(a.observe(1e12,"run2",61'000'000'005ULL)->fraction>.9);check(a.observe(0.,"run2",62'000'000'005ULL)->fraction==0);check(!a.observe({},"run2",63'000'000'005ULL));check(!a.observe(1.,"",4));std::cout<<"PASS "<<n<<" shared rate gauge checks\n";}

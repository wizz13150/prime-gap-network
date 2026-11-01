#include "ui/render/device_card_bands.h"
#include <iostream>
#include <stdexcept>
int main(){using pgn::ui::deviceCardBands;int n=0;auto check=[&](bool b){++n;if(!b)throw std::runtime_error(std::to_string(n));};for(const auto& x:{deviceCardBands({0,0,270,112},0,2,18),deviceCardBands({0,0,270,112},1,0,18),deviceCardBands({0,0,544,154},2,2,20),deviceCardBands({0,0,270,154},1,3,19),deviceCardBands({0,0,270,128},1,2,18)}){check(x.centerY-x.radius>=x.headerEnd);check(x.centerY+x.radius+18<=x.footerStart);check(x.radius>=16);}std::cout<<"PASS "<<n<<" shared card band checks\n";}

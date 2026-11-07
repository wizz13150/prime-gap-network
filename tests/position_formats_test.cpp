#include "ui/position_formats.h"
#include <iostream>
#include <stdexcept>
int main(){using pgn::ui::positionFormats;int n=0;auto check=[&](bool b){++n;if(!b)throw std::runtime_error(std::to_string(n));};auto p=positionFormats("200000000000000");check(p.human=="200 T");check(p.decimal=="2.0000e14");check(p.binary.contains("1.4211 × 2^47"));check(p.exact=="200000000000000");check(positionFormats("73786976294838206464").binary=="2^66");check(positionFormats("150000000000000000000").binary.contains("2^67"));check(positionFormats("10000000").decimal=="1.0000e7");check(positionFormats("100001").decimal.startsWith("≈ "));check(positionFormats("0").binary=="0");check(positionFormats("").human=="—");check(positionFormats("12e3").human=="—");check(positionFormats("0001000").exact=="1000");
check(positionFormats("39614081257132168796771975168").binary=="2^95");
check(positionFormats("39614081257132168796771975167").exact=="39614081257132168796771975167");
check(positionFormats("39614081257132168796771975167").decimal.startsWith("≈ "));
const auto close=pgn::ui::positionRangeFormats("39614081257132168796771975127","39614081257132168796771975167");
check(close.valid&&close.start.decimal!=close.end.decimal);check(!pgn::ui::positionRangeFormats("10","10").valid);
std::cout<<"PASS "<<n<<" position notation checks\n";}

#pragma once
#include <cstdint>
#include <cassert>
namespace scout_wheel {
// Each complete30-position period contains exactly8 wheel classes for every anchor.
inline void window_steps(uint32_t minimum,const uint32_t classes[8],const int8_t index[30],uint32_t steps[8]){
 assert(minimum>=1);
 const uint32_t periods=(minimum-1)/30,remainder=(minimum-1)%30;
 for(unsigned c=0;c<8;++c){
  steps[c]=periods*8;
  for(unsigned offset=1;offset<=remainder;++offset)
   if(index[(classes[c]+offset)%30]>=0)++steps[c];
 }
}
}

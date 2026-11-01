#pragma once
#include <cstdint>
namespace scout_extent {
// Count class30 candidates at offsets<=remaining, capped by the allocated bitmap.
inline uint64_t candidate_slots(uint64_t remaining,uint64_t capacity,const uint32_t classes[8]){
 uint64_t count=(remaining/30)*8;
 const auto rest=remaining%30;
 for(unsigned j=0;j<8;++j)count+=classes[j]<=rest;
 return count<capacity?count:capacity;
}
}

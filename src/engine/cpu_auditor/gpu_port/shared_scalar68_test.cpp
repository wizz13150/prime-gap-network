#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#endif
#include "shared_scalar68.h"
int main(){
#ifdef _WIN32
 if(!SetProcessAffinityMask(GetCurrentProcess(),1))return 3;
#else
 cpu_set_t set;CPU_ZERO(&set);CPU_SET(0,&set);if(sched_setaffinity(0,sizeof(set),&set))return 3;
#endif
 pgn_cpu_shared::NarrowOwner owner;
 uint32_t high;uint64_t low;
 while(std::cin>>high>>low){pgn_cpu_shared::NarrowOwner fresh;const int cached=pgn_cpu_shared::euler68(low,high,owner),cold=pgn_cpu_shared::euler68(low,high,fresh);std::cout<<cached<<' '<<cold<<' '<<owner.arithmetic.bucketTag<<' '<<owner.arithmetic.muLower<<'\n';}
 return 0;
}

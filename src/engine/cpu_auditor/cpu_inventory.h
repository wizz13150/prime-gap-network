#pragma once
#include <cpuid.h>
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
#ifndef _WIN32
#include <sched.h>
#include <unistd.h>
#endif
inline bool pinnedCpuAvx2Usable(){unsigned a,b,c,d;if(__get_cpuid_max(0,nullptr)<7)return false;__cpuid_count(1,0,a,b,c,d);if(!(c&(1u<<26))||!(c&(1u<<27))||!(c&(1u<<28)))return false;unsigned lo,hi;asm volatile("xgetbv":"=a"(lo),"=d"(hi):"c"(0));if((lo&6)!=6)return false;__cpuid_count(7,0,a,b,c,d);return (b&(1u<<5))!=0;}
inline std::string cpuJsonString(const std::string&input){std::string s="\"";for(unsigned char c:input){if(c=='\"'||c=='\\')s+='\\';s+=(c<32?' ':c);}return s+"\"";}
inline void printCpuInventory(bool probeLargePages){
 char brand[49]={0};unsigned a=0,b=0,c=0,d=0;
 if(__get_cpuid_max(0x80000000,nullptr)>=0x80000004)for(unsigned i=0;i<3;i++){__cpuid_count(0x80000002+i,0,a,b,c,d);unsigned v[4]={a,b,c,d};for(unsigned j=0;j<16;j++)brand[i*16+j]=((char*)v)[j];}
 std::string name(brand);while(!name.empty()&&name.back()==' ')name.pop_back();while(!name.empty()&&name.front()==' ')name.erase(name.begin());
 __cpuid_count(1,0,a,b,c,d);bool osx=c&(1u<<27),avx=c&(1u<<28),fma3=c&(1u<<12);uint64_t xcr=0;
 if(osx){unsigned lo,hi;asm volatile("xgetbv":"=a"(lo),"=d"(hi):"c"(0));xcr=(uint64_t(hi)<<32)|lo;}
 unsigned featureB=0,featureA1=0;if(__get_cpuid_max(0,nullptr)>=7){__cpuid_count(7,0,a,b,c,d);featureB=b;if(a>=1){__cpuid_count(7,1,a,b,c,d);featureA1=a;}}
 printf("{\"schema\":\"PGN_CPU_INVENTORY_V1\",\"cpu_name\":%s,\"features\":{\"osxsave\":%s,\"xcr0\":\"%llu\",\"avx2_cpu\":%s,\"avx2_usable\":%s,\"avx512_ifma_cpu\":%s,\"avx512_ifma_usable\":%s,\"avx_ifma_cpu\":%s,\"avx_ifma_usable\":%s,\"fma3_cpu\":%s,\"fma3_usable\":%s},\"available_cpus\":[",cpuJsonString(name).c_str(),osx?"true":"false",(unsigned long long)xcr,(featureB&(1u<<5))?"true":"false",(avx&&osx&&(xcr&6)==6&&(featureB&(1u<<5)))?"true":"false",(featureB&(1u<<21))?"true":"false",(osx&&(xcr&0xe6)==0xe6&&(featureB&(1u<<21)))?"true":"false",(featureA1&(1u<<23))?"true":"false",(osx&&(xcr&6)==6&&(featureA1&(1u<<23)))?"true":"false",fma3?"true":"false",(fma3&&osx&&(xcr&6)==6)?"true":"false");
 bool first=true;
#ifdef _WIN32
 DWORD_PTR mask=0,system=0;if(!GetProcessAffinityMask(GetCurrentProcess(),&mask,&system))throw std::runtime_error("CPU mask unavailable");
 for(unsigned i=0;i<64;i++)if(mask&(DWORD_PTR(1)<<i)){printf("%s%u",first?"":",",i);first=false;}
 printf("],\"topology\":[");DWORD bytes=0;GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&bytes);if(!bytes||bytes>1048576)throw std::runtime_error("CPU topology size invalid");std::vector<unsigned char>buffer(bytes);if(!GetLogicalProcessorInformationEx(RelationProcessorCore,(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buffer.data(),&bytes))throw std::runtime_error("CPU topology unavailable");first=true;
 for(DWORD off=0;off<bytes;){auto*p=(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buffer.data()+off);if(!p->Size||off+p->Size>bytes)throw std::runtime_error("CPU topology invalid");printf("%s{\"efficiency_class\":%u,\"cpus\":[",first?"":",",p->Processor.EfficiencyClass);first=false;bool f=true;for(unsigned j=0;j<p->Processor.GroupCount;j++)if(p->Processor.GroupMask[j].Group==0)for(unsigned i=0;i<64;i++)if(p->Processor.GroupMask[j].Mask&(KAFFINITY(1)<<i)){printf("%s%u",f?"":",",i);f=false;}printf("]}");off+=p->Size;}
 SIZE_T lp=GetLargePageMinimum();void*memory=probeLargePages&&lp?VirtualAlloc(nullptr,lp,MEM_RESERVE|MEM_COMMIT|MEM_LARGE_PAGES,PAGE_READWRITE):nullptr;DWORD err=probeLargePages&&!memory?GetLastError():0;
 printf("],\"large_pages\":{\"minimum_bytes\":%llu,\"probe_requested\":%s,\"allocation_verified\":%s,\"error\":%lu},\"compute_started\":false}\n",(unsigned long long)lp,probeLargePages?"true":"false",memory?"true":"false",(unsigned long)err);if(memory)VirtualFree(memory,0,MEM_RELEASE);
#else
 (void)probeLargePages;
 cpu_set_t mask;CPU_ZERO(&mask);if(sched_getaffinity(0,sizeof(mask),&mask))throw std::runtime_error("CPU mask unavailable");for(unsigned i=0;i<CPU_SETSIZE;i++)if(CPU_ISSET(i,&mask)){printf("%s%u",first?"":",",i);first=false;}
 printf("],\"topology\":[],\"large_pages\":{\"probe_requested\":false,\"allocation_verified\":false},\"compute_started\":false}\n");
#endif
}

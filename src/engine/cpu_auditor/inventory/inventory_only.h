#pragma once
#include <cpuid.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <stdexcept>
#ifndef _WIN32
#include <sched.h>
#include <unistd.h>
#endif
inline std::string invQuote(const std::string&v){std::string s="\"";for(unsigned char c:v){if(c=='\"'||c=='\\')s+='\\';s+=(c<32?' ':c);}return s+"\"";}
inline const char* invBool(bool value){return value?"true":"false";}
inline std::string invCpus(const std::vector<unsigned>&cpus){std::ostringstream s;s<<'[';for(size_t i=0;i<cpus.size();i++){if(i)s<<',';s<<cpus[i];}return s.str()+']';}
inline void printCpuInventory(bool){
 char host[256]={0},brand[49]={0};unsigned a=0,b=0,c=0,d=0;
#ifdef _WIN32
 DWORD hostBytes=sizeof(host);if(!GetComputerNameA(host,&hostBytes))std::snprintf(host,sizeof(host),"unknown");
#else
 if(gethostname(host,sizeof(host)-1))std::snprintf(host,sizeof(host),"unknown");
#endif
 if(__get_cpuid_max(0x80000000,nullptr)>=0x80000004)for(unsigned i=0;i<3;i++){__cpuid_count(0x80000002+i,0,a,b,c,d);unsigned words[4]={a,b,c,d};for(unsigned j=0;j<16;j++)brand[16*i+j]=((char*)words)[j];}
 std::string model(brand);while(!model.empty()&&model.back()==' ')model.pop_back();while(!model.empty()&&model.front()==' ')model.erase(model.begin());
 __cpuid_count(1,0,a,b,c,d);const bool xsave=c&(1u<<26),osx=c&(1u<<27),avx=c&(1u<<28),fma=c&(1u<<12);uint64_t xcr=0;
 if(osx){unsigned lo,hi;asm volatile("xgetbv":"=a"(lo),"=d"(hi):"c"(0));xcr=(uint64_t(hi)<<32)|lo;}
 unsigned b7=0,a71=0;if(__get_cpuid_max(0,nullptr)>=7){__cpuid_count(7,0,a,b,c,d);b7=b;if(a>=1){__cpuid_count(7,1,a,b,c,d);a71=a;}}
 const bool avxState=xsave&&osx&&avx&&(xcr&6)==6,avx512State=avxState&&(xcr&0xe6)==0xe6;
 std::ostringstream features;features<<"{\"xsave\":"<<invBool(xsave)<<",\"osxsave\":"<<invBool(osx)<<",\"xcr0\":\""<<xcr<<"\",\"avx2_cpu\":"<<invBool(b7&(1u<<5))<<",\"avx2_usable\":"<<invBool(avxState&&(b7&(1u<<5)))<<",\"avx512_ifma_cpu\":"<<invBool(b7&(1u<<21))<<",\"avx512_ifma_usable\":"<<invBool(avx512State&&(b7&(1u<<16))&&(b7&(1u<<17))&&(b7&(1u<<21)))<<",\"avx_ifma_cpu\":"<<invBool(a71&(1u<<23))<<",\"avx_ifma_usable\":"<<invBool(avxState&&(a71&(1u<<23)))<<",\"fma3_cpu\":"<<invBool(fma)<<",\"fma3_usable\":"<<invBool(avxState&&fma)<<'}';
 std::vector<unsigned>available;std::ostringstream topology;topology<<'[';bool first=true;unsigned group=0,systemCpus=0;
#ifdef _WIN32
 GROUP_AFFINITY ga{};if(!GetThreadGroupAffinity(GetCurrentThread(),&ga))throw std::runtime_error("Thread affinity unavailable");group=ga.Group;
 DWORD_PTR mask=0,system=0;if(!GetProcessAffinityMask(GetCurrentProcess(),&mask,&system)||!mask)mask=ga.Mask;
 for(unsigned i=0;i<64;i++)if(mask&(DWORD_PTR(1)<<i))available.push_back(i);
 systemCpus=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
 DWORD bytes=0;GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&bytes);if(!bytes||bytes>1048576)throw std::runtime_error("Topology size invalid");std::vector<unsigned char>raw(bytes);if(!GetLogicalProcessorInformationEx(RelationProcessorCore,(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)raw.data(),&bytes))throw std::runtime_error("Topology unavailable");
 for(DWORD off=0;off<bytes;){auto*p=(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(raw.data()+off);if(!p->Size||off+p->Size>bytes)throw std::runtime_error("Topology boundary invalid");for(unsigned j=0;j<p->Processor.GroupCount;j++){auto g=p->Processor.GroupMask[j];std::vector<unsigned>cpus;for(unsigned i=0;i<64;i++)if(g.Mask&(KAFFINITY(1)<<i))cpus.push_back(i);if(!first)topology<<',';first=false;topology<<"{\"processor_group\":"<<g.Group<<",\"efficiency_class\":"<<unsigned(p->Processor.EfficiencyClass)<<",\"cpus\":"<<invCpus(cpus)<<'}';}off+=p->Size;}
#else
 cpu_set_t mask;CPU_ZERO(&mask);if(sched_getaffinity(0,sizeof(mask),&mask))throw std::runtime_error("Affinity unavailable");for(unsigned i=0;i<CPU_SETSIZE;i++)if(CPU_ISSET(i,&mask))available.push_back(i);systemCpus=(unsigned)sysconf(_SC_NPROCESSORS_ONLN);
 std::map<std::pair<std::string,std::string>,std::vector<unsigned>>groups;
 for(unsigned cpu:available){std::string prefix="/sys/devices/system/cpu/cpu"+std::to_string(cpu)+"/topology/",package,core;std::ifstream p(prefix+"physical_package_id"),q(prefix+"core_id");std::getline(p,package);std::getline(q,core);if(core.empty())core=std::to_string(cpu);groups[{package,core}].push_back(cpu);}
 for(const auto&row:groups){if(!first)topology<<',';first=false;topology<<"{\"processor_group\":0,\"package_id\":"<<invQuote(row.first.first)<<",\"core_id\":"<<invQuote(row.first.second)<<",\"efficiency_class\":null,\"cpus\":"<<invCpus(row.second)<<'}';}
#endif
 topology<<']';const unsigned maxWorkers=(unsigned(available.size())*3)/4;const std::string list=invCpus(available),cap=features.str();
 std::printf("{\"schema\":\"PGN_CPU_INVENTORY_ONLY_V1\",\"host\":%s,\"model\":%s,\"cpu_name\":%s,\"topology\":%s,\"capabilities\":%s,\"features\":%s,\"maxWorkers\":%u,\"maximum_workers\":%u,\"available_cpus\":%s,\"effective_affinity\":%s,\"processor_group\":%u,\"logical_cpus_system\":%u,\"logical_cpus_available\":%u,\"compute_started\":false,\"work_supported\":false,\"prime_arithmetic\":false}\n",invQuote(host).c_str(),invQuote(model).c_str(),invQuote(model).c_str(),topology.str().c_str(),cap.c_str(),cap.c_str(),maxWorkers,maxWorkers,list.c_str(),list.c_str(),group,systemCpus,unsigned(available.size()));
}

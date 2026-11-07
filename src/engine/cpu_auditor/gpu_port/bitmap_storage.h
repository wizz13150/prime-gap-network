#pragma once
// Worker-local reusable bitmap arena. No OS privilege/policy modification.
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
namespace pgn_cpu_shared {
enum class PageMode{Off,Auto,Required};
inline PageMode &bitmapPageMode(){static PageMode m=PageMode::Off;return m;}
inline void setBitmapPageMode(const std::string&s){if(s=="off")bitmapPageMode()=PageMode::Off;else if(s=="auto")bitmapPageMode()=PageMode::Auto;else if(s=="required")bitmapPageMode()=PageMode::Required;else throw std::runtime_error("Invalid bitmap page mode");}
struct BitmapPageInfo{uint64_t allocated_bytes=0,bitmap_bytes=0,large_bytes=0,page_bytes=0,allocation_id=0,uses=0;bool supported=true,verified=false,compute_used=false,privilege_present=false,privilege_enabled=false,privilege_activation_attempted=false;int privilege_error=0;int last_error=0;std::string kind="none";};

#ifdef _WIN32
inline void enablePresentLockPrivilege(BitmapPageInfo &info){
 HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_ADJUST_PRIVILEGES,&token)){info.privilege_error=int(GetLastError());return;}
 struct Close{HANDLE h;~Close(){CloseHandle(h);}}close{token};LUID luid{};if(!LookupPrivilegeValueW(nullptr,L"SeLockMemoryPrivilege",&luid)){info.privilege_error=int(GetLastError());return;}
 auto read=[&](){DWORD bytes=0;GetTokenInformation(token,TokenPrivileges,nullptr,0,&bytes);if(!bytes||bytes>65536)return false;std::vector<unsigned char>data(bytes);if(!GetTokenInformation(token,TokenPrivileges,data.data(),bytes,&bytes))return false;auto*p=reinterpret_cast<TOKEN_PRIVILEGES*>(data.data());info.privilege_present=false;info.privilege_enabled=false;for(DWORD i=0;i<p->PrivilegeCount;i++)if(p->Privileges[i].Luid.LowPart==luid.LowPart&&p->Privileges[i].Luid.HighPart==luid.HighPart){info.privilege_present=true;info.privilege_enabled=(p->Privileges[i].Attributes&SE_PRIVILEGE_ENABLED)!=0;}return true;};
 if(!read()){info.privilege_error=int(GetLastError());return;}if(!info.privilege_present){info.privilege_error=ERROR_NOT_ALL_ASSIGNED;return;}if(info.privilege_enabled)return;
 TOKEN_PRIVILEGES desired{};desired.PrivilegeCount=1;desired.Privileges[0].Luid=luid;desired.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;SetLastError(ERROR_SUCCESS);info.privilege_activation_attempted=true;const BOOL ok=AdjustTokenPrivileges(token,FALSE,&desired,0,nullptr,nullptr);const DWORD error=GetLastError();if(!ok||error!=ERROR_SUCCESS){info.privilege_error=int(error);return;}if(!read()||!info.privilege_enabled)info.privilege_error=ERROR_NOT_ALL_ASSIGNED;
}
#endif

inline uint64_t &bitmapAllocationCounter(){static uint64_t n=0;return n;}
inline size_t roundPages(size_t n,size_t alignment){if(!alignment||n>SIZE_MAX-alignment+1)throw std::runtime_error("Bitmap allocation size overflow");return((n+alignment-1)/alignment)*alignment;}
struct BitmapBuffer{
 uint64_t *data=nullptr;size_t capacity=0;bool explicit_large=false;BitmapPageInfo info;size_t constructed_words=0;
 static size_t hugeSize(){
#ifdef _WIN32
  return size_t(GetLargePageMinimum());
#else
  std::ifstream f("/proc/meminfo");std::string key;uint64_t n;while(f>>key){if(key=="Hugepagesize:"){f>>n;return size_t(n*1024);}std::string tail;std::getline(f,tail);}return 0;
#endif
 }
 explicit BitmapBuffer(size_t needed){
  info.allocation_id=++bitmapAllocationCounter();const PageMode mode=bitmapPageMode();const size_t reportedHuge=hugeSize();const size_t huge=reportedHuge<=64ULL*1024*1024?reportedHuge:0;
#ifdef _WIN32
  SYSTEM_INFO sys{};GetSystemInfo(&sys);info.page_bytes=sys.dwPageSize;
  if(mode!=PageMode::Off)enablePresentLockPrivilege(info);
  if(mode!=PageMode::Off&&huge){capacity=roundPages(needed,huge);data=static_cast<uint64_t*>(VirtualAlloc(nullptr,capacity,MEM_RESERVE|MEM_COMMIT|MEM_LARGE_PAGES,PAGE_READWRITE));if(data){explicit_large=true;info.kind="windows_large";}else info.last_error=int(GetLastError());}
  if(!data){if(mode==PageMode::Required)throw std::runtime_error("Required large bitmap allocation refused:"+std::to_string(info.last_error));capacity=roundPages(needed,size_t(sys.dwPageSize));data=static_cast<uint64_t*>(VirtualAlloc(nullptr,capacity,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));info.kind="ordinary";if(!data)throw std::runtime_error("Ordinary bitmap allocation failed");}
#else
  info.page_bytes=uint64_t(sysconf(_SC_PAGESIZE));
  if(mode!=PageMode::Off&&huge){capacity=roundPages(needed,huge);void*p=mmap(nullptr,capacity,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB,-1,0);if(p!=MAP_FAILED){data=static_cast<uint64_t*>(p);explicit_large=true;info.kind="linux_hugetlb";}else info.last_error=errno;}
  if(!data){if(mode==PageMode::Required)throw std::runtime_error("Required huge bitmap allocation refused:"+std::to_string(info.last_error));const size_t alignment=(mode==PageMode::Auto&&huge)?huge:size_t(info.page_bytes);capacity=roundPages(needed,alignment);void*p=mmap(nullptr,capacity+alignment,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(p==MAP_FAILED)throw std::runtime_error("Ordinary bitmap allocation failed");uintptr_t raw=reinterpret_cast<uintptr_t>(p),aligned=roundPages(raw,alignment);size_t before=aligned-raw,after=capacity+alignment-before-capacity;if(before)munmap(reinterpret_cast<void*>(raw),before);if(after)munmap(reinterpret_cast<void*>(aligned+capacity),after);data=reinterpret_cast<uint64_t*>(aligned);info.kind="ordinary";
#ifdef MADV_HUGEPAGE
   if(mode==PageMode::Auto){if(madvise(data,capacity,MADV_HUGEPAGE)==0)info.kind="linux_thp_requested";else info.last_error=errno;}
#endif
#ifdef MADV_NOHUGEPAGE
   if(mode==PageMode::Off)madvise(data,capacity,MADV_NOHUGEPAGE);
#endif
  }
#endif
  info.allocated_bytes=capacity;std::memset(data,0,capacity);
 }
 ~BitmapBuffer(){if(data){
#ifdef _WIN32
  VirtualFree(data,0,MEM_RELEASE);
#else
  munmap(data,capacity);
#endif
 }}
 BitmapBuffer(const BitmapBuffer&)=delete;BitmapBuffer&operator=(const BitmapBuffer&)=delete;
 void verifyPages(){
  info.large_bytes=0;info.verified=false;
#ifdef _WIN32
  using Query=BOOL(WINAPI*)(HANDLE,PVOID,DWORD);auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32QueryWorkingSetEx"));if(!query){info.last_error=int(GetLastError());return;}
  const size_t step=explicit_large?hugeSize():size_t(info.page_bytes);if(!step)return;std::vector<PSAPI_WORKING_SET_EX_INFORMATION>pages((capacity+step-1)/step);for(size_t i=0;i<pages.size();i++)pages[i].VirtualAddress=reinterpret_cast<unsigned char*>(data)+i*step;
  if(!query(GetCurrentProcess(),pages.data(),DWORD(pages.size()*sizeof(pages[0])))){info.last_error=int(GetLastError());return;}
  for(const auto&p:pages){if(!p.VirtualAttributes.Valid)return;if(p.VirtualAttributes.LargePage)info.large_bytes+=step;}
  info.large_bytes=std::min<uint64_t>(info.large_bytes,capacity);info.verified=true;if(info.large_bytes){info.page_bytes=step;info.last_error=0;}
#else
  std::ifstream f("/proc/self/smaps");std::string line;bool target=false,isolated=false;uint64_t begin=0,end=0,anon=0,private_huge=0,shared_huge=0,kernel_page=0;
  const uintptr_t wanted=reinterpret_cast<uintptr_t>(data);
  while(std::getline(f,line)){unsigned long long a,b;if(std::sscanf(line.c_str(),"%llx-%llx",&a,&b)==2){if(target)break;target=(a<=wanted&&wanted<b);if(target){begin=a;end=b;isolated=begin==wanted&&end==wanted+capacity;}continue;}if(target){std::istringstream in(line);std::string key;uint64_t n;if(in>>key>>n){if(key=="AnonHugePages:")anon=n*1024;else if(key=="Private_Hugetlb:")private_huge=n*1024;else if(key=="Shared_Hugetlb:")shared_huge=n*1024;else if(key=="KernelPageSize:")kernel_page=n*1024;}}}
  if(!target||!isolated){info.last_error=EIO;return;}info.large_bytes=std::min<uint64_t>(capacity,anon+private_huge+shared_huge);if(explicit_large&&!info.large_bytes&&kernel_page>uint64_t(sysconf(_SC_PAGESIZE)))info.large_bytes=capacity;info.verified=true;if(info.large_bytes){info.page_bytes=kernel_page>uint64_t(sysconf(_SC_PAGESIZE))?kernel_page:hugeSize();info.kind=info.large_bytes==capacity?"linux_large_verified":"linux_mixed_verified";info.last_error=0;}
#endif
 }
 void used(size_t bytes){info.bitmap_bytes=bytes;info.compute_used=true;++info.uses;
#ifdef _WIN32
 if(!info.verified)verifyPages();
#else
 if(!info.verified||(!explicit_large&&bitmapPageMode()==PageMode::Auto))verifyPages();
#endif
 }
};
inline std::shared_ptr<BitmapBuffer>&primaryBitmapBuffer(){static thread_local std::shared_ptr<BitmapBuffer>b;return b;}
class BitmapWords{
 std::shared_ptr<BitmapBuffer>buffer_;size_t words_=0;
public:
 BitmapWords(size_t words,uint64_t value):words_(words){if(!words||words>SIZE_MAX/8)throw std::runtime_error("Invalid bitmap size");size_t bytes=words*8;auto&pool=primaryBitmapBuffer();if(!pool||pool.use_count()==1){if(!pool||pool->capacity<bytes)pool=std::make_shared<BitmapBuffer>(bytes);buffer_=pool;}else buffer_=std::make_shared<BitmapBuffer>(bytes);const size_t old=buffer_->constructed_words;std::fill_n(buffer_->data,std::min(old,words),value);if(words>old){std::uninitialized_fill_n(buffer_->data+old,words-old,value);buffer_->constructed_words=words;}}
 size_t size()const{return words_;}uint64_t*data(){return buffer_->data;}const uint64_t*data()const{return buffer_->data;}uint64_t&operator[](size_t n){return buffer_->data[n];}const uint64_t&operator[](size_t n)const{return buffer_->data[n];}uint64_t&back(){return buffer_->data[words_-1];}void markComputeUse()const{buffer_->used(words_*8);}
};
inline BitmapPageInfo bitmapPageInfo(){auto&p=primaryBitmapBuffer();return p?p->info:BitmapPageInfo{};}
inline std::string bitmapPageJSON(){const auto x=bitmapPageInfo();return std::string("{\"schema\":\"PGN_CPU_WORKER_MEMORY_PAGES_V1\",\"allocator_supported\":true,\"allocation_mode\":\"")+x.kind+"\",\"allocated_bytes\":\""+std::to_string(x.allocated_bytes)+"\",\"bitmap_bytes\":\""+std::to_string(x.bitmap_bytes)+"\",\"large_page_bytes_in_use\":\""+std::to_string(x.compute_used&&x.verified?x.large_bytes:0)+"\",\"page_size_bytes\":\""+std::to_string(x.page_bytes)+"\",\"allocation_id\":\""+std::to_string(x.allocation_id)+"\",\"uses\":\""+std::to_string(x.uses)+"\",\"verified\":"+(x.verified?"true":"false")+",\"compute_buffer_used\":"+(x.compute_used?"true":"false")+",\"last_error\":"+std::to_string(x.last_error)+",\"privilege_present\":"+(x.privilege_present?"true":"false")+",\"privilege_enabled\":"+(x.privilege_enabled?"true":"false")+",\"privilege_activation_attempted\":"+(x.privilege_activation_attempted?"true":"false")+",\"privilege_error\":"+std::to_string(x.privilege_error)+"}";}
}

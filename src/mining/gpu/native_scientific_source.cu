// PGN native GPU-owned source engine. No CPU search or result verifier.
// Runtime publication uses the existing phase0 source protocol, not network credit.
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <string>
struct Checkpoint {char magic[8];uint64_t low,high,length,positions,tests,gaps,batches;};
static_assert(sizeof(Checkpoint)==64,"Existing PGNCG001 checkpoint ABI must remain exact64 bytes");
static inline void formatPrefixCounter(char* text,size_t capacity,uint64_t time,const Checkpoint& prefix){
 std::snprintf(text,capacity,"[pgn-counters] time_ns=%llu positions=%llu gaps=%llu tests=%llu batches=%llu processed_gaps=%llu\n",
  (unsigned long long)time,(unsigned long long)prefix.positions,(unsigned long long)prefix.gaps,
  (unsigned long long)prefix.tests,(unsigned long long)prefix.batches,(unsigned long long)prefix.gaps);
}
#ifdef __CUDACC__
#include <cuda_runtime.h>
#define PGN_BOUNDARY_HD __host__ __device__
#else
#define PGN_BOUNDARY_HD
#endif
// The CLOSED CPU test includes this exact relation, never a translated model.
// It supplies a finite predicate oracle; production supplies the SOURCE sieve
// and PRP predicate below. Neither the helper nor its test grants coverage.
namespace pgn_source_boundary {
enum class Status : uint32_t { NotRun, Pair, NoOwnedPrime, Low64Exhausted,
 Gap32Exceeded, PredicateUnavailable, InvalidRange, PublicationHeld, Stopped };
struct Terminal { uint64_t p{},q{},examined{},source_tests{};Status status{Status::NotRun}; };
enum class Phase : uint32_t { Backward, Forward, Ready, Paused, Failed };
struct TailState { uint64_t first{},end{},cursor{};Terminal value{};Phase phase{Phase::Failed};uint32_t descriptor{}; };
PGN_BOUNDARY_HD inline bool owns(uint64_t p,uint64_t first,uint64_t end){return p>=first&&p<end;}
PGN_BOUNDARY_HD inline bool gapFits(uint64_t p,uint64_t q){return q>p&&q-p<=UINT32_MAX;}
PGN_BOUNDARY_HD inline TailState begin(uint64_t first,uint64_t end){
 TailState state{};state.first=first;state.end=end;
 if(first>end)state.value.status=Status::InvalidRange;
 else if(first==end){state.value.status=Status::NoOwnedPrime;state.phase=Phase::Ready;}
 else{state.cursor=end-1;state.phase=Phase::Backward;}return state;
}
template<class Predicate>
PGN_BOUNDARY_HD void step(TailState& state,Predicate& source,bool stop){
 if(state.phase!=Phase::Backward&&state.phase!=Phase::Forward)return;
 if(stop){state.phase=Phase::Paused;state.value.status=Status::Stopped;return;}
 if(state.phase==Phase::Forward){
  if(state.cursor==UINT64_MAX){state.phase=Phase::Failed;state.value.status=Status::Low64Exhausted;return;}
  ++state.cursor;
  if(!gapFits(state.value.p,state.cursor)){state.phase=Phase::Failed;state.value.status=Status::Gap32Exceeded;return;}
 }
 ++state.value.examined;const int result=source(state.cursor);
 if(result==-2){state.phase=Phase::Paused;state.value.status=Status::Stopped;return;}
 if(result<0){state.phase=Phase::Failed;state.value.status=Status::PredicateUnavailable;return;}
 if(state.phase==Phase::Backward){
  if(result){state.value.p=state.cursor;state.phase=Phase::Forward;}
  else if(state.cursor==state.first){state.phase=Phase::Ready;state.value.status=Status::NoOwnedPrime;}
  else --state.cursor;
 }else if(result){state.value.q=state.cursor;state.phase=Phase::Ready;state.value.status=Status::Pair;}
}
template<class Predicate>
PGN_BOUNDARY_HD Terminal close(uint64_t first,uint64_t end,Predicate& source){
 auto state=begin(first,end);
 while(state.phase==Phase::Backward||state.phase==Phase::Forward)step(state,source,false);
 return state.value;
}
enum class VerifyPhase : uint32_t { Lower, Upper, Interior, Ready, Paused, Failed };
struct VerifyState { uint64_t p{},q{},delta{1};VerifyPhase phase{VerifyPhase::Failed}; };
PGN_BOUNDARY_HD inline VerifyState beginVerify(uint64_t p,uint64_t q){
 VerifyState state{};state.p=p;state.q=q;if(gapFits(p,q))state.phase=VerifyPhase::Lower;return state;
}
template<class Predicate>
PGN_BOUNDARY_HD void verifyStep(VerifyState& state,Predicate& proof,bool stop){
 if(state.phase==VerifyPhase::Ready||state.phase==VerifyPhase::Paused||state.phase==VerifyPhase::Failed)return;
 if(stop){state.phase=VerifyPhase::Paused;return;}
 const int result=state.phase==VerifyPhase::Interior?proof.composite(state.p+state.delta):
  proof.endpoint(state.phase==VerifyPhase::Lower?state.p:state.q);
 if(result==-2){state.phase=VerifyPhase::Paused;return;}
 if(result!=1){state.phase=VerifyPhase::Failed;return;}
 if(state.phase==VerifyPhase::Lower)state.phase=VerifyPhase::Upper;
 else if(state.phase==VerifyPhase::Upper)state.phase=state.q-state.p==1?VerifyPhase::Ready:VerifyPhase::Interior;
 else if(++state.delta>=state.q-state.p)state.phase=VerifyPhase::Ready;
}
template<class Row>
PGN_BOUNDARY_HD bool merge(Row* rows,uint32_t capacity,const Terminal& value,
 uint64_t first,uint64_t end,uint32_t minimum){
 if(capacity<2||rows[0].gap>=capacity)return false;
 if(value.status!=Status::Pair&&value.status!=Status::NoOwnedPrime)return false;
 unsigned matches=0;
 for(unsigned row=1;row<=rows[0].gap;++row){
  if(value.status==Status::NoOwnedPrime&&owns(rows[row].slot,first,end))return false;
  if(value.status==Status::Pair&&rows[row].slot==value.p){
   ++matches;if(!gapFits(value.p,value.q)||rows[row].gap!=value.q-value.p||rows[row].gap<minimum)return false;
  }
 }
 if(matches>1)return false;
 if(value.status==Status::NoOwnedPrime)return true;
 if(!owns(value.p,first,end)||!gapFits(value.p,value.q))return false;
 const auto gap=uint32_t(value.q-value.p);
 if(gap<minimum||matches==1)return true;
 const unsigned count=rows[0].gap;if(count>=capacity-1)return false;
 rows[count+1].slot=value.p;rows[count+1].gap=gap;rows[count+1].pad=0;
 rows[0].gap=count+1;return true;
}
}
#undef PGN_BOUNDARY_HD
// Host publication formatting is included verbatim by the CLOSED CPU test.
struct Number128 {uint64_t low,high;};
static std::string decimal(Number128 n){
 uint32_t w[4]={uint32_t(n.low),uint32_t(n.low>>32),uint32_t(n.high),uint32_t(n.high>>32)};
 std::string s;do{uint64_t rem=0;for(int i=3;i>=0;--i){const uint64_t p=(rem<<32)|w[i];w[i]=uint32_t(p/10);rem=p%10;}s.push_back(char('0'+rem));}while(w[0]||w[1]||w[2]||w[3]);std::reverse(s.begin(),s.end());return s;
}
static inline bool formatTerminalBoundaryV2(char* text,size_t capacity,uint32_t descriptor,
 uint64_t ownedFirst,uint64_t ownedEnd,uint64_t high,const pgn_source_boundary::Terminal& tail){
 if(!text||!capacity)return false;
 text[0]='\0';
 using pgn_source_boundary::Status;
 if(ownedFirst>ownedEnd||(tail.status!=Status::Pair&&tail.status!=Status::NoOwnedPrime))return false;
 if(tail.status==Status::Pair&&(!pgn_source_boundary::owns(tail.p,ownedFirst,ownedEnd)||tail.q<ownedEnd||!pgn_source_boundary::gapFits(tail.p,tail.q)))return false;
 const auto first=decimal({ownedFirst,high}),end=decimal({ownedEnd,high});
 const auto p=tail.status==Status::Pair?decimal({tail.p,high}):std::string("none");
 const auto q=tail.status==Status::Pair?decimal({tail.q,high}):std::string("none");
 const int n=std::snprintf(text,capacity,"[pgn-source-boundary-v2] descriptor=%u owned_first=%s owned_end=%s status=%u terminal_p=%s terminal_q=%s examined=%llu source_tests=%llu scope=SOURCE_PRP_TERMINAL_NOT_INDEPENDENT_COVERAGE\n",
  unsigned(descriptor),first.c_str(),end.c_str(),unsigned(tail.status),p.c_str(),q.c_str(),(unsigned long long)tail.examined,(unsigned long long)tail.source_tests);
 if(n<0||size_t(n)>=capacity){text[0]='\0';return false;}return true;
}
#ifndef PGN_SOURCE_BOUNDARY_CLOSED_TEST
#include <cuda_runtime.h>
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>
#include "native_scientific_walk.cuh"
#include "source_batch_time.h"
#ifndef PGN_VERIFY_GAPS
#error Production source adapter requires GPU gap certificates
#endif
#define CHECK(call) do{auto e=(call);if(e!=cudaSuccess){std::fprintf(stderr,"PGN CUDA %s line%d\n",cudaGetErrorString(e),__LINE__);return 2;}}while(0)
#ifndef PGN_WALK_CTA_THREADS
#define PGN_WALK_CTA_THREADS 512U
#endif
constexpr unsigned WalkThreads=PGN_WALK_CTA_THREADS;
constexpr unsigned WalkJobs=164U*512U,WalkBlocks=WalkJobs/WalkThreads;
static_assert((WalkThreads==128U||WalkThreads==256U||WalkThreads==512U)&&WalkBlocks*WalkThreads==WalkJobs,
 "Physical CTA regrouping must preserve every original logical ownership job");
constexpr uint64_t Words=(1ULL<<28)/64,BlockNumbers=30ULL<<25;
constexpr unsigned K=64,SlotBlocks=K+1,Capacity=4096;
static bool parseNumber(const std::string& s,Number128& out){
 if(s.empty()||s.size()>39)return false;uint32_t w[4]{};
 for(char c:s){if(c<'0'||c>'9')return false;uint64_t carry=unsigned(c-'0');
  for(auto& v:w){const uint64_t p=uint64_t(v)*10+carry;v=uint32_t(p);carry=p>>32;}if(carry)return false;
 }out={uint64_t(w[0])|(uint64_t(w[1])<<32),uint64_t(w[2])|(uint64_t(w[3])<<32)};return true;
}
static uint64_t observedNs(){LARGE_INTEGER n,f;QueryPerformanceCounter(&n);QueryPerformanceFrequency(&f);
 return uint64_t(n.QuadPart/f.QuadPart)*1000000000ULL+uint64_t(n.QuadPart%f.QuadPart)*1000000000ULL/uint64_t(f.QuadPart);}
static bool exists(const std::string& file){return !file.empty()&&GetFileAttributesA(file.c_str())!=INVALID_FILE_ATTRIBUTES;}
static bool writeCheckpoint(const std::string& file,Checkpoint value){
 if(file.empty())return true;const auto pending=file+".pending";
 {std::ofstream stream(pending,std::ios::binary|std::ios::trunc);stream.write(reinterpret_cast<const char*>(&value),sizeof(value));if(!stream)return false;}
 return MoveFileExA(pending.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
struct PgnTerminalSourcePredicate {
 PgnWalkGraphBuffers buffers;uint64_t high;unsigned prime_count;volatile unsigned* stop;uint64_t calls{};
 __device__ int operator()(uint64_t low){
  // Exactly the same wheel/small-prime survivors as class30_sieve_body, NOT a
  // replacement MR selector. Avoid a descriptor-local quotient hint outside
  // its bitmap: zero requests the existing exact SOURCE initializer.
  if(*stop)return -2;
  if(!(low&1)||(low%3+high%3)%3==0||(low%5+high%5)%5==0)return 0;
  for(unsigned i=0;i<prime_count;++i){const auto p=buffers.primes[i];
   if(*stop)return -2;
   if((low%p+(high%p)*buffers.r64[i])%p==0)return 0;
  }
  ++calls;const int result=w_is_probable_prime<3>(low,high,0);return *stop?-2:result;
 }
};
struct PgnSourceControl {
 PgnWalkMailbox mailbox;cudaGraphConditionalHandle outer,tail,verify,publish;
};
// A quantum bounds WORK PER INVOCATION, never the search interval or padding.
// Each predicate itself has the explicit source high47/finite-prime-list domain.
// Neither this work bound nor CPU tests certify wall time/WDDM preemption.
constexpr unsigned SourceProbeQuantum=16;
__device__ void pgn_source_halt(PgnSourceControl control,unsigned status){
 cudaGraphSetConditional(control.tail,0);cudaGraphSetConditional(control.verify,0);
 cudaGraphSetConditional(control.publish,0);cudaGraphSetConditional(control.outer,0);
 __threadfence_system();*control.mailbox.done=status;
}
__global__ void pgn_source_terminal_begin(const PgnWalkDescriptor* descriptors,
 const PgnWalkGraphState* state,PgnWalkGraphBuffers buffers,uint64_t resume_low,
 uint64_t requested_end,pgn_source_boundary::TailState* tail,PgnSourceControl control){
 if(blockIdx.x||threadIdx.x||state->cursor>=state->count)return;
 // Every INNER handle is explicitly reset on EVERY descriptor, not just on
 // the one host graph launch. No preceding ready/pause survives into a job.
 cudaGraphSetConditional(control.tail,1);cudaGraphSetConditional(control.verify,0);cudaGraphSetConditional(control.publish,0);
 const auto d=descriptors[state->cursor];
 const uint64_t descriptor_end=d.low+uint64_t(d.owned_blocks)*BlockNumbers;
 const uint64_t first=resume_low>d.low?resume_low:d.low;
 const uint64_t end=requested_end<descriptor_end?requested_end:descriptor_end;
 *tail=pgn_source_boundary::begin(first,end);
 tail->descriptor=state->cursor;
}
__global__ void pgn_source_terminal_step(const PgnWalkGraphState* state,PgnWalkGraphBuffers buffers,
 unsigned prime_count,pgn_source_boundary::TailState* tail,pgn_source_boundary::Terminal* observations,PgnSourceControl control){
 if(blockIdx.x||threadIdx.x||state->cursor>=state->count)return;
 const unsigned index=state->cursor;auto* rows=buffers.results+uint64_t(index)*buffers.result_capacity;
 if(rows[0].gap>=buffers.result_capacity){tail->phase=pgn_source_boundary::Phase::Failed;tail->value.status=pgn_source_boundary::Status::PublicationHeld;}
 PgnTerminalSourcePredicate source{buffers,47,prime_count,control.mailbox.stop,0};
 for(unsigned i=0;i<SourceProbeQuantum&&(tail->phase==pgn_source_boundary::Phase::Backward||tail->phase==pgn_source_boundary::Phase::Forward);++i)
  pgn_source_boundary::step(*tail,source,*control.mailbox.stop!=0);
 tail->value.source_tests+=source.calls;
 if(tail->phase==pgn_source_boundary::Phase::Ready){
  if(*control.mailbox.stop){tail->phase=pgn_source_boundary::Phase::Paused;tail->value.status=pgn_source_boundary::Status::Stopped;}
  else if(!pgn_source_boundary::merge(rows,buffers.result_capacity,tail->value,tail->first,tail->end,buffers.gap_min)){
   tail->phase=pgn_source_boundary::Phase::Failed;tail->value.status=pgn_source_boundary::Status::PublicationHeld;
  }
 }
 observations[index]=tail->value;__threadfence_system();
 if(tail->phase==pgn_source_boundary::Phase::Paused)pgn_source_halt(control,1);
 else if(tail->phase==pgn_source_boundary::Phase::Failed)pgn_source_halt(control,2);
 else if(tail->phase==pgn_source_boundary::Phase::Ready)cudaGraphSetConditional(control.tail,0);
}
struct PgnSourceProofPredicate {
 uint64_t high;volatile unsigned* stop;
 __device__ int endpoint(uint64_t low){
  if(*stop)return -2;const int valid=pgn_mr96::prime12(low,high);
  if(valid!=1)return 0;return *stop?-2:1;
 }
 __device__ int composite(uint64_t low){
  if(*stop)return -2;
  if(!(low&1)||(low%3+high%3)%3==0||(low%5+high%5)%5==0)return 1;
  if(pgn_euler96::test(low,high)&&pgn_mr96::prime12(low,high)!=0)return 0;
  return *stop?-2:1;
 }
};
__global__ void pgn_source_verify_begin(const PgnWalkGraphState* state,PgnWalkGraphBuffers buffers,
 const pgn_source_boundary::TailState* tail,pgn_source_boundary::VerifyState* progress,PgnSourceControl control){
 if(blockIdx.x||threadIdx.x||state->cursor>=state->count)return;
 cudaGraphSetConditional(control.verify,0);cudaGraphSetConditional(control.publish,0);
 if(tail->phase!=pgn_source_boundary::Phase::Ready)return;
 const auto* rows=buffers.results+uint64_t(state->cursor)*buffers.result_capacity;const unsigned count=rows[0].gap;
 if(count>=buffers.result_capacity){pgn_source_halt(control,2);return;}
 if(*control.mailbox.stop){pgn_source_halt(control,1);return;}
 // The reset cost is bounded by the fixed result capacity, not gap length.
 for(unsigned row=1;row<=count;++row){const auto gap=rows[row];
  progress[row]=pgn_source_boundary::beginVerify(gap.slot,gap.slot<=UINT64_MAX-gap.gap?gap.slot+gap.gap:gap.slot);
  buffers.proofs[uint64_t(state->cursor)*buffers.result_capacity+row]=0;
 }
 cudaGraphSetConditional(control.verify,1);
}
__global__ void pgn_source_verify_step(const PgnWalkGraphState* state,PgnWalkGraphBuffers buffers,
 pgn_source_boundary::VerifyState* progress,PgnSourceControl control){
 const unsigned row=1+blockIdx.x*blockDim.x+threadIdx.x;if(state->cursor>=state->count)return;
 const unsigned count=buffers.results[uint64_t(state->cursor)*buffers.result_capacity].gap;
 if(row>count||row>=buffers.result_capacity)return;
 auto& value=progress[row];PgnSourceProofPredicate proof{47,control.mailbox.stop};
 for(unsigned i=0;i<SourceProbeQuantum&&value.phase!=pgn_source_boundary::VerifyPhase::Ready&&
  value.phase!=pgn_source_boundary::VerifyPhase::Failed&&value.phase!=pgn_source_boundary::VerifyPhase::Paused;++i)
  pgn_source_boundary::verifyStep(value,proof,*control.mailbox.stop!=0);
 if(value.phase==pgn_source_boundary::VerifyPhase::Ready)buffers.proofs[uint64_t(state->cursor)*buffers.result_capacity+row]=1;
 else if(value.phase==pgn_source_boundary::VerifyPhase::Failed)buffers.proofs[uint64_t(state->cursor)*buffers.result_capacity+row]=2;
}
__global__ void pgn_source_verify_control(const PgnWalkGraphState* state,PgnWalkGraphBuffers buffers,
 const pgn_source_boundary::VerifyState* progress,PgnSourceControl control){
 if(blockIdx.x||threadIdx.x||state->cursor>=state->count)return;
 const unsigned count=buffers.results[uint64_t(state->cursor)*buffers.result_capacity].gap;
 if(count>=buffers.result_capacity){pgn_source_halt(control,2);return;}
 bool failed=false,paused=false,ready=true;
 for(unsigned row=1;row<=count;++row){
  const auto phase=progress[row].phase;failed|=phase==pgn_source_boundary::VerifyPhase::Failed;
  paused|=phase==pgn_source_boundary::VerifyPhase::Paused;ready&=phase==pgn_source_boundary::VerifyPhase::Ready;
 }
 if(failed){pgn_source_halt(control,3);return;}
 if(paused||*control.mailbox.stop){pgn_source_halt(control,1);return;}
 if(ready){cudaGraphSetConditional(control.verify,0);cudaGraphSetConditional(control.publish,1);}
}
#ifdef PGN_PHASE_TIMING
__global__ void pgn_source_stamp_published(const PgnWalkGraphState* state,
 const pgn_source_boundary::TailState* tail,PgnPhaseTiming* trace){
 if(blockIdx.x||threadIdx.x||state->cursor!=tail->descriptor+1||state->completed!=state->cursor)return;
 uint64_t timer;asm volatile("mov.u64 %0, %%globaltimer;" : "=l"(timer));trace[tail->descriptor].stamp[7]=timer;
}
#endif
int main(int argc,char** argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);
 std::string startText,lengthText,stopFile,stateFile,logFile;unsigned gapMin=800;
 for(int a=1;a<argc;++a){const std::string option=argv[a];
  if(option=="--capabilities"){std::puts("{\"schema\":\"pgn-engine-module-1\",\"id\":\"cuda-native-high47-candidate-v1\",\"protocol\":\"phase0-source-1\",\"host_refill\":false,\"host_verification\":false,\"whole_job_gpu_owned\":true,\"cuda_arch\":\"sm86\",\"prime_certificate\":\"MR12 below318665857834031151167461\",\"coverage\":\"SOURCE_PRP_INDEPENDENT_PROOF_PENDING\",\"single_low64_window\":true,\"supported_high_word\":47}");return 0;}
  if(option=="--no-records")continue;
  if(a+1>=argc){std::fprintf(stderr,"Missing value for %s\n",option.c_str());return 1;}const std::string value=argv[++a];
  if(option=="--start")startText=value;else if(option=="--length")lengthText=value;
  else if(option=="--stop-file")stopFile=value;else if(option=="--state")stateFile=value;else if(option=="--log")logFile=value;
  else if(option=="--gap-min"){if(value!="800"){std::fprintf(stderr,"This qualified source profile requires gap-min800\n");return 1;}}
  else if(option=="--engine"){if(value!="walk")return 1;}
  else if(option=="--device"){if(value!="0")return 1;}
  else if(option=="--progress"||option=="--state-every"){char* end=nullptr;const double n=std::strtod(value.c_str(),&end);if(!end||*end||!(n>0))return 1;}
  else {std::fprintf(stderr,"Unsupported engine option %s\n",option.c_str());return 1;}
 }
 Number128 requested{},len{};if(!parseNumber(startText,requested)||!parseNumber(lengthText,len)||len.high||!len.low||requested.high!=47){std::fprintf(stderr,"Invalid source interval or unsupported high word\n");return 1;}
 const uint64_t length=len.low;if(length>UINT64_MAX-requested.low){std::fprintf(stderr,"Single low64 window required\n");return 1;}
 Checkpoint saved{{'P','G','N','C','G','0','0','1'},requested.low,requested.high,length,0,0,0,0};
 if(exists(stateFile)){std::ifstream input(stateFile,std::ios::binary);Checkpoint prior{};
  input.read(reinterpret_cast<char*>(&prior),sizeof(prior));if(!input||std::memcmp(prior.magic,saved.magic,8)||prior.low!=saved.low||prior.high!=saved.high||prior.length!=length||prior.positions>=length){std::fprintf(stderr,"Checkpoint identity incompatible; preserved, no restart\n");return 1;}saved=prior;
 }
 const uint64_t remaining=length-saved.positions,resumeLow=requested.low+saved.positions;
 const uint64_t requestedEnd=requested.low+length;
 const uint64_t remainder=(resumeLow%30+(requested.high%30)*16)%30,baseLow=resumeLow-remainder;
 if(resumeLow<remainder){std::fprintf(stderr,"Source floor crosses low64 window\n");return 1;}
 const uint64_t blocks=(remaining+remainder+BlockNumbers-1)/BlockNumbers;
 if(blocks>4194304||baseLow>UINT64_MAX-(blocks+2)*BlockNumbers){std::fprintf(stderr,"Source window/descriptor bound\n");return 1;}
 const unsigned Count=unsigned((blocks+K-1)/K);
#ifdef PGN_PRP_CALL_CENSUS
 constexpr const char* CallPath="C:/PGN/diagnostics/prp-call-census-source-20261013-v1/call-counts.bin";
 if(Count!=17||exists(CallPath)){std::fprintf(stderr,"Finite17 census or unique output admission failed; no CUDA call\n");return 1;}
#endif
#ifdef PGN_PHASE_TIMING
 if(Count>256){std::fprintf(stderr,"Phase diagnostic finite descriptor limit exceeded; no CUDA call\n");return 1;}
#endif
 CHECK(cudaSetDeviceFlags(cudaDeviceMapHost));int visible=0;CHECK(cudaGetDeviceCount(&visible));
 if(visible!=1){std::fprintf(stderr,"Exactly one UUID-selected device required\n");return 1;}
 cudaDeviceProp property{};CHECK(cudaGetDeviceProperties(&property,0));
 if(property.major!=8||property.minor!=6||!property.canMapHostMemory){std::fprintf(stderr,"This module requires qualified sm86 mapped memory\n");return 1;}
 PgnWalkGraphBuffers buffers{};buffers.gap_min=gapMin;buffers.result_capacity=Capacity;buffers.blocks_per_slot=SlotBlocks;
#ifndef PGN_SIEVE_DEPTH
#define PGN_SIEVE_DEPTH 40000
#endif
 constexpr unsigned Depth=PGN_SIEVE_DEPTH,GapMin=800;
 static_assert(Depth==40000 || Depth==80000,"Only explicitly bounded sieve profiles are admitted");
 std::vector<unsigned char> composite(Depth+1);
 for(unsigned i=2;i*i<=Depth;++i)if(!composite[i])for(unsigned j=i*i;j<=Depth;j+=i)composite[j]=1;
 std::vector<uint64_t> primes,reciprocals,r64;
 std::vector<uint32_t> item_pidx,item_k0;
 uint32_t wheel_primes[3]{};
 for(unsigned p=7;p<=Depth;p+=2)if(!composite[p]){
  const unsigned index=unsigned(primes.size());primes.push_back(p);reciprocals.push_back(UINT64_MAX/p);
  r64.push_back((UINT64_MAX%p+1)%p);
  if(p==7)wheel_primes[0]=index;if(p==11)wheel_primes[1]=index;if(p==13)wheel_primes[2]=index;
  if(p>13)for(unsigned chunk=0;chunk<(pgn_mark_tile_bits/p+2+255)/256;++chunk){item_pidx.push_back(index);item_k0.push_back(chunk);}
 }
 const uint32_t classes[8]={1,7,11,13,17,19,23,29};int8_t class_index[30];
 std::memset(class_index,-1,sizeof(class_index));uint32_t permutation[30]{},inverse[30]{},steps[8]{};
 for(unsigned j=0;j<8;++j)class_index[classes[j]]=int8_t(j);
 for(unsigned p=0;p<30;++p)if(class_index[p]>=0){
  for(unsigned j=0;j<8;++j)permutation[p]|=1u<<((p*classes[j])%30);
  for(unsigned x=1;x<30;++x)if((p*x)%30==1)inverse[p]=x;
 }
 for(unsigned c=0;c<8;++c)for(unsigned t=1;t<GapMin;++t)if(class_index[(classes[c]+t)%30]>=0)++steps[c];
 CHECK(cudaMemcpyToSymbol(c30_clsidx,class_index,sizeof(class_index)));
 CHECK(cudaMemcpyToSymbol(c30_perm,permutation,sizeof(permutation)));
 CHECK(cudaMemcpyToSymbol(c30_pinv,inverse,sizeof(inverse)));

#ifdef PGN_MARK_CTA_BYTES
 constexpr unsigned MarkSharedBytes=PGN_MARK_CTA_BYTES;
 static_assert(MarkSharedBytes>=pgn_mark_tile_bits/8&&MarkSharedBytes<=65536,"Storage admission preserves the exact bitmap tile");
#else
 constexpr unsigned MarkSharedBytes=pgn_mark_tile_bits/8;
#endif
 CHECK(cudaFuncSetAttribute(pgn_graph_mark,cudaFuncAttributeMaxDynamicSharedMemorySize,MarkSharedBytes));
#ifdef PGN_PHASE_TIMING
 int markCtas=0,walkCtas=0;cudaFuncAttributes markResources{},walkResources{};
 CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(&markCtas,pgn_graph_mark,512,MarkSharedBytes));
 CHECK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(&walkCtas,pgn_graph_walk,WalkThreads,0));
 CHECK(cudaFuncGetAttributes(&markResources,pgn_graph_mark));CHECK(cudaFuncGetAttributes(&walkResources,pgn_graph_walk));
 std::printf("[pgn-phase-resource] mark_cta_limit=%d walk_cta_limit=%d mark_dynamic_bytes=%u mark_static_bytes=%llu mark_regs=%d walk_regs=%d sm_count=%d max_threads_sm=%d registers_sm=%d shared_optin_block=%llu\n",markCtas,walkCtas,MarkSharedBytes,(unsigned long long)markResources.sharedSizeBytes,markResources.numRegs,walkResources.numRegs,property.multiProcessorCount,property.maxThreadsPerMultiProcessor,property.regsPerMultiprocessor,(unsigned long long)property.sharedMemPerBlockOptin);
#endif


 std::vector<PgnWalkDescriptor> descriptors(Count);
 for(unsigned i=0;i<Count;++i){const uint64_t first=uint64_t(i)*K;const unsigned owned=unsigned(std::min<uint64_t>(K,blocks-first));
  descriptors[i]={baseLow+first*BlockNumbers,requested.high,owned+(first+owned<blocks?1u:0u),owned};
 }
 uint64_t *d_primes{},*d_reciprocals{},*d_r64{};uint32_t *d_items{},*d_chunks{},*d_wheel{},*d_steps{};
 PgnWalkDescriptor* d_descriptors{};PgnWalkGraphState* d_state{};unsigned* d_visits{};
 pgn_source_boundary::TailState* d_tail{};pgn_source_boundary::VerifyState* d_verify{};
 unsigned *h_flags{},*d_flags{};w_gaprec_t* h_results{};unsigned long long* h_stats{};PgnWalkMailbox mailbox{};
 pgn_source_boundary::Terminal *h_terminal{},*d_terminal{};
 const uint64_t resultBytes=uint64_t(Count)*Capacity*sizeof(w_gaprec_t),bitmapBytes=2ULL*SlotBlocks*Words*8;
 size_t freeBytes=0,totalBytes=0;CHECK(cudaMemGetInfo(&freeBytes,&totalBytes));
 if(bitmapBytes+resultBytes+uint64_t(Count)*Capacity*4+(128ULL<<20)>freeBytes){std::fprintf(stderr,"Insufficient GPU memory; no job launched\n");return 1;}
 CHECK(cudaMalloc(&d_primes,primes.size()*8));CHECK(cudaMalloc(&d_reciprocals,primes.size()*8));CHECK(cudaMalloc(&d_r64,r64.size()*8));
 CHECK(cudaMalloc(&d_items,item_pidx.size()*4));CHECK(cudaMalloc(&d_chunks,item_k0.size()*4));CHECK(cudaMalloc(&d_wheel,sizeof(wheel_primes)));CHECK(cudaMalloc(&d_steps,sizeof(steps)));
 CHECK(cudaMalloc(&buffers.bitmaps,bitmapBytes));CHECK(cudaMalloc(&buffers.results,resultBytes));CHECK(cudaMalloc(&buffers.stats,uint64_t(Count)*16));
#ifdef PGN_PRP_CALL_CENSUS
 CHECK(cudaMalloc(&buffers.call_counts,uint64_t(Count)*WalkJobs*sizeof(unsigned long long)));
 CHECK(cudaMemset(buffers.call_counts,0xff,uint64_t(Count)*WalkJobs*sizeof(unsigned long long)));
#endif
 CHECK(cudaMalloc(&buffers.proofs,uint64_t(Count)*Capacity*4));CHECK(cudaMalloc(&d_descriptors,uint64_t(Count)*sizeof(PgnWalkDescriptor)));
 CHECK(cudaMalloc(&d_state,sizeof(PgnWalkGraphState)));CHECK(cudaMalloc(&d_visits,uint64_t(Count)*4));
 CHECK(cudaMalloc(&d_tail,sizeof(*d_tail)));CHECK(cudaMalloc(&d_verify,uint64_t(Capacity)*sizeof(*d_verify)));
 CHECK(cudaHostAlloc(&h_flags,uint64_t(Count+2)*4,cudaHostAllocMapped));CHECK(cudaHostAlloc(&h_results,resultBytes,cudaHostAllocMapped));CHECK(cudaHostAlloc(&h_stats,uint64_t(Count)*16,cudaHostAllocMapped));
 CHECK(cudaHostAlloc(&h_terminal,uint64_t(Count)*sizeof(*h_terminal),cudaHostAllocMapped));
 CHECK(cudaHostGetDevicePointer(&d_terminal,h_terminal,0));
 CHECK(cudaHostGetDevicePointer(&d_flags,h_flags,0));CHECK(cudaHostGetDevicePointer(&mailbox.results,h_results,0));CHECK(cudaHostGetDevicePointer(&mailbox.stats,h_stats,0));
 mailbox.ready=d_flags;mailbox.stop=d_flags+Count;mailbox.done=d_flags+Count+1;
 CHECK(cudaMemcpy(d_primes,primes.data(),primes.size()*8,cudaMemcpyHostToDevice));CHECK(cudaMemcpy(d_reciprocals,reciprocals.data(),primes.size()*8,cudaMemcpyHostToDevice));
 CHECK(cudaMemcpy(d_r64,r64.data(),r64.size()*8,cudaMemcpyHostToDevice));CHECK(cudaMemcpy(d_items,item_pidx.data(),item_pidx.size()*4,cudaMemcpyHostToDevice));
 CHECK(cudaMemcpy(d_chunks,item_k0.data(),item_k0.size()*4,cudaMemcpyHostToDevice));CHECK(cudaMemcpy(d_wheel,wheel_primes,sizeof(wheel_primes),cudaMemcpyHostToDevice));
 CHECK(cudaMemcpy(d_steps,steps,sizeof(steps),cudaMemcpyHostToDevice));CHECK(cudaMemcpy(d_descriptors,descriptors.data(),uint64_t(Count)*sizeof(PgnWalkDescriptor),cudaMemcpyHostToDevice));
 buffers.primes=d_primes;buffers.invp=d_reciprocals;buffers.r64=d_r64;buffers.item_pidx=d_items;buffers.item_k0=d_chunks;buffers.wheel_primes=d_wheel;buffers.window_steps=d_steps;buffers.item_count=unsigned(item_pidx.size());
 CHECK(cudaMemset(buffers.bitmaps,0,bitmapBytes));CHECK(cudaMemset(buffers.results,0,resultBytes));CHECK(cudaMemset(buffers.stats,0,uint64_t(Count)*16));CHECK(cudaMemset(buffers.proofs,0,uint64_t(Count)*Capacity*4));
 CHECK(cudaMemset(d_visits,0,uint64_t(Count)*4));const PgnWalkGraphState initial{0,Count,0};CHECK(cudaMemcpy(d_state,&initial,sizeof(initial),cudaMemcpyHostToDevice));
 std::memset(h_flags,0,uint64_t(Count+2)*4);std::memset(h_results,0,resultBytes);std::memset(h_stats,0,uint64_t(Count)*16);
 for(unsigned i=0;i<Count;++i)h_terminal[i]={};
#ifdef PGN_PHASE_TIMING
 PgnPhaseTiming* d_trace{};CHECK(cudaMalloc(&d_trace,Count*sizeof(PgnPhaseTiming)));CHECK(cudaMemset(d_trace,0,Count*sizeof(PgnPhaseTiming)));
#ifdef PGN_CTA_CENSUS
 static_assert(92U*SlotBlocks<65536U&&WalkBlocks<65536U,"Atomic packed CTA counters cannot carry into peer field");
 const unsigned zeroLive=0;std::vector<PgnCtaCensus> census(Count);
 CHECK(cudaMemcpyToSymbol(pgn_cta_live,&zeroLive,sizeof(zeroLive)));CHECK(cudaMemcpyToSymbol(pgn_cta_census,census.data(),Count*sizeof(PgnCtaCensus)));
#endif
#endif
 cudaGraph_t graph{};cudaGraphExec_t executable{};CHECK(cudaGraphCreate(&graph,0));cudaGraphConditionalHandle handle{};CHECK(cudaGraphConditionalHandleCreate(&handle,graph,1,cudaGraphCondAssignDefault));
 unsigned current=0,ahead=1;
 auto mark=[&](cudaGraph_t target,unsigned* distance,cudaGraphNode_t* node){void* args[]={&d_descriptors,&d_state,&buffers,distance};cudaGraphNodeParams p{};p.type=cudaGraphNodeTypeKernel;
  p.kernel.func=reinterpret_cast<void*>(pgn_graph_mark);p.kernel.gridDim=dim3(92,SlotBlocks);p.kernel.blockDim=dim3(512);p.kernel.sharedMemBytes=MarkSharedBytes;p.kernel.kernelParams=args;return cudaGraphAddNode(node,target,nullptr,0,&p);
 };
 cudaGraphNode_t first{},loopNode{},nextMark{},walk{},tailBegin{},terminal{},verifyBegin{},proofNode{},advance{};CHECK(mark(graph,&current,&first));
 cudaGraphNodeParams loop{};loop.type=cudaGraphNodeTypeConditional;loop.conditional.handle=handle;loop.conditional.type=cudaGraphCondTypeWhile;loop.conditional.size=1;CHECK(cudaGraphAddNode(&loopNode,graph,&first,1,&loop));
 const auto body=loop.conditional.phGraph_out[0];CHECK(mark(body,&ahead,&nextMark));
 PgnSourceControl control{};control.mailbox=mailbox;control.outer=handle;
 CHECK(cudaGraphConditionalHandleCreate(&control.tail,body,0,cudaGraphCondAssignDefault));
 CHECK(cudaGraphConditionalHandleCreate(&control.verify,body,0,cudaGraphCondAssignDefault));
 CHECK(cudaGraphConditionalHandleCreate(&control.publish,body,0,cudaGraphCondAssignDefault));
 void* workArgs[]={&d_descriptors,&d_state,&buffers};cudaGraphNodeParams work{};work.type=cudaGraphNodeTypeKernel;work.kernel.func=reinterpret_cast<void*>(pgn_graph_walk);work.kernel.gridDim=dim3(WalkBlocks);work.kernel.blockDim=dim3(WalkThreads);work.kernel.kernelParams=workArgs;
 CHECK(cudaGraphAddNode(&walk,body,nullptr,0,&work));
 unsigned primeCount=unsigned(primes.size());uint64_t boundaryResumeLow=resumeLow,boundaryRequestedEnd=requestedEnd;
 void* tailBeginArgs[]={&d_descriptors,&d_state,&buffers,&boundaryResumeLow,&boundaryRequestedEnd,&d_tail,&control};
 work.kernel.func=reinterpret_cast<void*>(pgn_source_terminal_begin);work.kernel.gridDim=dim3(1);work.kernel.blockDim=dim3(1);work.kernel.kernelParams=tailBeginArgs;
 CHECK(cudaGraphAddNode(&tailBegin,body,&walk,1,&work));
 cudaGraphNodeParams tailLoop{};tailLoop.type=cudaGraphNodeTypeConditional;tailLoop.conditional.handle=control.tail;tailLoop.conditional.type=cudaGraphCondTypeWhile;tailLoop.conditional.size=1;
 CHECK(cudaGraphAddNode(&terminal,body,&tailBegin,1,&tailLoop));
 void* terminalArgs[]={&d_state,&buffers,&primeCount,&d_tail,&d_terminal,&control};
 work.kernel.func=reinterpret_cast<void*>(pgn_source_terminal_step);work.kernel.kernelParams=terminalArgs;cudaGraphNode_t tailStep{};
 CHECK(cudaGraphAddNode(&tailStep,tailLoop.conditional.phGraph_out[0],nullptr,0,&work));
 void* verifyBeginArgs[]={&d_state,&buffers,&d_tail,&d_verify,&control};
 work.kernel.func=reinterpret_cast<void*>(pgn_source_verify_begin);work.kernel.kernelParams=verifyBeginArgs;
 CHECK(cudaGraphAddNode(&verifyBegin,body,&terminal,1,&work));
 cudaGraphNodeParams verifyLoop{};verifyLoop.type=cudaGraphNodeTypeConditional;verifyLoop.conditional.handle=control.verify;verifyLoop.conditional.type=cudaGraphCondTypeWhile;verifyLoop.conditional.size=1;
 CHECK(cudaGraphAddNode(&proofNode,body,&verifyBegin,1,&verifyLoop));
 void* verifyArgs[]={&d_state,&buffers,&d_verify,&control};cudaGraphNode_t proofStep{},proofControl{};
 work.kernel.func=reinterpret_cast<void*>(pgn_source_verify_step);work.kernel.gridDim=dim3((Capacity+127)/128);work.kernel.blockDim=dim3(128);work.kernel.kernelParams=verifyArgs;
 CHECK(cudaGraphAddNode(&proofStep,verifyLoop.conditional.phGraph_out[0],nullptr,0,&work));
 work.kernel.func=reinterpret_cast<void*>(pgn_source_verify_control);work.kernel.gridDim=dim3(1);work.kernel.blockDim=dim3(1);
 CHECK(cudaGraphAddNode(&proofControl,verifyLoop.conditional.phGraph_out[0],&proofStep,1,&work));
 const cudaGraphNode_t dependencies[]={nextMark,proofNode};void* advanceArgs[]={&d_state,&d_visits,&buffers,&mailbox,&handle};
 cudaGraphNodeParams publishGate{};publishGate.type=cudaGraphNodeTypeConditional;publishGate.conditional.handle=control.publish;publishGate.conditional.type=cudaGraphCondTypeIf;publishGate.conditional.size=1;
 CHECK(cudaGraphAddNode(&advance,body,dependencies,2,&publishGate));
 work.kernel.func=reinterpret_cast<void*>(pgn_graph_advance);work.kernel.kernelParams=advanceArgs;cudaGraphNode_t publishAdvance{};
 CHECK(cudaGraphAddNode(&publishAdvance,publishGate.conditional.phGraph_out[0],nullptr,0,&work));
#ifdef PGN_PHASE_TIMING
 auto stamp=[&](cudaGraph_t target,unsigned channel,int offset,const cudaGraphNode_t* deps,size_t n,cudaGraphNode_t* node){
  void* args[]={&d_state,&d_trace,&channel,&offset};cudaGraphNodeParams p{};p.type=cudaGraphNodeTypeKernel;
  p.kernel.func=reinterpret_cast<void*>(pgn_graph_stamp);p.kernel.gridDim=dim3(1);p.kernel.blockDim=dim3(1);p.kernel.kernelParams=args;
  return cudaGraphAddNode(node,target,deps,n,&p);
 };
 cudaGraphNode_t firstBegin{},firstEnd{},markBegin{},markEnd{},walkBegin{},walkEnd{},proofBegin{},proofEnd{},publishBegin{},publishEnd{};
 CHECK(stamp(graph,0,0,nullptr,0,&firstBegin));CHECK(cudaGraphAddDependencies(graph,&firstBegin,&first,1));
 CHECK(stamp(graph,1,0,&first,1,&firstEnd));CHECK(cudaGraphAddDependencies(graph,&firstEnd,&loopNode,1));
 CHECK(stamp(body,0,1,nullptr,0,&markBegin));CHECK(cudaGraphAddDependencies(body,&markBegin,&nextMark,1));CHECK(stamp(body,1,1,&nextMark,1,&markEnd));
 CHECK(stamp(body,2,0,nullptr,0,&walkBegin));CHECK(cudaGraphAddDependencies(body,&walkBegin,&walk,1));CHECK(stamp(body,3,0,&walk,1,&walkEnd));
 CHECK(cudaGraphAddDependencies(body,&walkEnd,&tailBegin,1));
 CHECK(stamp(body,4,0,&terminal,1,&proofBegin));CHECK(cudaGraphAddDependencies(body,&proofBegin,&verifyBegin,1));CHECK(stamp(body,5,0,&proofNode,1,&proofEnd));
 const cudaGraphNode_t stampDeps[]={markEnd,proofEnd};CHECK(stamp(body,6,0,stampDeps,2,&publishBegin));CHECK(cudaGraphAddDependencies(body,&publishBegin,&advance,1));
#ifdef PGN_PHASE_SERIAL
 // Closed causal ablation only: eliminate concurrent mark/walk resource use.
 // Data/ownership/order stay exact; this is not a production scheduling change.
 CHECK(cudaGraphAddDependencies(body,&proofEnd,&markBegin,1));
#endif
 // A paused advance does NOT increment cursor. Never overwrite the preceding
 // accepted descriptor's trace stamp when this descriptor was not published.
 void* publishedArgs[]={&d_state,&d_tail,&d_trace};cudaGraphNodeParams published{};published.type=cudaGraphNodeTypeKernel;
 published.kernel.func=reinterpret_cast<void*>(pgn_source_stamp_published);published.kernel.gridDim=dim3(1);published.kernel.blockDim=dim3(1);published.kernel.kernelParams=publishedArgs;
 CHECK(cudaGraphAddNode(&publishEnd,body,&advance,1,&published));
#endif
 CHECK(cudaGraphInstantiate(&executable,graph,nullptr,nullptr,0));cudaStream_t stream{};CHECK(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));
 cudaEvent_t ended{};CHECK(cudaEventCreate(&ended));CHECK(cudaDeviceSynchronize());
 std::printf("[phase0-gpu] engine=GPU_OWNED_CONTINUOUS descriptors=%u original_start=%s length=%llu proof=GPU_MR12_GAP coverage=SOURCE_PRP_INDEPENDENT_PROOF_PENDING boundary_protocol=pgn-source-boundary-v2\n",Count,startText.c_str(),(unsigned long long)length);
 std::ofstream gapLog;if(!logFile.empty()){gapLog.open(logFile,std::ios::app);if(!gapLog){std::fprintf(stderr,"Source log cannot open; no launch\n");return 1;}}
#ifdef PGN_PHASE_TIMING
 cudaEvent_t traceStarted{};CHECK(cudaEventCreate(&traceStarted));CHECK(cudaEventRecord(traceStarted,stream));
#endif
 CHECK(cudaGraphLaunch(executable,stream));CHECK(cudaEventRecord(ended,stream));
 const auto submitted=cudaStreamQuery(stream);if(submitted!=cudaSuccess&&submitted!=cudaErrorNotReady){std::fprintf(stderr,"Initial dispatch failed\n");return 2;}
 unsigned next=0;uint64_t tests=saved.tests,gaps=saved.gaps,batches=saved.batches,positions=saved.positions,lastSaved=observedNs();
 bool stop=false;volatile unsigned* flags=h_flags;
 // There is deliberately NO CUDA API, search, primality verifier or next-work
 // submission in this live host loop. Payloads are immutable GPU publications.
 for(;;){
  MemoryBarrier();while(next<Count&&flags[next]){
   MemoryBarrier();const auto* rows=h_results+uint64_t(next)*Capacity;const unsigned count=rows[0].gap;if(count>=Capacity)return 4;
   const auto publication=pgn::source::captureBatchPublicationTime();
   const auto d=descriptors[next];const uint64_t ownedEnd=d.low+uint64_t(d.owned_blocks)*BlockNumbers;
   for(unsigned row=1;row<=count;++row){const auto g=rows[row];
    if(!pgn_source_boundary::owns(g.slot,resumeLow,requestedEnd)||!pgn_source_boundary::owns(g.slot,d.low,ownedEnd))continue;
    const std::string p=decimal({g.slot,requested.high}),q=decimal({g.slot+g.gap,requested.high});
    const double merit=double(g.gap)/std::log(double(requested.high)*18446744073709551616.0+double(g.slot));
    char line[400];pgn::source::formatSourceGap(line,sizeof(line),merit,g.gap,p.c_str(),q.c_str(),publication);
    std::fputs(line,stdout);if(gapLog)gapLog<<line;++gaps;
   }
   const auto tail=h_terminal[next];char boundary[512];
   const uint64_t ownedFirst=resumeLow>d.low?resumeLow:d.low;
   const uint64_t clippedEnd=requestedEnd<ownedEnd?requestedEnd:ownedEnd;
   if(!formatTerminalBoundaryV2(boundary,sizeof(boundary),next,ownedFirst,clippedEnd,requested.high,tail))return 9;
   // Like existing point verification, terminal SOURCE probes are distinct
   // from historical walk.tests. Lookahead NEVER expands positions/ownership.
   std::fputs(boundary,stdout);
   tests+=h_stats[2*next];++batches;++next;
   positions=saved.positions+std::min<uint64_t>(remaining,uint64_t(next)*K*BlockNumbers-remainder);
   auto prefix=saved;prefix.positions=positions;prefix.tests=tests;prefix.gaps=gaps;prefix.batches=batches;
   char counter[300];formatPrefixCounter(counter,sizeof(counter),observedNs(),prefix);std::fputs(counter,stdout);
   if(observedNs()-lastSaved>30000000000ULL){auto checkpoint=saved;checkpoint.positions=positions;checkpoint.tests=tests;checkpoint.gaps=gaps;checkpoint.batches=batches;if(!writeCheckpoint(stateFile,checkpoint))return 5;lastSaved=observedNs();}
  }
  const unsigned done=flags[Count+1];if(done){MemoryBarrier();if(next<Count&&flags[next])continue;break;}
  if(!stop&&exists(stopFile)){MemoryBarrier();flags[Count]=1;MemoryBarrier();stop=true;}
  Sleep(1);
 }
 CHECK(cudaEventSynchronize(ended));
 if(flags[Count+1]!=1){const auto tail=h_terminal[next<Count?next:Count-1];std::fprintf(stderr,"GPU job failed closed: status%u, accepted prefix%u, terminal_status%u (low64/gap32/predicate/publication limits never imply SOURCE completion)\n",flags[Count+1],next,unsigned(tail.status));return 6;}
 if(next<Count){auto checkpoint=saved;checkpoint.positions=positions;checkpoint.tests=tests;checkpoint.gaps=gaps;checkpoint.batches=batches;
  if(!writeCheckpoint(stateFile,checkpoint))return 5;
  // Even STOP before the first ready of this process needs a genuine current
  // source counter. Its values are the actual saved/accepted prefix, NOT work
  // from the interrupted descriptor and NOT fabricated default counters.
  char counter[300];formatPrefixCounter(counter,sizeof(counter),observedNs(),checkpoint);std::fputs(counter,stdout);
  std::printf("[phase0-gpu] stopped positions=%llu checkpoint=%s\n",(unsigned long long)positions,stateFile.c_str());return 0;
 }
#ifdef PGN_PRP_CALL_CENSUS
 if(next!=Count){std::fprintf(stderr,"Call census source incomplete; no inference\n");return 8;}
 std::vector<unsigned long long> callCounts(uint64_t(Count)*WalkJobs);
 CHECK(cudaMemcpy(callCounts.data(),buffers.call_counts,callCounts.size()*sizeof(unsigned long long),cudaMemcpyDeviceToHost));
 for(unsigned i=0;i<Count;++i){
  unsigned long long sum=0,warpMaximumSum=0,minimum=UINT64_MAX,maximum=0;
  for(unsigned t=0;t<WalkJobs;t+=32){unsigned long long warpMaximum=0;
   for(unsigned lane=0;lane<32;++lane){const auto value=callCounts[uint64_t(i)*WalkJobs+t+lane];
    if(value==UINT64_MAX){std::fprintf(stderr,"Unwritten call census entry; no fabricated zero\n");return 8;}
    sum+=value;minimum=std::min(minimum,value);maximum=std::max(maximum,value);warpMaximum=std::max(warpMaximum,value);
   }warpMaximumSum+=warpMaximum;
  }
  if(sum!=h_stats[2*i]){std::fprintf(stderr,"Call census conservation failed\n");return 8;}
  std::printf("[pgn-prp-call-census] descriptor=%u jobs=%u calls=%llu warp_max_sum=%llu min=%llu max=%llu scope=LOGICAL_CALL_DISTRIBUTION_NOT_HARDWARE_UTILIZATION\n",i,WalkJobs,sum,warpMaximumSum,minimum,maximum);
 }
 {std::ofstream output(CallPath,std::ios::binary|std::ios::trunc);const char magic[8]={'P','G','N','P','R','P','C','1'};
  const uint32_t layout[4]={Count,WalkJobs,WalkThreads,0};output.write(magic,8);output.write(reinterpret_cast<const char*>(layout),sizeof(layout));
  output.write(reinterpret_cast<const char*>(callCounts.data()),callCounts.size()*sizeof(unsigned long long));output.flush();if(!output){std::fprintf(stderr,"Call census output failed\n");return 8;}
 }
 CHECK(cudaFree(buffers.call_counts));
#endif
#ifdef PGN_PHASE_TIMING
 std::vector<PgnPhaseTiming> trace(Count);CHECK(cudaMemcpy(trace.data(),d_trace,Count*sizeof(PgnPhaseTiming),cudaMemcpyDeviceToHost));
 float gpuMs=0;CHECK(cudaEventElapsedTime(&gpuMs,traceStarted,ended));
 if(next!=Count){std::fprintf(stderr,"Incomplete diagnostic trace preserved; no phase inference\n");return 8;}
 for(unsigned i=0;i<Count;++i){const auto* t=trace[i].stamp;
  for(unsigned channel=0;channel<8;++channel)if(!t[channel]){std::fprintf(stderr,"Missing GPU phase timestamp; no invented zero\n");return 8;}
  if(t[1]<t[0]||t[2]<t[1]||t[3]<t[2]||t[4]<t[3]||t[5]<t[4]||t[6]<t[5]||t[7]<t[6]||(i+1<Count&&t[6]<trace[i+1].stamp[1])){std::fprintf(stderr,"GPU phase dependency/clock invariant failed\n");return 8;}
 }
 const uint64_t span=trace.back().stamp[7]-trace.front().stamp[0];
 const double tolerance=1000000.0+double(gpuMs)*1000000.0*0.05;
 if(!(gpuMs>0)||std::fabs(double(span)-double(gpuMs)*1000000.0)>tolerance){std::fprintf(stderr,"Globaltimer/CUDA event calibration failed; raw evidence only\n");return 8;}
 std::printf("[pgn-phase] calibration globaltimer_span=%llu cuda_event_ms=%.6f count=%u scope=NATIVE_SM86_DIAGNOSTIC_NOT_PRODUCTION\n",(unsigned long long)span,double(gpuMs),Count);
 for(unsigned i=0;i<Count;++i){const auto* t=trace[i].stamp;
  std::printf("[pgn-phase] descriptor=%u mark_begin=%llu mark_end=%llu walk_begin=%llu walk_end=%llu verify_begin=%llu verify_end=%llu publish_begin=%llu publish_end=%llu\n",i,(unsigned long long)t[0],(unsigned long long)t[1],(unsigned long long)t[2],(unsigned long long)t[3],(unsigned long long)t[4],(unsigned long long)t[5],(unsigned long long)t[6],(unsigned long long)t[7]);
 }
 CHECK(cudaFree(d_trace));CHECK(cudaEventDestroy(traceStarted));
#ifdef PGN_CTA_CENSUS
 unsigned finalLive=0;CHECK(cudaMemcpyFromSymbol(&finalLive,pgn_cta_live,sizeof(finalLive)));CHECK(cudaMemcpyFromSymbol(census.data(),pgn_cta_census,Count*sizeof(PgnCtaCensus)));
 if(finalLive){std::fprintf(stderr,"CTA census unbalanced; no concurrency inference\n");return 8;}
 for(unsigned i=0;i<Count;++i){const auto& c=census[i];
  if(c.mark_entered!=92U*descriptors[i].bitmap_blocks||c.walk_entered!=WalkBlocks||c.mark_saw_walk>c.mark_entered||c.walk_saw_mark>c.walk_entered){std::fprintf(stderr,"CTA census coverage/count invalid\n");return 8;}
  std::printf("[pgn-cta-census] descriptor=%u mark_entered=%u walk_entered=%u mark_saw_walk=%u walk_saw_mark=%u final_live=%u scope=INSTRUMENTED_CTA_LIFETIME_NOT_SPEED_GAIN\n",i,c.mark_entered,c.walk_entered,c.mark_saw_walk,c.walk_saw_mark,finalLive);
 }
#endif
#endif
#ifdef PGN_EULER68_MIXED
 unsigned long long unsupported=0,invariant=0;
 CHECK(cudaMemcpyFromSymbol(&unsupported,pgn_euler68_admitted::unsupported_count,sizeof(unsupported)));
 CHECK(cudaMemcpyFromSymbol(&invariant,pgn_euler68_admitted::invariant_count,sizeof(invariant)));
 std::printf("[pgn-candidate] euler68_fallback unsupported=%llu invariant=%llu\n",unsupported,invariant);
#endif
 if(positions!=length){std::fprintf(stderr,"Exact final positions mismatch\n");return 7;}
 if(exists(stateFile)){const auto archived=stateFile+".completed-"+std::to_string(GetCurrentProcessId());if(!MoveFileExA(stateFile.c_str(),archived.c_str(),MOVEFILE_WRITE_THROUGH))return 5;}
 std::printf("[phase0-gpu] gaps reported=%llu verification_failures=0 records_new=0\n[phase0-gpu] --- end ---\n",(unsigned long long)gaps);
 return 0;
}
#endif // !PGN_SOURCE_BOUNDARY_CLOSED_TEST


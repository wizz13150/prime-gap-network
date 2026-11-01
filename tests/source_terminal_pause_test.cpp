// CLOSED finite CPU test of actual shared tail/proof steps and the actual
// pgn_graph_advance body extracted from the pinned header by the test runner.
#define PGN_SOURCE_BOUNDARY_CLOSED_TEST 1
#include "../tools/continuous-gpu-runtime.cu"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>
namespace b=pgn_source_boundary;
static unsigned checks=0,fences=0;
static void check(bool ok,const char* message){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
struct w_gaprec_t {uint64_t slot;uint32_t gap,pad;};
struct PgnWalkGraphState {uint32_t cursor,count,completed;};
struct PgnWalkGraphBuffers {w_gaprec_t* results;unsigned long long* stats;unsigned* proofs;unsigned result_capacity;};
struct PgnWalkMailbox {w_gaprec_t* results;unsigned long long* stats;volatile unsigned *ready,*stop,*done;};
using cudaGraphConditionalHandle=unsigned*;
static void __threadfence_system(){++fences;}
static void cudaGraphSetConditional(cudaGraphConditionalHandle handle,unsigned value){*handle=value;}
#define __global__
#define PGN_VERIFY_GAPS 1
#include "source-advance-extracted.h"
#undef __global__
struct Source {
 std::set<uint64_t> accepted{11,19,23};std::vector<uint64_t> visited;
 int operator()(uint64_t n){visited.push_back(n);return accepted.count(n)?1:0;}
};
struct Proof {
 std::vector<uint64_t> visited;
 static bool prime(uint64_t n){if(n<2)return false;for(uint64_t d=2;d<=n/d;++d)if(n%d==0)return false;return true;}
 int endpoint(uint64_t n){visited.push_back(n);return prime(n)?1:0;}
 int composite(uint64_t n){visited.push_back(n);return prime(n)?0:1;}
};
static void advanceCase(bool stop,unsigned proof,unsigned count,unsigned expectedDone,bool commits){
 constexpr unsigned Capacity=4;w_gaprec_t rows[12]{},published[12]{};
 unsigned long long stats[6]{},publishedStats[6]{};unsigned proofs[12]{},visits[3]{},ready[3]{1,0,0};
 rows[Capacity].gap=count;rows[Capacity+1]={19,4,0};proofs[Capacity+1]=proof;
 stats[2]=9007199254740993ULL;published[Capacity].slot=777;publishedStats[2]=888;
 PgnWalkGraphState state{1,3,1};PgnWalkGraphBuffers buffers{rows,stats,proofs,Capacity};
 unsigned stopFlag=stop?1:0,done=0,handle=1;PgnWalkMailbox mailbox{published,publishedStats,ready,&stopFlag,&done};
 pgn_graph_advance(&state,visits,buffers,mailbox,&handle);
 check(done==expectedDone,"real advance preserves normal-pause versus failure status");
 check(state.cursor==(commits?2u:1u)&&state.completed==(commits?2u:1u),"real advance never increments unpaid prefix");
 check(ready[0]==1&&ready[1]==(commits?1u:0u)&&visits[1]==(commits?1u:0u),"STOP never sets current descriptor accepted/visit");
 check(published[Capacity].slot==(commits?0u:777u)&&publishedStats[2]==(commits?9007199254740993ULL:888ULL),"STOP cannot expose current rows or counters");
 check(handle==(commits?1u:0u),"paused/failed source stops the outer GPU loop");
}
int main(){
 Source reference;auto complete=b::begin(11,22);
 while(complete.phase==b::Phase::Backward||complete.phase==b::Phase::Forward)b::step(complete,reference,false);
 check(complete.phase==b::Phase::Ready&&complete.value.p==19&&complete.value.q==23,"complete tail closes actual oracle relation");
 for(unsigned quantum:{1u,2u,7u,16u}){
  Source source;auto state=b::begin(11,22);unsigned invocations=0;
  while(state.phase==b::Phase::Backward||state.phase==b::Phase::Forward){
   const auto before=source.visited.size();
   for(unsigned i=0;i<quantum&&(state.phase==b::Phase::Backward||state.phase==b::Phase::Forward);++i)b::step(state,source,false);
   check(source.visited.size()-before<=quantum,"work per invocation is bounded, not total search padding");++invocations;
  }
  check(invocations>0&&state.value.p==19&&state.value.q==23&&source.visited==reference.visited,"resumable quanta neither restart nor skip search work");
 }
 for(size_t stopAt=0;stopAt<reference.visited.size();++stopAt){
  Source source;auto state=b::begin(11,22);
  while(state.phase==b::Phase::Backward||state.phase==b::Phase::Forward)b::step(state,source,source.visited.size()>=stopAt);
  check(state.phase==b::Phase::Paused&&state.value.status==b::Status::Stopped,"STOP in backward/forward is typed Pause, not failure");
  check(source.visited==std::vector<uint64_t>(reference.visited.begin(),reference.visited.begin()+stopAt),"STOP performs no later predicate or speculative row");
  w_gaprec_t rows[4]{};check(!b::merge(rows,4,state.value,11,22,2)&&rows[0].gap==0,"incomplete tail cannot merge or pretend descriptor complete");
 }
 struct PredicateStop {int operator()(uint64_t){return -2;}} predicateStop;
 auto stopped=b::begin(11,22);b::step(stopped,predicateStop,false);
 check(stopped.phase==b::Phase::Paused&&stopped.value.status==b::Status::Stopped,"STOP during bounded source primitive propagates distinctly");
 Proof proofReference;auto verified=b::beginVerify(19,23);
 while(verified.phase!=b::VerifyPhase::Ready&&verified.phase!=b::VerifyPhase::Failed)b::verifyStep(verified,proofReference,false);
 check(verified.phase==b::VerifyPhase::Ready&&proofReference.visited==std::vector<uint64_t>({19,23,20,21,22}),"actual shared proof steps include both endpoints and every interior integer");
 for(unsigned quantum:{1u,2u,16u}){
  Proof proof;auto state=b::beginVerify(19,23);
  while(state.phase!=b::VerifyPhase::Ready&&state.phase!=b::VerifyPhase::Failed){
   const auto before=proof.visited.size();for(unsigned i=0;i<quantum;++i)b::verifyStep(state,proof,false);
   check(proof.visited.size()-before<=quantum,"verification kernel cannot inherit an unbounded gap32 loop");
  }check(proof.visited==proofReference.visited,"verification resumes exact cursor, no lost/duplicate interior tests");
 }
 for(size_t stopAt=0;stopAt<proofReference.visited.size();++stopAt){
  Proof proof;auto state=b::beginVerify(19,23);
  while(state.phase!=b::VerifyPhase::Ready&&state.phase!=b::VerifyPhase::Paused&&state.phase!=b::VerifyPhase::Failed)b::verifyStep(state,proof,proof.visited.size()>=stopAt);
  check(state.phase==b::VerifyPhase::Paused&&proof.visited.size()==stopAt,"STOP in endpoint/interior verification stays unpublished Pause");
 }
 Proof poisoned;auto invalid=b::beginVerify(19,29);
 while(invalid.phase!=b::VerifyPhase::Ready&&invalid.phase!=b::VerifyPhase::Failed)b::verifyStep(invalid,poisoned,false);
 check(invalid.phase==b::VerifyPhase::Failed,"a real interior prime remains a failed proof");b::verifyStep(invalid,poisoned,true);
 check(invalid.phase==b::VerifyPhase::Failed,"later STOP cannot relabel a known proof failure");
 advanceCase(true,0,1,1,false);advanceCase(true,1,1,1,false);
 advanceCase(true,2,1,3,false);advanceCase(true,1,4,2,false);
 advanceCase(false,0,1,3,false);advanceCase(false,1,1,0,true);
 check(fences>=6,"actual publisher keeps system-release boundaries");
 for(uint64_t positions:{0ULL,123ULL}){
  Checkpoint prefix{{'P','G','N','C','G','0','0','1'},100,10,10000,positions,9007199254740993ULL,7,2};
  const auto original=prefix;char counter[300];formatPrefixCounter(counter,sizeof(counter),9007199254740995ULL,prefix);
  const std::string expected="[pgn-counters] time_ns=9007199254740995 positions="+std::to_string(positions)+" gaps=7 tests=9007199254740993 batches=2 processed_gaps=7\n";
  check(counter==expected,"genuine source counter formatter preserves zero/resumed accepted prefix and >2^53 tuple");
  check(sizeof(prefix)==64&&std::memcmp(&original,&prefix,64)==0&&prefix.positions<prefix.length,"STOP tuple does not alter baseline, checkpoint ABI or claim full range");
 }
 std::printf("source terminal pause checks=%u failures=0; shared bounded steps + actual extracted publisher + prefix formatter, finite CPU only; no CUDA/latency/TDR/physical qualification\n",checks);
}

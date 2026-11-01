// CLOSED CPU relation test. This includes the production helper byte-for-byte;
// the finite oracle below is NOT the GPU SOURCE predicate or physical evidence.
#define PGN_SOURCE_BOUNDARY_CLOSED_TEST 1
#include "../tools/continuous-gpu-runtime.cu"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <utility>
#include <vector>
namespace b=pgn_source_boundary;
static unsigned checks=0;
static void check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
struct Row{uint64_t slot{};uint32_t gap{},pad{};};
struct Prime {
 std::vector<uint64_t> visited;
 int operator()(uint64_t n){visited.push_back(n);if(n<2)return 0;for(uint64_t d=2;d<=n/d;++d)if(n%d==0)return 0;return 1;}
};
struct Accepted {
 std::set<uint64_t> values;std::vector<uint64_t> visited;
 int operator()(uint64_t n){visited.push_back(n);return values.count(n)?1:0;}
};
static std::vector<std::pair<uint64_t,uint64_t>> oracle(uint64_t first,uint64_t end,unsigned minimum){
 // An independent ascending exhaustive construction, not the terminal search.
 std::vector<std::pair<uint64_t,uint64_t>> result;Prime prime;
 for(uint64_t p=first;p<end;++p)if(prime(p)){
  uint64_t q=p+1;while(!prime(q))++q;if(q-p>=minimum)result.push_back({p,q});
 }return result;
}
static std::vector<std::pair<uint64_t,uint64_t>> descriptor(uint64_t first,uint64_t end,uint64_t bitmapEnd,unsigned minimum){
 Row rows[256]{};
 // A finite old-walk publication fixture: rows are missing if q is outside
 // its bitmap. Only the exact shared helper repairs that missing terminal.
 for(const auto& pair:oracle(first,end,minimum))if(pair.second<bitmapEnd){
  ++rows[0].gap;rows[rows[0].gap]={pair.first,uint32_t(pair.second-pair.first),0};
 }
 Prime prime;const auto tail=b::close(first,end,prime);
 check(b::merge(rows,256,tail,first,end,minimum),"valid bounded terminal merges");
 std::vector<std::pair<uint64_t,uint64_t>> result;
 for(unsigned r=1;r<=rows[0].gap;++r)if(b::owns(rows[r].slot,first,end))result.push_back({rows[r].slot,rows[r].slot+rows[r].gap});
 std::sort(result.begin(),result.end());return result;
}
int main(){
 {
  Prime prime;const auto tail=b::close(11,20,prime);
  check(tail.status==b::Status::Pair&&tail.p==19&&tail.q==23,"last owned p closes with real next prime beyond end");
  check((prime.visited==std::vector<uint64_t>{19,20,21,22,23}),"no padding: every intervening position and first q visited exactly");
  check(!b::owns(7,11,20)&&b::owns(11,11,20)&&b::owns(19,11,20)&&!b::owns(20,11,20)&&!b::owns(23,11,20),"ownership applies to p only, half open");
  check(descriptor(11,20,20,2)==oracle(11,20,2),"old bitmap exhaustion repaired, q outside bitmap/range retained");
  check(descriptor(11,23,23,2)==oracle(11,23,2),"q equal to requested end is not dropped");
 }
 for(uint64_t first=2;first<70;++first)for(uint64_t length=1;length<=19;++length)for(unsigned minimum:{2u,4u,8u}){
  const auto end=first+length;
  check(descriptor(first,end,end,minimum)==oracle(first,end,minimum),"exhaustive finite prime intervals agree with independent oracle");
 }
 {
  const auto before=descriptor(11,20,20,2),after=descriptor(20,40,40,2);
  auto combined=before;combined.insert(combined.end(),after.begin(),after.end());
  check(combined==oracle(11,40,2),"internal descriptors neither lose nor duplicate boundary gap");
  check(std::count(combined.begin(),combined.end(),std::make_pair(uint64_t(19),uint64_t(23)))==1,"cross-descriptor p19 q23 emitted once");
  check(after==oracle(20,40,2),"checkpoint resume excludes all previously owned p below resume boundary");
  Prime prime;const auto empty=b::close(14,17,prime);Row rows[4]{};
  check(empty.status==b::Status::NoOwnedPrime&&b::merge(rows,4,empty,14,17,2)&&rows[0].gap==0,"no owned prime does not adopt previous p13 or outside p17");
  check((prime.visited==std::vector<uint64_t>{16,15,14}),"empty interval performs no future-prime search");
  prime.visited.clear();check(b::close(17,17,prime).status==b::Status::NoOwnedPrime&&prime.visited.empty(),"empty ownership needs no speculative lookahead");
 }
 {
  Accepted source{{1000,1799,2599,3400},{}};const auto tail=b::close(1000,2600,source);Row rows[8]{};
  rows[0].gap=1;rows[1]={1799,800,0};
  check(tail.status==b::Status::Pair&&tail.p==2599&&tail.q==3400,"SOURCE oracle tail is not limited to arbitrary padding");
  check(b::merge(rows,8,tail,1000,2600,800)&&rows[0].gap==2&&rows[2].gap==801,"exact gapmin800 retained, gap799 excluded");
  check(b::merge(rows,8,tail,1000,2600,800)&&rows[0].gap==2,"existing exact terminal row is idempotent, no duplicate");
  rows[3]=rows[2];rows[0].gap=3;check(!b::merge(rows,8,tail,1000,2600,800),"duplicate source rows cannot be silently accepted");
  rows[0].gap=2;rows[2].gap=802;check(!b::merge(rows,8,tail,1000,2600,800),"same p with inconsistent q is Held");
  rows[0].gap=1;check(!b::merge(rows,2,tail,1000,2600,800)&&rows[0].gap==1,"full result queue refuses tail, never truncates");
  Accepted shortGap{{1000,1799},{}};const auto shortTail=b::close(1000,1001,shortGap);Row shortRows[2]{};
  check(b::merge(shortRows,2,shortTail,1000,1001,800)&&shortRows[0].gap==0,"subthreshold final gap remains excluded");
 }
 {
  check(b::gapFits(10,10+uint64_t(UINT32_MAX))&&!b::gapFits(10,11+uint64_t(UINT32_MAX)),"real gap32 representation boundary checked without billion-step fixture");
  check(!b::gapFits(UINT64_MAX,0),"wrapped q is not a gap");
  Accepted exhausted{{UINT64_MAX-1},{}};const auto tail=b::close(UINT64_MAX-1,UINT64_MAX,exhausted);Row rows[3]{};
  check(tail.status==b::Status::Low64Exhausted&&!b::merge(rows,3,tail,UINT64_MAX-1,UINT64_MAX,2),"low64 exhaustion cannot publish SOURCE completion");
  struct Unknown{int operator()(uint64_t n){return n==19?1:-1;}} unknown;
  const auto held=b::close(19,20,unknown);
  check(held.status==b::Status::PredicateUnavailable&&!b::merge(rows,3,held,19,20,2),"unknown future predicate is not prime or completion");
  Prime prime;check(b::close(20,19,prime).status==b::Status::InvalidRange,"invalid range fails closed");
  const auto last=b::close(11,20,prime);check(!b::merge(rows,1,last,11,20,2),"no usable queue capacity fails closed");
  rows[0].gap=3;check(!b::merge(rows,3,last,11,20,2),"preexisting overflow is Held before reading rows");
  rows[0].gap=1;rows[1]={13,4,0};Prime none;const auto noPrime=b::close(14,17,none);
  rows[1].slot=14;check(!b::merge(rows,3,noPrime,14,17,2),"no-prime result conflicts with an owned source row");
 }
 std::printf("source terminal boundary checks=%u failures=0; shared production relation, finite CPU oracles only; no GPU/SOURCE predicate qualification or independent coverage\n",checks);
}

// CPU ONLY: include the actual host formatter, never CUDA/driver/runtime.
#define PGN_SOURCE_BOUNDARY_CLOSED_TEST 1
#include "../tools/continuous-gpu-runtime.cu"
#include <cstdlib>
#include <cstring>
#include <limits>
#include <iostream>
namespace b=pgn_source_boundary;
static unsigned checks=0;
static void check(bool ok,const char* why){++checks;if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
 char text[512];const uint64_t m=std::numeric_limits<uint64_t>::max();
 check(decimal({m,m})=="340282366920938463463374607431768211455","full uint128 decimal, no low64 truncation");
 b::Terminal pair{m-100,m,1272,12,b::Status::Pair};
 check(formatTerminalBoundaryV2(text,sizeof(text),4294967295U,m-200,m-50,10,pair),"Pair v2 emits");
 const std::string expected="[pgn-source-boundary-v2] descriptor=4294967295 owned_first=202914184810805067575 owned_end=202914184810805067725 status=1 terminal_p=202914184810805067675 terminal_q=202914184810805067775 examined=1272 source_tests=12 scope=SOURCE_PRP_TERMINAL_NOT_INDEPENDENT_COVERAGE\n";
 check(text==expected,"exact ordered v2 bytes and absolute high10 coordinates");
 check(formatTerminalBoundaryV2(text,sizeof(text),0,m-200,m-50,m,pair),"max high remains representable");
 check(std::string(text).find("terminal_q=340282366920938463463374607431768211455 ")!=std::string::npos,"terminal q max uint128 retained");
 b::Terminal none{99,100,m,m,b::Status::NoOwnedPrime};
 check(formatTerminalBoundaryV2(text,sizeof(text),0,1,2,10,none),"NoOwnedPrime v2 emits");
 check(std::string(text).find("status=2 terminal_p=none terminal_q=none examined=18446744073709551615 source_tests=18446744073709551615 ")!=std::string::npos,"none tokens and uint64 costs exact");
 char shortText[16];std::memset(shortText,'x',sizeof(shortText));
 check(!formatTerminalBoundaryV2(shortText,sizeof(shortText),0,1,2,10,none)&&shortText[0]=='\0',"truncated line never publishable");
 check(!formatTerminalBoundaryV2(nullptr,0,0,1,2,10,none),"null/zero capacity held");
 for(unsigned status:{0U,3U,4U,5U,6U,7U,8U}){
  auto poison=pair;poison.status=static_cast<b::Status>(status);std::strcpy(text,"old");
  check(!formatTerminalBoundaryV2(text,sizeof(text),0,m-200,m-50,10,poison)&&text[0]=='\0',"nonaccepted status including 7 never marker");
 }
 auto poison=pair;poison.p=m-201;
 check(!formatTerminalBoundaryV2(text,sizeof(text),0,m-200,m-50,10,poison),"foreign terminal p held");
 poison=pair;poison.q=poison.p;
 check(!formatTerminalBoundaryV2(text,sizeof(text),0,m-200,m-50,10,poison),"nonforward terminal pair held");
 poison=pair;poison.q=m-51;
 check(!formatTerminalBoundaryV2(text,sizeof(text),0,m-200,m-50,10,poison),"terminal q cannot precede owned end");
 check(!formatTerminalBoundaryV2(text,sizeof(text),0,9,8,10,none),"inverted intersection held");
 std::cout<<"source terminal publication v2 checks="<<checks<<" failures=0; CPU formatter only, no SOURCE coverage\n";
}

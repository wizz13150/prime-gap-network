// Private bounded AUDIT-ONLY worker of the canonical sharedGPU scientific pipeline.
// It is not installed/admitted until independent port and runtime gates pass.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#endif
#include "shared_count68_port.h"
using namespace pgn_cpu_shared;
static uint64_t decimal64(const std::string&s){if(s.empty())throw std::runtime_error("Empty integer");uint64_t n=0;for(char c:s){if(c<'0'||c>'9'||n>(UINT64_MAX-uint64_t(c-'0'))/10)throw std::runtime_error("Invalid integer");n=n*10+uint64_t(c-'0');}return n;}
static std::string decimal96(Number96 n){uint32_t w[3]={uint32_t(n.low),uint32_t(n.low>>32),n.high};std::string text;do{uint64_t carry=0;for(int i=2;i>=0;i--){uint64_t v=(carry<<32)|w[i];w[i]=uint32_t(v/10);carry=v%10;}text.push_back(char('0'+carry));}while(w[0]||w[1]||w[2]);std::reverse(text.begin(),text.end());return text;}
static void pin(int cpu){
#ifdef _WIN32
 if(cpu<0||cpu>=64||!SetProcessAffinityMask(GetCurrentProcess(),DWORD_PTR(1)<<cpu))throw std::runtime_error("CPU affinity failed");DWORD_PTR mask=0,system=0;if(!GetProcessAffinityMask(GetCurrentProcess(),&mask,&system)||mask!=(DWORD_PTR(1)<<cpu))throw std::runtime_error("CPU affinity not verified");
#else
 if(cpu<0||cpu>=CPU_SETSIZE)throw std::runtime_error("CPU index invalid");cpu_set_t mask;CPU_ZERO(&mask);CPU_SET(cpu,&mask);if(sched_setaffinity(0,sizeof(mask),&mask))throw std::runtime_error("CPU affinity failed");
#endif
}
int main(int argc,char**argv){try{
 if(argc==2&&std::string(argv[1])=="--component-info"){std::cout<<"{\"schema\":\"PGN_CPU_SHARED_COMPONENT_V1\",\"bits\":[68],\"canonical_source_manifest_sha256\":\"96C600D3486DED57B400FCD4428E904F6A3743E49F587DC85E93847D9FFDDE13\",\"production_admitted\":false,\"audit_only\":true,\"generic_scientific_fallback\":false}\n";return 0;}
 if((argc<4||argc>6)||std::string(argv[1])!="--audit-only"||std::string(argv[2])!="--cpu")throw std::runtime_error("Explicit --audit-only --cpu INDEX required");
 const uint64_t selected=decimal64(argv[3]);if(selected>1023)throw std::runtime_error("CPU index invalid");pin(int(selected));bool simd4=false,tiled=false;for(int i=4;i<argc;i++){std::string option=argv[i];if(option=="--simd4")simd4=true;else if(option=="--tiled")tiled=true;else throw std::runtime_error("Unknown execution mode");}if(simd4&&!pgn_promoted_avx2::avx2_on_current_cpu())throw std::runtime_error("AVX2 not admitted on pinnedCPU/OS");const auto bank=canonicalPrimeBank();
 std::cout<<"{\"type\":\"ready\",\"backend\":\"cpu_shared_gpu_lineage68\",\"pinned_cpu\":"<<selected<<",\"affinity_verified\":true,\"audit_only\":true,\"production_admitted\":false}\n"<<std::flush;
 char buffer[1025];while(std::cin.getline(buffer,sizeof(buffer))){std::string line(buffer);if(line=="STOP")break;std::istringstream input(line);std::string action,idText,highText,lowText,lengthText,gapText,extra;
  if(!(input>>action>>idText>>highText>>lowText>>lengthText>>gapText)||input>>extra||action!="TASK")throw std::runtime_error("Invalid bounded auditpiece command");
  const uint64_t id=decimal64(idText),high=decimal64(highText),low=decimal64(lowText),length=decimal64(lengthText),gap=decimal64(gapText);
  if(high>UINT32_MAX||gap>2147483647)throw std::runtime_error("Invalid numeric domain");const Number96 start{low,uint32_t(high)};const auto began=std::chrono::steady_clock::now();
  const auto result=countOwned68(start,length,uint32_t(gap),bank,simd4,tiled);const auto end=addSmall(start,length);const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
  if(!result.complete||result.positions!=length)throw std::runtime_error("Unclosed sharedpipeline piece");
  std::cout.precision(12);std::cout<<"{\"schema\":\"PGN_CPU_SHARED_OWNED_PIECE_V1\",\"type\":\"result\",\"state\":\"COMPLETE\",\"backend\":\"cpu_shared_gpu_lineage68\",\"audit_only\":true,\"production_admitted\":false,\"job_id\":"<<id<<",\"start\":\""<<decimal96(start)<<"\",\"end\":\""<<decimal96(end)<<"\",\"positions\":\""<<result.positions<<"\",\"minimum_gap\":"<<gap<<",\"count\":\""<<result.retained<<"\",\"implementation\":\""<<(simd4?"shared_avx2_4owner":"shared_scalar")<<"\",\"candidate_tests\":"<<result.tests<<",\"jumps\":"<<result.jumps<<",\"exact_prefix_positions\":"<<result.exact_prefix_positions<<",\"pinned_cpu\":"<<selected<<",\"affinity_verified\":true,\"coverage_contract\":\"SOURCE_PRP_INDEPENDENT_PROOF_PENDING\",\"canonical_source_manifest_sha256\":\"96C600D3486DED57B400FCD4428E904F6A3743E49F587DC85E93847D9FFDDE13\",\"seconds\":"<<seconds<<",\"sieve_layout\":\""<<(tiled?"canonical_tiled":"whole_bitmap")<<"\",\"sieve_seconds\":"<<result.sieve_seconds<<",\"walk_seconds\":"<<result.walk_seconds<<",\"verify_seconds\":"<<result.verify_seconds<<",\"boundary_seconds\":"<<result.boundary_seconds<<"}\n"<<std::flush;
 }
 if(!std::cin.eof()&&std::cin.fail())throw std::runtime_error("Auditpiece command exceeds1024bytes");std::cout<<"{\"type\":\"stopped\"}\n"<<std::flush;return 0;
 }catch(const std::exception&e){std::cerr<<"CPU_SHARED_PORT_ERROR: "<<e.what()<<"\n";return 1;}}

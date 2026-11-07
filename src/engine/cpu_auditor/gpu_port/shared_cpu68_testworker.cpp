// Bounded owned-piece executor of the canonical sharedGPU scientific pipeline.
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
 if(argc==2&&std::string(argv[1])=="--component-info"){std::cout<<"{\"schema\":\"PGN_CPU_SHARED_COMPONENT_V1\",\"bits\":[68],\"canonical_source_manifest_sha256\":\"96C600D3486DED57B400FCD4428E904F6A3743E49F587DC85E93847D9FFDDE13\",\"production_admitted\":false,\"audit_only\":false,\"generic_scientific_fallback\":false}\n";return 0;}
 // All declared optional modes together occupy fourteen argv entries. Keep
 // parsing exact options below; a fixed ceiling must not reject the valid
 // AVX2+tiled+large-pages+complete-composite combination.
 if((argc<4||argc>14)||std::string(argv[1])!="--owned-piece"||std::string(argv[2])!="--cpu")throw std::runtime_error("Explicit --owned-piece --cpu INDEX required");
 const uint64_t selected=decimal64(argv[3]);if(selected>1023)throw std::runtime_error("CPU index invalid");pin(int(selected));bool simd4=false,tiled=false,smallFactors=false;std::string pagePath;std::unique_ptr<pgn_psp_native::Cohort> cohort;
 for(int i=4;i<argc;i++){
  const std::string option=argv[i];
  if(option=="--simd4")simd4=true;
  else if(option=="--tiled")tiled=true;
  else if(option.rfind("--large-pages=",0)==0)setBitmapPageMode(option.substr(14));
  else if(option=="--small-factor-complement"){if(smallFactors)throw std::runtime_error("Duplicate complement flag");smallFactors=true;}
  else if(option=="--composite-page"){if(!pagePath.empty()||i+1>=argc)throw std::runtime_error("Explicit unique composite page required");pagePath=argv[++i];}
  else if(option=="--composite-cohort"){
   if(cohort||i+3>=argc)throw std::runtime_error("Explicit unique cohort HIGH LOW LENGTH required");
   const uint64_t high=decimal64(argv[++i]),low=decimal64(argv[++i]),length=decimal64(argv[++i]);
   cohort=std::make_unique<pgn_psp_native::Cohort>(pgn_psp_native::Cohort::from({low,high},length));
  }else throw std::runtime_error("Unknown execution mode");
 }
 if(bool(cohort)!=(smallFactors||!pagePath.empty()))throw std::runtime_error("Complement requires both a cohort and a construction");
 if(simd4&&!pgn_promoted_avx2::avx2_on_current_cpu())throw std::runtime_error("AVX2 not admitted on pinnedCPU/OS");
 const auto prepareBegan=std::chrono::steady_clock::now();std::unique_ptr<CpuCompositeOverlay> complement;
 if(!pagePath.empty())complement=std::make_unique<CpuCompositeOverlay>(CpuCompositeOverlay::factored(pagePath,*cohort));
 if(smallFactors){
  const auto table=pgn_psp_native::SmallFactorTable::build();auto dense=CpuCompositeOverlay::dense(table,*cohort);
  complement=std::make_unique<CpuCompositeOverlay>(complement?CpuCompositeOverlay::merge(*complement,dense):std::move(dense));
 }
 const double complementPrepareSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-prepareBegan).count();
 const auto bank=canonicalPrimeBank();
 std::cout<<"{\"type\":\"ready\",\"backend\":\"cpu_shared_gpu_lineage68\",\"pinned_cpu\":"<<selected<<",\"affinity_verified\":true,\"audit_only\":false,\"production_admitted\":false,\"memory_pages\":"<<bitmapPageJSON()<<"}\n"<<std::flush;
 char buffer[1025];while(std::cin.getline(buffer,sizeof(buffer))){std::string line(buffer);if(line=="STOP")break;if(line=="MEMORY"){auto &arena=primaryBitmapBuffer();if(arena&&arena->info.compute_used)arena->verifyPages();std::cout<<"{\"type\":\"memory_pages\",\"memory_pages\":"<<bitmapPageJSON()<<"}\n"<<std::flush;continue;}std::istringstream input(line);std::string action,idText,highText,lowText,lengthText,gapText,extra;
  if(!(input>>action>>idText>>highText>>lowText>>lengthText>>gapText)||input>>extra||action!="TASK")throw std::runtime_error("Invalid bounded owned-piece command");
  const uint64_t id=decimal64(idText),high=decimal64(highText),low=decimal64(lowText),length=decimal64(lengthText),gap=decimal64(gapText);
  if(high>UINT32_MAX||gap>2147483647)throw std::runtime_error("Invalid numeric domain");const Number96 start{low,uint32_t(high)};const auto began=std::chrono::steady_clock::now();
  const auto result=countOwned68(start,length,uint32_t(gap),bank,simd4,tiled,complement.get());const auto end=addSmall(start,length);const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
  if(!result.complete||result.positions!=length)throw std::runtime_error("Unclosed sharedpipeline piece");
  std::cout.precision(12);std::cout<<"{\"schema\":\"PGN_CPU_SHARED_OWNED_PIECE_V2\",\"type\":\"result\",\"state\":\"COMPLETE\",\"backend\":\"cpu_shared_gpu_lineage68\",\"audit_only\":false,\"production_admitted\":false,\"job_id\":"<<id<<",\"start\":\""<<decimal96(start)<<"\",\"end\":\""<<decimal96(end)<<"\",\"positions\":\""<<result.positions<<"\",\"minimum_gap\":"<<gap<<",\"count\":\""<<result.retained<<"\",\"implementation\":\""<<(simd4?"shared_avx2_8owner":"shared_scalar64")<<"\",\"candidate_tests\":"<<result.tests<<",\"jumps\":"<<result.jumps<<",\"exact_prefix_positions\":"<<result.exact_prefix_positions<<",\"pinned_cpu\":"<<selected<<",\"affinity_verified\":true,\"coverage_contract\":\"SOURCE_PRP_INDEPENDENT_PROOF_PENDING\",\"canonical_source_manifest_sha256\":\"96C600D3486DED57B400FCD4428E904F6A3743E49F587DC85E93847D9FFDDE13\",\"seconds\":"<<seconds<<",\"sieve_layout\":\""<<(tiled?"canonical_tiled":"whole_bitmap")<<"\",\"sieve_seconds\":"<<result.sieve_seconds<<",\"walk_seconds\":"<<result.walk_seconds<<",\"verify_seconds\":"<<result.verify_seconds<<",\"boundary_seconds\":"<<result.boundary_seconds<<",\"memory_pages\":"<<bitmapPageJSON()<<",\"best\":[";
   for(size_t i=0;i<result.best.size();++i){if(i)std::cout<<",";const auto &g=result.best[i];std::cout<<"{\"lower_prime\":\""<<decimal96(g.lower)<<"\",\"upper_prime\":\""<<decimal96(g.upper)<<"\",\"gap\":"<<(g.upper.low-g.lower.low)<<"}";}
   std::cout<<"],\"complement_enabled\":"<<(complement?"true":"false")<<",\"complement_complete_psp_coverage\":false,\"complement_events\":"<<(complement?complement->events():0)<<",\"composite_new_marks\":"<<result.composite_new_marks<<",\"complement_prepare_seconds\":"<<complementPrepareSeconds<<",\"complement_seconds\":"<<result.complement_seconds<<",\"owned_frontier\":{\"start\":\""<<decimal96(start)<<"\",\"end\":\""<<decimal96(end)<<"\",\"gap_owner\":\"lower_prime\",\"right_crossing_verified\":true},\"records_verification\":\"canonical_reported_gap_and_exact_seams\"}\n"<<std::flush;
 }
 if(!std::cin.eof()&&std::cin.fail())throw std::runtime_error("Owned-piece command exceeds1024bytes");std::cout<<"{\"type\":\"stopped\"}\n"<<std::flush;return 0;
 }catch(const std::exception&e){std::cerr<<"CPU_SHARED_PORT_ERROR: "<<e.what()<<"\n";return 1;}}

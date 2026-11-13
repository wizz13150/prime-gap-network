// Policy-equivalence oracle: exact published Core15 selector, source 1bd2eae7853660c66ee65f771cc2e9f86cceecee.

#include <pgn/native_report_audit.h>
#include <util/translation.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
extern const TranslateFn G_TRANSLATION_FUN=[](const char* text){return std::string(text);};
using namespace pgn::core;
inline std::optional<std::pair<uint256,uint8_t>> SelectNativeAuditBaseline15(const NativeAuditLedger& ledger,const CScript& actor,int32_t parent_height,int64_t parent_median_time){
 if(!NativeAuditEligible(ledger,actor))return {};
 const bool recheck=NativeAuditRecheckAt(ledger,parent_height);
 std::optional<std::pair<uint256,uint8_t>> chosen;std::optional<std::tuple<int,int,int32_t,uint256,uint8_t>> rank;
 for(const auto& [id,report]:ledger){
  const bool own=NativeAuditOperator(ledger,report.owner)==NativeAuditOperator(ledger,actor);
  if((own&&!recheck)||report.report.height>=parent_height||report.Audited())continue;
  for(uint8_t slot=0;slot<2;++slot){const auto& job=report.jobs[slot];
   if(job&&(job->stage!=NativeAuditJob::Assigned||!NativeAuditAssignmentExpired(*job,parent_height,parent_median_time)))continue;
   const auto& other=report.jobs[1-slot];const bool repeated=other&&NativeAuditOperator(ledger,other->actor)==NativeAuditOperator(ledger,actor);
   if(repeated&&!recheck)continue;
   // Close a partially committed report before opening more work, otherwise
   // other-owner preference can strand all credits behind fresh first slots.
   // Preserve the exact historic order before this network rule activates.
   const bool closing=other&&(other->stage==NativeAuditJob::Committed||other->stage==NativeAuditJob::Passed);
   const auto candidate=std::tuple{recheck?int(!closing):0,2*int(repeated)+int(own),report.report.height,id,slot};
   if(!rank||candidate<*rank){rank=candidate;chosen=std::pair{id,slot};}
  }
 }
 return chosen;
}
static void need(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static uint256 id(unsigned n){uint256 v;for(unsigned i=0;i<4;++i)v.begin()[i]=(n>>(8*i))&255;return v;}
static CScript actor(unsigned n){std::vector<unsigned char> key(33);key[0]=2;for(unsigned i=0;i<4;++i)key[1+i]=(n>>(8*i))&255;return CScript{}<<key<<OP_CHECKSIG;}
int main(){try{
 std::cout<<"{\"PASS\":true,\"cases\":[";bool comma=false;size_t checks=0;
 for(unsigned count:{100U,1000U,10000U})for(unsigned population:{2U,10000U})for(bool enrolled:{false,true})for(bool mostly_closed:{false,true}){
  NativeAuditLedger ledger;ledger.public_admission=true;ledger.audit_chain=id(333);ledger.audit_operator_rules.emplace();ledger.audit_operator_rules->chain=ledger.audit_chain;ledger.audit_operator_rules->activation_height=50;
  const unsigned used=std::min(count,population);std::vector<CScript> actors;for(unsigned i=0;i<used;++i){actors.push_back(actor(i+1));if(enrolled)ledger.participants.emplace(Bytes{actors.back().begin(),actors.back().end()},NativeAuditMembership{id(i+1),true});}
  for(unsigned i=0;i<count;++i){NativeReportAudit r;r.owner=actors[i%used];r.report.height=int32_t(1+i%40);
   const unsigned mode=i%7;if(mode>=2){r.jobs[0]=NativeAuditJob{actors[(i+1)%used],0,45,mode==2?48:120};if(mode>=3)r.jobs[0]->stage=NativeAuditJob::Committed;}
   if(mode>=4){r.jobs[1]=NativeAuditJob{actors[(i+2)%used],0,45,mode==4?48:120};if(mode>=5){r.jobs[1]->stage=NativeAuditJob::Passed;r.jobs[0]->stage=NativeAuditJob::Passed;r.details_root=Digest{};}}
   if(mostly_closed&&i%100!=0){for(auto& job:r.jobs){job=NativeAuditJob{actors[(i+1)%used],0,45,120};job->stage=NativeAuditJob::Passed;}r.details_root=Digest{};}
   ledger.emplace(id(i+1),std::move(r));}
  std::vector<CScript> query;for(unsigned n=0;n<16;++n)query.push_back(n%2?actors[n%used]:actor(20000+n));
  for(int32_t height:{0,49,50,80,130})for(const auto& q:query){need(SelectNativeAuditBaseline15(ledger,q,height,500)==SelectNativeAudit(ledger,q,height,500),"Hoisted selector differs from exact current policy");++checks;}
  if(comma)std::cout<<',';comma=true;std::cout<<"{\"reports\":"<<count<<",\"operators\":"<<population<<",\"enrolled\":"<<(enrolled?"true":"false")<<",\"mostly_closed\":"<<(mostly_closed?"true":"false")<<'}';
 }
 std::cout<<"],\"exact_comparisons\":"<<checks<<",\"network_or_runtime_changed\":false}"<<std::endl;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}

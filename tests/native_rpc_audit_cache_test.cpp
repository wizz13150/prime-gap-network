#include <pgn/native_report_audit.h>
#include <pgn/rpc_audit_cache.h>
#include <pgn/rpc_audit_incremental.h>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace pgn::core;
int main(){
 unsigned checks=0;const auto check=[&](bool okay){++checks;if(!okay)throw std::runtime_error("RPC cache check "+std::to_string(checks));};
 auto make=[](size_t count){
  auto ledger=std::make_shared<NativeAuditLedger>();
  for(size_t i=0;i<count;++i){uint256 id;for(unsigned b=0;b<sizeof(i);++b)id.begin()[b]=(i>>(8*b))&255;auto& report=(*ledger)[id];report.owner.resize(22);report.owner_reveal="fixture";report.report.commitment.signature.resize(65);}
  return ledger;
 };
 // Native ledger objects, not JSON mocks; these are cache fixtures, never
 // admitted native history or independently verified audit evidence.
 auto mutableLedger=make(5000);
 std::shared_ptr<const NativeAuditLedger> ledger=mutableLedger;
 const auto charge=RpcAuditCacheCharge(*ledger);check(charge.has_value());check(*charge>4*1024*1024);check(*charge<RpcAuditCacheBudget);
 const auto key=[](unsigned char n){uint256 value;value.begin()[0]=n;return value;};
 RpcAuditSnapshotCache<NativeAuditLedger,uint256> cache;int firstState=0,secondState=0;
 check(!cache.get(&firstState,key(1),key(10)));check(cache.retain(&firstState,key(1),key(10),ledger));
 check(cache.get(&firstState,key(1),key(10))==ledger);
 check(!cache.get(&firstState,key(2),key(10)));check(!cache.get(&firstState,key(1),key(11)));check(!cache.get(&secondState,key(1),key(10)));
 check(cache.retain(&firstState,key(2),key(10),ledger));check(!cache.get(&firstState,key(1),key(10)));check(cache.get(&firstState,key(2),key(10))==ledger);
 check(!cache.retain(&firstState,key(3),key(10),ledger,*charge-1));check(!cache.get(&firstState,key(2),key(10)));
 check(cache.retain(&firstState,key(3),key(10),ledger,*charge));check(cache.get(&firstState,key(3),key(10))==ledger);
 cache.reset();check(!cache.get(&firstState,key(3),key(10)));
 check(!cache.retain(&firstState,key(3),key(10),{}));
 check(!RpcAuditCacheCharge(*ledger,0));
 check(RpcAuditCacheCharge(*ledger,std::numeric_limits<size_t>::max()).has_value());
 auto oversized=make(17000);check(!RpcAuditCacheCharge(*oversized));
 auto reserved=make(1);reserved->begin()->second.owner_reveal.reserve(8*1024*1024);
 check(!RpcAuditCacheCharge(*reserved,4*1024*1024));check(RpcAuditCacheCharge(*reserved).has_value());
 reserved->begin()->second.report.commitment.signature.reserve(8*1024*1024);
 check(!RpcAuditCacheCharge(*reserved,12*1024*1024));
 std::weak_ptr<const NativeAuditLedger> weak;
 {auto temporary=make(1);weak=temporary;check(cache.retain(&firstState,key(4),key(10),temporary));}
 check(!weak.expired());cache.reset();check(weak.expired());
 check(cache.retain(&firstState,key(1),key(10),ledger));
 auto old=cache.get(&firstState,key(1),key(10));check(cache.retain(&firstState,key(2),key(10),make(2)));
 check(old->size()==5000);check(cache.get(&firstState,key(2),key(10))->size()==2);
 // Direct-child successors are transactional. Real native-ledger objects
 // exercise copy isolation and failure; these fixtures are not chain proofs.
 unsigned builds=0;
 const auto append=[&](const NativeAuditLedger& before)->std::optional<NativeAuditLedger>{
  ++builds;auto next=before;next[key(77)].owner_reveal="new disclosure";return next;
 };
 check(!cache.advance(&secondState,key(2),key(3),key(10),append));check(builds==0);
 check(!cache.get(&firstState,key(2),key(10)));
 check(cache.retain(&firstState,key(2),key(10),make(2)));
 auto immutable=cache.get(&firstState,key(2),key(10));
 auto successor=cache.advance(&firstState,key(2),key(3),key(10),append);
 check(builds==1&&successor&&successor->size()==3);check(immutable->size()==2&&!immutable->contains(key(77)));
 check(!cache.get(&firstState,key(2),key(10)));check(cache.get(&firstState,key(3),key(10))==successor);
 const auto refused=[&](const NativeAuditLedger& before)->std::optional<NativeAuditLedger>{++builds;auto partial=before;partial.clear();return {};};
 check(!cache.advance(&firstState,key(3),key(4),key(10),refused));check(builds==2&&successor->size()==3);
 check(!cache.get(&firstState,key(4),key(10))&&!cache.get(&firstState,key(3),key(10)));
 check(cache.retain(&firstState,key(3),key(10),successor));
 bool threw=false;try{cache.advance(&firstState,key(3),key(4),key(10),[](const NativeAuditLedger&)->std::optional<NativeAuditLedger>{throw std::runtime_error("invalid event");});}catch(const std::runtime_error&){threw=true;}
 check(threw&&!cache.get(&firstState,key(4),key(10))&&successor->size()==3);
 check(cache.retain(&firstState,key(3),key(10),successor));
 check(!cache.advance(&firstState,key(3),key(3),key(10),append));check(builds==2);
 check(cache.retain(&firstState,key(3),key(10),successor));
 check(!cache.advance(&firstState,key(3),key(4),key(11),append));check(builds==2);
 check(cache.retain(&firstState,key(3),key(10),successor));
 auto uncached=cache.advance(&firstState,key(3),key(4),key(10),append,1);
 check(uncached&&uncached->size()==3&&!cache.get(&firstState,key(4),key(10)));
 std::cout<<"PASS "<<checks<<" native-ledger cache controls; 5000 reports charge "<<*charge<<" bytes; no live Core or throughput claim\n";
}

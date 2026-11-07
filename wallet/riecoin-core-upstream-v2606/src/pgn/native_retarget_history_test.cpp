#undef NDEBUG
#include <pgn/native_retarget_policy.h>
#include <cassert>
#include <cstdio>
#include <deque>
#include <memory>
#include <vector>
using namespace pgn::core;
namespace {
struct Branch {
    std::deque<uint256> hashes;
    std::vector<std::unique_ptr<CBlockIndex>> nodes;
    unsigned next{1};
    CBlockIndex& append(CBlockIndex* previous,bool science,int64_t time,uint32_t merit){
        char value[65];std::snprintf(value,sizeof(value),"%064x",next++);
        hashes.push_back(*uint256::FromHex(value));auto node=std::make_unique<CBlockIndex>();
        node->phashBlock=&hashes.back();node->pprev=previous;node->nHeight=previous?previous->nHeight+1:0;
        node->nTime=time;node->nNonce=science?0:2;node->nBits=science?merit:0x207fffff;
        node->nStatus=BLOCK_VALID_SCRIPTS;node->BuildSkip();nodes.push_back(std::move(node));return *nodes.back();
    }
};
void check(const CBlockIndex& parent,const MeritControllerDefinition& policy,const std::vector<int64_t>& goldTimes){
    const auto quote=DeriveHistoricalMeritTarget(parent,policy);assert(quote);
    assert(quote->scientific_count==goldTimes.size());
    const auto* genesis=parent.GetAncestor(0);
    if(goldTimes.empty()){
        assert(quote->samples==0&&quote->adjusted_count==0);
        assert(quote->target_q16==*QuoteAnchoredMeritTarget(policy,0,parent.GetMedianTimePast()-genesis->GetBlockTime()));
        return;
    }
    const auto samples=std::min(size_t(11),goldTimes.size());
    std::vector<int64_t> recent(goldTimes.end()-samples,goldTimes.end());std::sort(recent.begin(),recent.end());
    const auto adjusted=goldTimes.size()-(samples-1)/2;
    assert(quote->samples==samples&&quote->adjusted_count==adjusted);
    assert(quote->target_q16==*QuoteAnchoredMeritTarget(policy,adjusted,recent[samples/2]-genesis->GetBlockTime()));
}
}
int main(){
    const MeritControllerDefinition policy{32*65536,20*65536,40*65536,600,86400};
    Branch branch;auto* genesis=&branch.append(nullptr,false,100000,policy.initial_q16);auto* head=genesis;
    std::vector<int64_t> golds;
    for(unsigned i=0;i<30;++i){head=&branch.append(head,false,100000+i*10,policy.initial_q16);check(*head,policy,golds);}
    auto* beforeGold=head;
    for(unsigned gold=0;gold<40;++gold){
        golds.push_back(101000+gold*601);head=&branch.append(head,true,golds.back(),policy.initial_q16);check(*head,policy,golds);
        const auto target=DeriveHistoricalMeritTarget(*head,policy)->target_q16;
        for(unsigned ledger=0;ledger<gold%7+1;++ledger){
            head=&branch.append(head,false,golds.back()+ledger+1,policy.initial_q16);check(*head,policy,golds);
            assert(DeriveHistoricalMeritTarget(*head,policy)->target_q16==target);
        }
    }
    check(*beforeGold,policy,{}); // A later branch/cache entry cannot rewrite old work counts.
    auto* fork=&branch.append(beforeGold,true,102345,policy.initial_q16);check(*fork,policy,{102345});
    for(unsigned i=0;i<4200;++i){fork=&branch.append(fork,false,102346+i,policy.initial_q16);check(*fork,policy,{102345});}
    check(*head,policy,golds); // Exact reconstruction after bounded cache eviction.
    auto changed=policy;changed.spacing_seconds=1200;check(*head,changed,golds);
    auto& invalid=branch.append(head,false,200000,policy.initial_q16);invalid.nNonce=3;
    assert(!DeriveHistoricalMeritTarget(invalid,policy));
    std::puts("PASS retarget history: scientific-only count, interleaved coordination, exact median, fork, old ancestry, cache eviction, policy change, invalid header");
}

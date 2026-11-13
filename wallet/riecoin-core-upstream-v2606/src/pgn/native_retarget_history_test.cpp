#undef NDEBUG
#include <pgn/native_retarget_policy.h>
#include <pgn/native_elapsed_retarget.h>
#include <pow.h>
#include <consensus/params.h>
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
    // Elapsed-time candidate: the legacy quote freezes during a Gold drought.
    // The replacement retains the anchor exactly and follows verified MTP.
    const auto anchor_target=DeriveHistoricalMeritTarget(*head,policy)->target_q16;
    const auto initial=DeriveElapsedMeritTarget(*head,*head,policy,1800);assert(initial&&initial->target_q16==anchor_target);
    const auto anchor_time=head->GetMedianTimePast();
    for(const int64_t elapsed:std::array<int64_t,3>{600,1800,3600}){
        auto* drought=head;
        for(int offset=-5;offset<=5;++offset)drought=&branch.append(drought,false,anchor_time+elapsed+offset,policy.initial_q16);
        assert(drought->GetMedianTimePast()==anchor_time+elapsed);
        const auto old=DeriveHistoricalMeritTarget(*drought,policy);assert(old&&old->target_q16==anchor_target);
        const auto changed=DeriveElapsedMeritTarget(*drought,*head,policy,1800);assert(changed&&changed->target_q16<anchor_target&&changed->blocks_since_anchor==0&&changed->elapsed_seconds==elapsed);
        const auto exact_drop=(uint64_t(elapsed)*45426+1799)/1800;
        assert(anchor_target-changed->target_q16==exact_drop);
        std::printf("elapsed_seconds=%lld legacy_delta_q16=0 candidate_delta_q16=-%u\n",static_cast<long long>(elapsed),anchor_target-changed->target_q16);
    }
    assert(!DeriveElapsedMeritTarget(*beforeGold,*head,policy,1800)); // Future anchor.
    assert(!DeriveElapsedMeritTarget(*fork,*head,policy,1800)); // Another branch.
    assert(!DeriveElapsedMeritTarget(*head,*head,policy,599)); // Below target spacing.
    // Equal accepted-work cadence cancels elapsed time exactly; bursts add
    // bounded work credit rather than being mistaken for coordination records.
    for(const uint32_t half_life:std::array<uint32_t,2>{1800,3600}){
        auto* steady=head;
        for(unsigned tick=1;tick<=12;++tick){
            const auto time=anchor_time+tick*600;
            steady=&branch.append(steady,true,time,policy.initial_q16);
            for(int offset=-5;offset<=5;++offset)steady=&branch.append(steady,false,time+offset,policy.initial_q16);
            const auto quote=DeriveElapsedMeritTarget(*steady,*head,policy,half_life);
            assert(quote&&quote->target_q16==anchor_target&&quote->blocks_since_anchor==tick);
        }
        auto* burst=head;
        for(unsigned i=0;i<3;++i)burst=&branch.append(burst,true,anchor_time+600+i,policy.initial_q16);
        for(int offset=-5;offset<=5;++offset)burst=&branch.append(burst,false,anchor_time+600+offset,policy.initial_q16);
        const auto quote=DeriveElapsedMeritTarget(*burst,*head,policy,half_life);
        auto rebased=policy;rebased.initial_q16=anchor_target;rebased.half_life_seconds=half_life;
        assert(quote&&quote->target_q16==*QuoteAnchoredMeritTarget(rebased,3,600));
        std::printf("half_life_seconds=%u steady_12_gold_delta_q16=0 burst_3_gold_delta_q16=%u\n",half_life,quote->target_q16-anchor_target);
    }
    auto* outage=head;
    for(int offset=-5;offset<=5;++offset)outage=&branch.append(outage,false,anchor_time+86400+offset,policy.initial_q16);
    const auto floor=DeriveElapsedMeritTarget(*outage,*head,policy,1800);assert(floor&&floor->target_q16==policy.minimum_q16);
    for(unsigned i=0;i<3;++i)outage=&branch.append(outage,true,anchor_time+86406+i,policy.minimum_q16);
    const auto recovery=DeriveElapsedMeritTarget(*outage,*head,policy,1800);assert(recovery);
    auto fixed=policy;fixed.initial_q16=anchor_target;fixed.half_life_seconds=1800;
    const auto fixed_recovery=QuoteAnchoredMeritTarget(fixed,3,recovery->elapsed_seconds);
    assert(fixed_recovery&&*fixed_recovery==policy.minimum_q16);
    assert(recovery->target_q16>policy.minimum_q16&&recovery->target_q16<policy.minimum_q16+65536);
    std::printf("24h_outage_floor_q16=%u recovery_after_3_gold_q16=%u rejected_fixed_anchor_q16=%u\n",floor->target_q16,recovery->target_q16,*fixed_recovery);
    auto* evicted=outage;
    for(unsigned i=0;i<4200;++i)evicted=&branch.append(evicted,false,anchor_time+86410+i,policy.initial_q16);
    assert(DeriveElapsedMeritTarget(*evicted,*head,policy,1800));
    assert(DeriveElapsedMeritTarget(*outage,*head,policy,1800)->target_q16==recovery->target_q16);
    assert(DeriveElapsedMeritTarget(*head,*head,policy,3600)->target_q16==anchor_target);
    const auto forked=DeriveElapsedMeritTarget(*fork,*beforeGold,policy,1800);assert(forked&&forked->blocks_since_anchor==1);
    const ElapsedRetargetSchedule schedule{genesis->GetBlockHash(),head->nHeight+1,1800,21U*65536U};
    assert(DeriveScheduledMeritTarget(*head,policy,schedule)->target_q16==anchor_target);
    assert(DeriveScheduledMeritTarget(*outage,policy,schedule)->target_q16>21U*65536U);
    const auto floor21=DeriveScheduledMeritTarget(*evicted,policy,schedule);assert(floor21&&floor21->target_q16==21U*65536U);
    assert(DeriveScheduledMeritTarget(*beforeGold,policy,schedule)->target_q16==DeriveHistoricalMeritTarget(*beforeGold,policy)->target_q16);
    auto foreign=schedule;foreign.chain=branch.hashes.back();
    assert(DeriveScheduledMeritTarget(*evicted,policy,foreign)->target_q16==DeriveHistoricalMeritTarget(*evicted,policy)->target_q16);
    assert(DeriveNextMeritTarget(*evicted,policy)->target_q16==DeriveHistoricalMeritTarget(*evicted,policy)->target_q16);
    auto malformed=schedule;malformed.height=0;assert(!DeriveScheduledMeritTarget(*head,policy,malformed));
    malformed=schedule;malformed.retained_floor_q16=64U*65536U;assert(!DeriveScheduledMeritTarget(*head,policy,malformed));
    // Many cheap records at the same median time do not manufacture work.
    auto* still=head;
    for(int offset=-5;offset<=5;++offset)still=&branch.append(still,false,anchor_time+600+offset,policy.initial_q16);
    const auto unchanged=DeriveElapsedMeritTarget(*still,*head,policy,1800)->target_q16;
    for(unsigned i=0;i<100;++i)still=&branch.append(still,false,anchor_time+600,policy.initial_q16);
    assert(DeriveElapsedMeritTarget(*still,*head,policy,1800)->target_q16==unchanged);
    auto* ceiling=head;
    for(unsigned i=0;i<100;++i)ceiling=&branch.append(ceiling,true,anchor_time+601,policy.maximum_q16);
    assert(DeriveElapsedMeritTarget(*ceiling,*head,policy,1800)->target_q16==policy.maximum_q16);
    for(int offset=-5;offset<=5;++offset)ceiling=&branch.append(ceiling,false,anchor_time+1200+offset,policy.initial_q16);
    assert(DeriveElapsedMeritTarget(*ceiling,*head,policy,1800)->target_q16<policy.maximum_q16);
    std::puts("PASS elapsed transition: old history unchanged, wrong chain excluded, exact transition, retained floor21, no coordination count credit, upper/lower recovery");
    // Link the real production GetNextWorkRequired entry, with the configured
    // testnet genesis and actual activation height. The old pow.o fails this.
    const MeritControllerDefinition deployed{29U*65536U,16U*65536U,64U*65536U,600,172800};
    Branch pinned;pinned.next=100000;auto* live=&pinned.append(nullptr,false,100000,deployed.initial_q16);
    pinned.hashes.front()=*uint256::FromHex("0ccd1f2349ed75f9cf4d0279871a204e107683e5baa1dd6641c4e6f08a25a17d");
    Consensus::Params params{};params.pgn_native_proof=Consensus::NativeProofParameters{deployed.initial_q16,deployed.minimum_q16,deployed.maximum_q16,600,172800,0x207fffff,{}};
    for(int32_t height=1;height<TestnetElapsedRetargetHeight;++height){
        const auto expected=GetNextWorkRequired(live,params);
        live=&pinned.append(live,height==100,100000+height,expected);
    }
    const auto legacy_at_activation=DeriveHistoricalMeritTarget(*live,deployed)->target_q16;
    assert(GetNextWorkRequired(live,params)==legacy_at_activation);
    const auto* transition=live;const auto start_time=live->GetMedianTimePast();
    for(int offset=-5;offset<=5;++offset)live=&pinned.append(live,false,start_time+600+offset,deployed.initial_q16);
    const auto next=GetNextWorkRequired(live,params);
    assert(next==DeriveNextMeritTarget(*live,deployed)->target_q16&&next<legacy_at_activation);
    assert(DeriveHistoricalMeritTarget(*live,deployed)->target_q16==legacy_at_activation);
    live=&pinned.append(live,true,start_time+606,next);
    assert(live->nBits==GetNextWorkRequired(live->pprev,params));
    assert(GetNextWorkRequired(transition,params)==legacy_at_activation);
    std::printf("PASS production pow entry: height%d preserves %u; post-transition drought target %u; new Gold header matches; old ancestry unchanged\n",TestnetElapsedRetargetHeight,legacy_at_activation,next);
    std::puts("PASS retarget history: scientific-only count, interleaved coordination, exact median, fork, old ancestry, cache eviction, policy change, invalid header");
    {
    Branch upgraded;upgraded.next=50000;
    auto* origin=&upgraded.append(nullptr,false,1000,policy.initial_q16);auto* tip=origin;
    const uint32_t floor=55U*32768U;const int64_t activation=20000;
    unsigned pre{},post{};
    for(int64_t stamp=1600;stamp<=40000;stamp+=600){
        tip=&upgraded.append(tip,false,stamp,policy.initial_q16);
        const auto old=DeriveElapsedMeritTarget(*tip,*origin,policy,1800);
        const auto changed=DeriveElapsedMeritTarget(*tip,*origin,policy,1800,0,activation,floor);
        assert(old&&changed);
        if(tip->GetMedianTimePast()<activation){assert(old->target_q16==changed->target_q16);++pre;}
        else{assert(changed->target_q16>=floor);++post;}
    }
    assert(pre&&post);
    assert(DeriveElapsedMeritTarget(*tip,*origin,policy,1800)->target_q16<floor);
    const auto before=DeriveElapsedMeritTarget(*tip,*origin,policy,1800,0,activation,floor)->target_q16;
    for(unsigned i=0;i<12;++i)tip=&upgraded.append(tip,true,tip->GetBlockTime()+1,before);
    const auto recovery=DeriveElapsedMeritTarget(*tip,*origin,policy,1800,0,activation,floor);
    assert(recovery&&recovery->target_q16>floor);
    assert(!DeriveElapsedMeritTarget(*tip,*origin,policy,1800,0,activation,policy.maximum_q16+1));
    assert(!DeriveElapsedMeritTarget(*tip,*origin,policy,1800,0,0,floor));
    const auto reopened=DeriveElapsedMeritTarget(*tip,*origin,policy,1800,0,activation,floor);
    assert(reopened&&reopened->target_q16==recovery->target_q16);
    std::printf("PASS exact27.5 scheduled floor: %u old-history points unchanged, %u post-cutover points bounded, immediate Gold recovery %u Q16\n",pre,post,recovery->target_q16);
    }

}

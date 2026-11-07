#undef NDEBUG
#include <pgn/native_bootstrap_phase.h>
#include <cassert>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <vector>
using namespace pgn::core;
namespace {
uint256 FixtureId(unsigned value){char text[65];std::snprintf(text,sizeof(text),"%064x",value);return *uint256::FromHex(text);}
struct View:CCoinsView {
    FactHead head;
    std::map<uint256,FactCommit> steps;
    mutable unsigned reads{};
    std::optional<Coin> GetCoin(const COutPoint&)const override{return {};}
    std::optional<Coin> PeekCoin(const COutPoint&)const override{return {};}
    bool HaveCoin(const COutPoint&)const override{return false;}
    std::vector<uint256> GetHeadBlocks()const override{return {};}
    void BatchWrite(CoinsViewCacheCursor&,const uint256&)override{assert(false);}
    std::unique_ptr<CCoinsViewCursor> Cursor()const override{return {};}
    size_t EstimateSize()const override{return 0;}
    uint256 GetBestBlock()const override{return head.tip;}
    std::optional<FactHead> GetPgnFactHead()const override{return head;}
    std::optional<FactCommit> GetPgnFactForward(const uint256& id)const override{
        ++reads;const auto found=steps.find(id);return found==steps.end()?std::nullopt:std::optional{found->second};
    }
};
struct Fixture {
    std::deque<uint256> hashes;
    std::vector<std::unique_ptr<CBlockIndex>> blocks;
    View view;
    CBlockIndex& Append(CBlockIndex* previous,unsigned id,bool science=false){
        hashes.push_back(FixtureId(id));auto next=std::make_unique<CBlockIndex>();
        next->phashBlock=&hashes.back();next->pprev=previous;
        next->nHeight=previous?previous->nHeight+1:0;next->nStatus=BLOCK_VALID_SCRIPTS;next->BuildSkip();
        auto& block=*next;blocks.push_back(std::move(next));
        if(!previous)view.head={block.GetBlockHash(),block.GetBlockHash()};
        else{
            FactCommit step{FactHead{view.head.chain,previous->GetBlockHash()},FactHead{view.head.chain,block.GetBlockHash()},{}};
            if(science)step.changes.push_back({FixtureId(90000),{},FactClaim{FixtureId(90001),block.GetBlockHash()}});
            assert(step.Valid());view.steps.emplace(block.GetBlockHash(),std::move(step));view.head.tip=block.GetBlockHash();
        }
        return block;
    }
};
NativeBootstrapPhaseVerdict Finish(View& view,const CBlockIndex& tip,NativeBootstrapPhaseCursor& cursor,unsigned slice){
    for(unsigned attempt=0;attempt<100;++attempt){
        view.reads=0;const auto result=InspectNativeBootstrapPhaseSlice(view,tip,slice,cursor);
        assert(view.reads<=slice);
        if(result!=NativeBootstrapPhaseVerdict::BudgetExceeded)return result;
    }
    assert(false&&"Bounded replay made no progress");return NativeBootstrapPhaseVerdict::HistoryHeld;
}
}
int main(){
    for(const auto phase:{NativeBootstrapPhaseVerdict::Bootstrap,NativeBootstrapPhaseVerdict::Scientific,
                         NativeBootstrapPhaseVerdict::InvalidParent,NativeBootstrapPhaseVerdict::HistoryHeld,
                         NativeBootstrapPhaseVerdict::BudgetExceeded}){
        assert(NativeLedgerAllowsCoordination(phase,false)==(phase==NativeBootstrapPhaseVerdict::Bootstrap));
        assert(NativeLedgerAllowsCoordination(phase,true)==(phase==NativeBootstrapPhaseVerdict::Bootstrap||phase==NativeBootstrapPhaseVerdict::Scientific));
    }
    LOCK(cs_main);Fixture fixture;auto* genesis=&fixture.Append(nullptr,1);auto* tip=genesis;
    for(unsigned id=2;id<=5001;++id)tip=&fixture.Append(tip,id);
    NativeBootstrapPhaseCursor cursor;
    assert(InspectNativeBootstrapPhaseSlice(fixture.view,*tip,0,cursor)==NativeBootstrapPhaseVerdict::BudgetExceeded);
    assert(InspectNativeBootstrapPhaseSlice(fixture.view,*tip,4096,cursor)==NativeBootstrapPhaseVerdict::BudgetExceeded);
    assert(fixture.view.reads==4096);
    assert(Finish(fixture.view,*tip,cursor,4096)==NativeBootstrapPhaseVerdict::Bootstrap);
    assert(fixture.view.reads==904);
    fixture.view.reads=0;
    assert(InspectNativeBootstrapPhase(fixture.view,*tip,4096)==NativeBootstrapPhaseVerdict::Bootstrap);
    assert(fixture.view.reads==5000);
    fixture.view.reads=0;
    assert(InspectNativeBootstrapPhaseSlice(fixture.view,*tip,2,cursor)==NativeBootstrapPhaseVerdict::Bootstrap);
    assert(fixture.view.reads==0);
    tip=&fixture.Append(tip,5002);fixture.view.reads=0;
    assert(InspectNativeBootstrapPhaseSlice(fixture.view,*tip,2,cursor)==NativeBootstrapPhaseVerdict::Bootstrap);
    assert(fixture.view.reads==1);
    tip=&fixture.Append(tip,5003,true);
    assert(Finish(fixture.view,*tip,cursor,2)==NativeBootstrapPhaseVerdict::Scientific);
    auto* fork=genesis;
    for(unsigned id=20001;id<=20014;++id)fork=&fixture.Append(fork,id);
    assert(Finish(fixture.view,*fork,cursor,2)==NativeBootstrapPhaseVerdict::Bootstrap);
    NativeBootstrapPhaseCursor cold;
    const auto missing_id=fork->GetAncestor(10)->GetBlockHash();const auto saved=fixture.view.steps.at(missing_id);
    fixture.view.steps.erase(missing_id);
    assert(Finish(fixture.view,*fork,cold,2)==NativeBootstrapPhaseVerdict::HistoryHeld);
    fixture.view.steps.emplace(missing_id,saved);
    assert(Finish(fixture.view,*fork,cold,2)==NativeBootstrapPhaseVerdict::Bootstrap);
    cold.Reset();fixture.view.steps.at(fork->GetBlockHash()).before=FactHead{fixture.view.head.chain,FixtureId(777)};
    assert(Finish(fixture.view,*fork,cold,2)==NativeBootstrapPhaseVerdict::HistoryHeld);
    fixture.view.head.tip=FixtureId(999);
    assert(InspectNativeBootstrapPhaseSlice(fixture.view,*fork,2,cold)==NativeBootstrapPhaseVerdict::InvalidParent);
    uint64_t reverse_reads{};
    for(size_t height=4999;height>=4900;--height){
        const auto& previous=*fixture.blocks.at(height);fixture.view.head.tip=previous.GetBlockHash();fixture.view.reads=0;
        assert(InspectNativeBootstrapPhase(fixture.view,previous,4096)==NativeBootstrapPhaseVerdict::Bootstrap);
        reverse_reads+=fixture.view.reads;
    }
    std::printf("{\"reverse_queries\":100,\"fact_step_reads\":%llu}\n",static_cast<unsigned long long>(reverse_reads));
    assert(reverse_reads==0);
    Fixture large;auto* large_tip=&large.Append(nullptr,200000);
    for(unsigned id=200001;id<=270000;++id)large_tip=&large.Append(large_tip,id);
    NativeBootstrapPhaseCursor bounded;
    assert(Finish(large.view,*large_tip,bounded,4096)==NativeBootstrapPhaseVerdict::Bootstrap);
    auto& older=*large.blocks.at(1000);large.view.head.tip=older.GetBlockHash();large.view.reads=0;
    assert(InspectNativeBootstrapPhaseSlice(large.view,older,4096,bounded)==NativeBootstrapPhaseVerdict::Bootstrap);
    assert(large.view.reads==1000); // A bounded eviction needs revalidation, never invented state.
    large.view.reads=0;
    assert(InspectNativeBootstrapPhaseSlice(large.view,older,4096,bounded)==NativeBootstrapPhaseVerdict::Bootstrap);
    assert(large.view.reads==0);
    return 0;
}

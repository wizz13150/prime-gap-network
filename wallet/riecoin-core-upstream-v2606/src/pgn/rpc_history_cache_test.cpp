// Real native archive/registry objects; no process, wallet, peer or GPU.
#undef NDEBUG
#include <pgn/request_registry.h>
#include <pgn/rpc_history_cache.h>
#include <coins.h>
#include <chain.h>
#include <consensus/merkle.h>
#include <validation.h>
#include <cassert>
#include <deque>
#include <iostream>
#include <limits>
#include <type_traits>
using namespace pgn::core;
namespace {
struct View:CCoinsView {
    FactHead head;
    std::optional<Coin> GetCoin(const COutPoint&)const override{return {};}
    std::optional<Coin> PeekCoin(const COutPoint&)const override{return {};}
    bool HaveCoin(const COutPoint&)const override{return false;}
    uint256 GetBestBlock()const override{return head.tip;}
    std::optional<FactHead> GetPgnFactHead()const override{return head;}
    std::vector<uint256> GetHeadBlocks()const override{return {};}
    void BatchWrite(CoinsViewCacheCursor&,const uint256&)override{assert(false);}
    std::unique_ptr<CCoinsViewCursor> Cursor()const override{return {};}
    size_t EstimateSize()const override{return 0;}
};
}
int main(int argc,char** argv){
    assert(argc==2);LOCK(cs_main);
    static_assert(!std::is_copy_constructible_v<VerifiedRequestRegistry>);
    unsigned checks=0;const auto check=[&](bool okay){++checks;if(!okay)throw std::runtime_error("history cache check "+std::to_string(checks));};
    constexpr RequestHistoryArchiveBudget archive_budget{4000000,1U<<20};
    const ConfirmedRequestScanBudget scan{4000000,4000000,4000000,4000000,{10000,10000,1U<<20,true}};
    NativeRequestHistoryArchive archive{fs::path{argv[1]},1U<<20};View view;
    std::deque<uint256> hashes;std::vector<std::unique_ptr<CBlockIndex>> blocks;
    std::optional<VerifiedRequestRegistry> registry;size_t initial_charge{};
    for(unsigned height=0;height<40;++height){
        CMutableTransaction tx;tx.vin.resize(1);tx.vin[0].scriptSig=CScript{}<<int64_t(height+1);tx.vout.emplace_back(0,CScript{}<<OP_RETURN);
        CBlock block;block.nVersion=4;block.nTime=1000+height;if(height)block.hashPrevBlock=hashes.back();
        block.vtx.push_back(MakeTransactionRef(std::move(tx)));block.hashMerkleRoot=BlockMerkleRoot(block);hashes.push_back(block.GetHash());
        auto index=std::make_unique<CBlockIndex>(block);index->phashBlock=&hashes.back();index->pprev=height?blocks.back().get():nullptr;
        index->nHeight=height;index->nStatus=BLOCK_VALID_SCRIPTS;index->nTx=1;index->BuildSkip();blocks.push_back(std::move(index));
        view.head={hashes.front(),hashes.back()};auto& tip=*blocks.back();
        check(archive.StoreBlock(view,tip,height,block,archive_budget)==RequestArchiveVerdict::Stored);
        const auto loaded=archive.Load(view,tip,height,archive_budget);check(loaded.block.has_value());
        const auto sealed=ScanConfirmedTicketRequests(view,tip,*loaded.block,scan);check(sealed.batch.has_value());
        if(!registry){auto start=BeginConfirmedRequestRegistry(view,tip,*sealed.batch);check(start.registry.has_value());registry=std::move(start.registry);initial_charge=*registry->RpcCacheCharge(RpcHistoryCacheBudget);}
        else check(AdvanceConfirmedRequestRegistry(view,tip,*registry,*sealed.batch,262144)==RequestRegistryVerdict::Advanced);
    }
    const auto charge=registry->RpcCacheCharge(RpcHistoryCacheBudget);check(charge.has_value()&&*charge>initial_charge);
    check(!registry->RpcCacheCharge(0));check(!registry->RpcCacheCharge(*charge-1));check(registry->RpcCacheCharge(*charge)==charge);
    check(registry->RpcCacheCharge(std::numeric_limits<size_t>::max())==charge);
    check(registry->Height()==39&&registry->Size()==0);
    const auto absence=LookupConfirmedRequest(view,*blocks.back(),*registry,hashes.front());
    check(absence.verdict==RequestRegistryVerdict::ReadyAtParent&&!absence.location);
    auto snapshot=std::make_shared<const VerifiedRequestRegistry>(std::move(*registry));registry.reset();
    RpcHistorySnapshotCache<VerifiedRequestRegistry,uint256> cache;int state=0,other=0;
    check(!cache.get(&state,hashes.back(),hashes.front()));
    check(cache.retain(&state,hashes.back(),hashes.front(),snapshot));
    for(unsigned i=0;i<10000;++i)check(cache.get(&state,hashes.back(),hashes.front()).get()==snapshot.get());
    check(!cache.get(&state,hashes[38],hashes.front()));check(!cache.get(&state,hashes.back(),hashes[1]));check(!cache.get(&other,hashes.back(),hashes.front()));
    check(!cache.retain(&state,hashes.back(),hashes.front(),snapshot,*charge-1));check(!cache.get(&state,hashes.back(),hashes.front()));
    check(cache.retain(&state,hashes.back(),hashes.front(),snapshot,*charge));
    auto held=cache.get(&state,hashes.back(),hashes.front());std::weak_ptr<const VerifiedRequestRegistry> weak=held;
    snapshot.reset();cache.reset();check(!weak.expired());check(held->Height()==39);held.reset();check(weak.expired());
    check(!cache.retain(&state,hashes.back(),hashes.front(),{}));
    std::cout<<"PASS "<<checks<<" native history cache checks; 40 authenticated archive blocks; 10000 same-object hits; charge "<<*charge<<" bytes; no end-to-end speed claim\n";
}

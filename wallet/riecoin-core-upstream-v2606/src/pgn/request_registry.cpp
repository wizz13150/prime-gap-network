#include <pgn/request_registry.h>
#include <chain.h>
#include <coins.h>
#include <validation.h>
#include <limits>

namespace pgn::core {
namespace {
bool Current(const CCoinsView& view,const CBlockIndex& parent,const uint256& chain){
    AssertLockHeld(cs_main);
    const auto head=view.GetPgnFactHead();
    return head&&parent.phashBlock&&parent.nHeight>=0&&parent.IsValid(BLOCK_VALID_SCRIPTS)&&
        head->chain==chain&&head->tip==parent.GetBlockHash()&&head->tip==view.GetBestBlock();
}
bool Ancestor(const CBlockIndex& parent,int32_t height,const uint256& hash){
    const auto* block=height>=0&&height<=parent.nHeight?parent.GetAncestor(height):nullptr;
    return block&&block->phashBlock&&block->IsValid(BLOCK_VALID_SCRIPTS)&&block->GetBlockHash()==hash;
}
}
// The only internal copy capability for this otherwise move-only sealed type.
// A snapshot is reused only on authenticated native ancestry. Partial snapshots
// retain progress but cannot make LookupConfirmedRequest claim known absence.
class ConfirmedRequestReplayCache {
    std::optional<VerifiedRequestRegistry> m_snapshot;
public:
    std::optional<VerifiedRequestRegistry> Restore(const CCoinsView& view,
        const CBlockIndex& parent,size_t maximum)const
    {
        if(!m_snapshot||m_snapshot->Size()>maximum||!Current(view,parent,m_snapshot->Chain()))return {};
        VerifiedRequestRegistry copy{*m_snapshot};
        return std::optional<VerifiedRequestRegistry>{std::move(copy)};
    }
    void Remember(const CCoinsView& view,const CBlockIndex& parent,const VerifiedRequestRegistry& registry)
    {
        if(!Current(view,parent,registry.Chain())||!Ancestor(parent,registry.Height(),registry.Tip()))return;
        m_snapshot=VerifiedRequestRegistry{registry};
    }
};
RequestRegistryStart BeginConfirmedRequestRegistry(const CCoinsView& view,const CBlockIndex& parent,const ConfirmedRequestBatch& genesis){
    AssertLockHeld(cs_main);
    if(!ConfirmedRequestBatchOnBranch(view,parent,genesis))return {RequestRegistryVerdict::InvalidParent,{}};
    // A valid signed request requires an EARLIER anchor, so height0 cannot
    // contain one. Never enroll an arbitrary historical nonempty starting set.
    if(genesis.Height()!=0||!genesis.Requests().empty())return {RequestRegistryVerdict::InvalidGenesis,{}};
    return {RequestRegistryVerdict::GenesisReady,VerifiedRequestRegistry{genesis.Chain(),genesis.Block()}};
}
RequestRegistryVerdict AdvanceConfirmedRequestRegistry(const CCoinsView& view,const CBlockIndex& parent,
    VerifiedRequestRegistry& registry,const ConfirmedRequestBatch& batch,size_t maximum){
    AssertLockHeld(cs_main);
    if(registry.m_blocks.empty()||!Current(view,parent,registry.Chain())||batch.Chain()!=registry.Chain()||
       !ConfirmedRequestBatchOnBranch(view,parent,batch))return RequestRegistryVerdict::InvalidParent;
    if(registry.Height()==std::numeric_limits<int32_t>::max()||batch.Height()!=registry.Height()+1||
       !Ancestor(parent,registry.Height(),registry.Tip()))return RequestRegistryVerdict::WrongNextBlock;
    if(!maximum||registry.Size()>maximum||batch.Requests().size()>maximum-registry.Size())return RequestRegistryVerdict::BudgetExceeded;
    std::map<uint256,RequestLocation,VerifiedRequestRegistry::Less> staged;
    VerifiedRequestRegistry::Block step{batch.Block(),{}};
    step.requests.reserve(batch.Requests().size());
    for(const auto& request:batch.Requests()){
        const auto& id=request.Request().Id();
        if(registry.m_entries.contains(id)||!staged.emplace(id,RequestLocation{request.Block(),request.Height(),request.Output()}).second)
            return RequestRegistryVerdict::DuplicateRequestId;
        step.requests.push_back(id);
    }
    // Allocate every node/undo slot BEFORE semantic mutation. With this fixed
    // noexcept comparator, node transfer needs no allocations or verification.
    if(registry.m_blocks.size()==registry.m_blocks.capacity())
        registry.m_blocks.reserve(std::max(size_t{8},registry.m_blocks.size()*2));
    registry.m_entries.merge(staged);
    registry.m_blocks.push_back(std::move(step));
    return RequestRegistryVerdict::Advanced;
}
RequestRegistryVerdict RewindConfirmedRequestRegistry(const CCoinsView& view,const CBlockIndex& parent,VerifiedRequestRegistry& registry){
    AssertLockHeld(cs_main);
    if(registry.m_blocks.empty()||!Current(view,parent,registry.Chain()))return RequestRegistryVerdict::InvalidParent;
    int32_t common=std::min(parent.nHeight,registry.Height());
    for(;common>=0&&!Ancestor(parent,common,registry.m_blocks[size_t(common)].hash);--common){}
    if(common<0)return RequestRegistryVerdict::InvalidParent;
    if(common==registry.Height())return RequestRegistryVerdict::AlreadyOnBranch;
    while(registry.Height()>common){
        for(const auto& id:registry.m_blocks.back().requests)registry.m_entries.erase(id);
        registry.m_blocks.pop_back();
    }
    return RequestRegistryVerdict::Rewound;
}
RequestRegistryLookup LookupConfirmedRequest(const CCoinsView& view,const CBlockIndex& parent,
    const VerifiedRequestRegistry& registry,const uint256& id){
    AssertLockHeld(cs_main);
    if(registry.m_blocks.empty()||!Current(view,parent,registry.Chain()))return {RequestRegistryVerdict::InvalidParent,{}};
    if(registry.Height()!=parent.nHeight||registry.Tip()!=parent.GetBlockHash())return {RequestRegistryVerdict::Incomplete,{}};
    const auto found=registry.m_entries.find(id);
    return {RequestRegistryVerdict::ReadyAtParent,found==registry.m_entries.end()?std::nullopt:std::optional{found->second}};
}
static RequestReplayResult ReplayConfirmedRequestHistorySlice(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& archive,std::optional<VerifiedRequestRegistry>& registry,RequestReplayBudget budget){
    AssertLockHeld(cs_main);
    RequestReplayResult result{RequestReplayVerdict::MoreWork};
    if(!budget.maximum_blocks||!budget.maximum_total_requests)return {RequestReplayVerdict::BudgetExceeded};
    if(registry){
        if(registry->Height()<0)return {RequestReplayVerdict::InvalidParent};
        const auto before=registry->Height();const auto rewind=RewindConfirmedRequestRegistry(view,parent,*registry);
        if(rewind==RequestRegistryVerdict::InvalidParent)return {RequestReplayVerdict::InvalidParent};
        result.rewound=before-registry->Height();
    }
    while(result.blocks<budget.maximum_blocks){
        if(registry&&registry->Height()==parent.nHeight&&registry->Tip()==parent.GetBlockHash()){
            result.verdict=RequestReplayVerdict::AtParent;return result;
        }
        const int32_t height=registry?registry->Height()+1:0;
        const auto loaded=archive.Load(view,parent,height,budget.archive);
        if(!loaded.block){
            result.held_height=height;
            result.verdict=loaded.verdict==RequestArchiveVerdict::Missing?RequestReplayVerdict::MissingHistory:
                loaded.verdict==RequestArchiveVerdict::BudgetExceeded?RequestReplayVerdict::BudgetExceeded:
                loaded.verdict==RequestArchiveVerdict::InvalidParent?RequestReplayVerdict::InvalidParent:RequestReplayVerdict::InvalidHistory;
            return result;
        }
        auto scanned=ScanConfirmedTicketRequests(view,parent,*loaded.block,budget.scan);
        if(!scanned.batch){result.verdict=scanned.verdict==ConfirmedRequestScanVerdict::BudgetExceeded?
            RequestReplayVerdict::BudgetExceeded:RequestReplayVerdict::InvalidHistory;result.held_height=height;return result;}
        if(!registry){auto started=BeginConfirmedRequestRegistry(view,parent,*scanned.batch);
            if(!started.registry){result.verdict=RequestReplayVerdict::InvalidHistory;result.held_height=height;return result;}
            registry=std::move(started.registry);
        }else{
            const auto advanced=AdvanceConfirmedRequestRegistry(view,parent,*registry,*scanned.batch,budget.maximum_total_requests);
            if(advanced!=RequestRegistryVerdict::Advanced){result.verdict=advanced==RequestRegistryVerdict::BudgetExceeded?RequestReplayVerdict::BudgetExceeded:RequestReplayVerdict::InvalidHistory;result.held_height=height;return result;}
        }
        ++result.blocks;
    }
    if(registry&&registry->Height()==parent.nHeight&&registry->Tip()==parent.GetBlockHash())result.verdict=RequestReplayVerdict::AtParent;
    return result;
}
RequestReplayResult ReplayConfirmedRequestHistory(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& archive,std::optional<VerifiedRequestRegistry>& registry,RequestReplayBudget budget)
{
    AssertLockHeld(cs_main);
    static ConfirmedRequestReplayCache cache;
    if(!registry&&budget.maximum_blocks&&budget.maximum_total_requests)
        registry=cache.Restore(view,parent,budget.maximum_total_requests);
    auto result=ReplayConfirmedRequestHistorySlice(view,parent,archive,registry,budget);
    if(registry&&registry->Size()<=budget.maximum_total_requests&&
       (result.verdict==RequestReplayVerdict::AtParent||result.verdict==RequestReplayVerdict::MoreWork||
        result.verdict==RequestReplayVerdict::MissingHistory||result.verdict==RequestReplayVerdict::BudgetExceeded))
        cache.Remember(view,parent,*registry);
    return result;
}
RequestReplayResult ReplayConfirmedRequestHistoryToParent(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& archive,std::optional<VerifiedRequestRegistry>& registry,RequestReplayBudget budget)
{
    AssertLockHeld(cs_main);
    RequestReplayResult total{RequestReplayVerdict::MoreWork,0,0,{}};
    for(;;){
        const auto step=ReplayConfirmedRequestHistory(view,parent,archive,registry,budget);
        total.blocks+=step.blocks;total.rewound+=step.rewound;total.verdict=step.verdict;total.held_height=step.held_height;
        if(step.verdict!=RequestReplayVerdict::MoreWork||!step.blocks)return total;
    }
}
}

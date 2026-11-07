// MIT. Candidate complete-chain replay; no consensus activation or storage claim.
#pragma once
#include <pgn/ticket_request_confirmation.h>
#include <pgn/request_history_archive.h>
#include <map>

namespace pgn::core {
struct RequestLocation {
    uint256 block;int32_t height;uint32_t output;
    bool operator==(const RequestLocation&)const=default;
};
enum class RequestRegistryVerdict {GenesisReady,Advanced,Rewound,AlreadyOnBranch,ReadyAtParent,
    InvalidParent,InvalidGenesis,WrongNextBlock,DuplicateRequestId,BudgetExceeded,Incomplete};
struct RequestRegistryStart;
struct RequestRegistryLookup;
class VerifiedRequestRegistry final {
    struct Less {bool operator()(const uint256& a,const uint256& b)const noexcept{return a<b;}};
    struct Block {uint256 hash;std::vector<uint256> requests;};
    uint256 m_chain;
    std::vector<Block> m_blocks;
    std::map<uint256,RequestLocation,Less> m_entries;
    VerifiedRequestRegistry(uint256 chain,uint256 genesis):m_chain(chain),m_blocks{{genesis,{}}}{}
    VerifiedRequestRegistry(const VerifiedRequestRegistry&)=default;
    friend class ConfirmedRequestReplayCache;
    friend RequestRegistryStart BeginConfirmedRequestRegistry(const CCoinsView&,const CBlockIndex&,const ConfirmedRequestBatch&);
    friend RequestRegistryVerdict AdvanceConfirmedRequestRegistry(const CCoinsView&,const CBlockIndex&,
        VerifiedRequestRegistry&,const ConfirmedRequestBatch&,size_t);
    friend RequestRegistryVerdict RewindConfirmedRequestRegistry(const CCoinsView&,const CBlockIndex&,VerifiedRequestRegistry&);
    friend RequestRegistryLookup LookupConfirmedRequest(const CCoinsView&,const CBlockIndex&,const VerifiedRequestRegistry&,const uint256&);
public:
    VerifiedRequestRegistry& operator=(const VerifiedRequestRegistry&)=delete;
    VerifiedRequestRegistry(VerifiedRequestRegistry&&)=default;
    VerifiedRequestRegistry& operator=(VerifiedRequestRegistry&&)=default;
    const uint256& Chain()const{return m_chain;}
    uint256 Tip()const{return m_blocks.empty()?uint256{}:m_blocks.back().hash;}
    int32_t Height()const{return m_blocks.empty()?-1:int32_t(m_blocks.size()-1);}
    size_t Size()const{return m_entries.size();}
    // RPC retention charge only, never a network admission or consensus limit.
    // Count allocated vector capacities and conservative fixed map-node costs.
    // Overflow or a charge outside budget declines caching, not history access.
    std::optional<size_t> RpcCacheCharge(size_t budget)const{
        size_t used=0;
        const auto charge=[&](size_t count,size_t width){
            if(!width||count>(budget-used)/width)return false;
            used+=count*width;return true;
        };
        if(!charge(1,sizeof(*this)+1024)||!charge(m_entries.size(),256)
                ||!charge(m_blocks.capacity(),sizeof(Block)+32))return {};
        for(const auto& block:m_blocks)if(!charge(block.requests.capacity(),sizeof(uint256)+16))return {};
        return used;
    }
};
struct RequestRegistryStart {RequestRegistryVerdict verdict;std::optional<VerifiedRequestRegistry> registry;};
struct RequestRegistryLookup {
    RequestRegistryVerdict verdict;
    // Null location means known ABSENT only when verdict==ReadyAtParent.
    std::optional<RequestLocation> location;
};
// Native cs_main held throughout. Only complete sealed ONE-block batches can
// enter, from genesis with no gaps. Never equate partial replay with a registry
// current at the native tip. Every request ID is globally indexed, not namespaced
// by miner/reward/device and NOT written into the scientific-fact namespace.
RequestRegistryStart BeginConfirmedRequestRegistry(const CCoinsView&,const CBlockIndex&,const ConfirmedRequestBatch&);
// Explicit resource budget, NOT a proposed network maximum. Failure leaves all
// semantic state unchanged. O(new IDs log(history IDs)), no whole-map copy.
RequestRegistryVerdict AdvanceConfirmedRequestRegistry(const CCoinsView&,const CBlockIndex&,
    VerifiedRequestRegistry&,const ConfirmedRequestBatch&,size_t maximum_total_requests);
// Exact compact per-block undo to actual common ancestor. Must replay every
// replacement block before lookup may again report authoritative absence.
RequestRegistryVerdict RewindConfirmedRequestRegistry(const CCoinsView&,const CBlockIndex&,VerifiedRequestRegistry&);
RequestRegistryLookup LookupConfirmedRequest(const CCoinsView&,const CBlockIndex&,const VerifiedRequestRegistry&,const uint256&);
struct RequestReplayBudget {
    uint32_t maximum_blocks;size_t maximum_total_requests;
    RequestHistoryArchiveBudget archive;ConfirmedRequestScanBudget scan;
};
enum class RequestReplayVerdict {AtParent,MoreWork,MissingHistory,InvalidHistory,InvalidParent,BudgetExceeded,
    BlockDataUnavailable,StorageError};
struct RequestReplayResult {RequestReplayVerdict verdict;uint32_t blocks{},rewound{};std::optional<int32_t> held_height;};
// Bounded startup/reorg slice from authenticated native archive, never an
// archive-supplied list of blocks or claimed-complete head. Partial valid replay
// is retained but grants NO known-absence until exact current native tip.
RequestReplayResult ReplayConfirmedRequestHistory(const CCoinsView&,const CBlockIndex&,
    const NativeRequestHistoryArchive&,std::optional<VerifiedRequestRegistry>&,RequestReplayBudget);
// Complete-at-parent admission boundary. Slices retain their local work budget;
// trusted ancestry bounds total cold replay. No progress/errors stay explicit.
RequestReplayResult ReplayConfirmedRequestHistoryToParent(const CCoinsView&,const CBlockIndex&,
    const NativeRequestHistoryArchive&,std::optional<VerifiedRequestRegistry>&,RequestReplayBudget);
// This in-memory replay component is NOT a durable CCoinsView registry, range
// allocator, chain activation, seed-quality proof or accepted network policy.
}

// Copyright (c) 2026 The PGN developers. MIT.
// RPC-only immutable history anchor/cache. Never used by consensus/admission.
#pragma once
#include <pgn/native_leaderboard.h>
#include <chain.h>
#include <coins.h>
#include <memory>
#include <mutex>

namespace pgn::core {
// Capture actual authenticated state while locked. This is deliberately NOT
// a UTXO/fact registry snapshot: it provides only the head needed to verify
// immutable archived coinbase proofs against their real captured block index.
class LeaderboardHistoryAnchorView final : public CoinsViewEmpty {
    const FactHead head_;
    explicit LeaderboardHistoryAnchorView(FactHead head):head_(std::move(head)){}
public:
    static std::unique_ptr<LeaderboardHistoryAnchorView> Capture(const CCoinsView& view,const CBlockIndex& anchor){
        AssertLockHeld(cs_main);
        const auto head=view.GetPgnFactHead();
        if(!head||!head->Valid()||head->tip!=anchor.GetBlockHash()||view.GetBestBlock()!=head->tip||!anchor.IsValid(BLOCK_VALID_SCRIPTS))
            throw std::runtime_error("Authenticated leaderboard anchor capture failed");
        return std::unique_ptr<LeaderboardHistoryAnchorView>(new LeaderboardHistoryAnchorView(*head));
    }
    uint256 GetBestBlock()const override{return head_.tip;}
    std::optional<FactHead> GetPgnFactHead()const override{return head_;}
    void BatchWrite(CoinsViewCacheCursor&,const uint256&)override{throw std::logic_error("Leaderboard history anchor is read-only");}
};
inline bool LeaderboardAnchorCanonical(const NativeLeaderboardSnapshot& snapshot,const CChain& active){
    AssertLockHeld(cs_main);
    const auto* block=active[snapshot.height];
    return block&&block->GetBlockHash()==snapshot.tip;
}
class LeaderboardSnapshotCache {
    std::mutex mutex_;
    std::shared_ptr<const NativeLeaderboardSnapshot> newest_,previous_;
    static bool Matches(const std::shared_ptr<const NativeLeaderboardSnapshot>& value,const void* state,const uint256& genesis,const CChain& active){
        return value&&value->chainstate==state&&value->genesis==genesis&&LeaderboardAnchorCanonical(*value,active);
    }
public:
    std::mutex build_mutex;
    std::shared_ptr<const NativeLeaderboardSnapshot> Get(const void* state,const uint256& tip,const uint256& genesis,const CChain& active){
        AssertLockHeld(cs_main);std::lock_guard guard(mutex_);
        for(const auto& value:{newest_,previous_})if(Matches(value,state,genesis,active)&&value->tip==tip)return value;
        return {};
    }
    // Keep only one prior canonical projection before allocating a successor.
    // At rest the retained set is at most old+new, never an all-time catalogue.
    std::shared_ptr<const NativeLeaderboardSnapshot> BeginBuild(const void* state,const uint256& genesis,const CChain& active){
        AssertLockHeld(cs_main);std::lock_guard guard(mutex_);
        auto keep=Matches(newest_,state,genesis,active)?newest_:Matches(previous_,state,genesis,active)?previous_:nullptr;
        previous_.reset();newest_=keep;return keep;
    }
    void Publish(std::shared_ptr<const NativeLeaderboardSnapshot> value,const CChain& active){
        AssertLockHeld(cs_main);
        if(!LeaderboardAnchorCanonical(*value,active))throw std::runtime_error("Leaderboard snapshot reset: anchor was reorganized");
        std::lock_guard guard(mutex_);
        previous_=Matches(newest_,value->chainstate,value->genesis,active)&&newest_->tip!=value->tip?newest_:nullptr;
        newest_=std::move(value);
    }
};
} // namespace pgn::core

// RPC accelerator only. Consensus must never consume this cache.
#pragma once
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace pgn::core {
inline constexpr size_t RpcAuditCacheBudget = 64 * 1024 * 1024;

// Conservative fixed-node charge plus actual retained variable capacities.
// Each addition/multiplication is checked before it can overflow. A ledger
// outside the charge budget is returned to its caller but is never retained.
template<class Ledger>
std::optional<size_t> RpcAuditCacheCharge(const Ledger& ledger,size_t budget=RpcAuditCacheBudget){
    size_t used=0;
    const auto charge=[&](size_t count,size_t width=1){
        if(!width||count>(budget-used)/width)return false;
        used+=count*width;return true;
    };
    if(!charge(1,sizeof(Ledger))||!charge(ledger.size(),4096)
            ||!charge(ledger.participants.size(),384)||!charge(ledger.authority.capacity()))return {};
    for(const auto& [key,participant]:ledger.participants)if(!charge(key.capacity()))return {};
    for(const auto& [id,report]:ledger){
        if(!charge(report.owner_reveal.capacity())||!charge(report.owner.capacity())
                ||!charge(report.report.commitment.signature.capacity()))return {};
        for(const auto& job:report.jobs)if(job){
            if(!charge(job->reveal.capacity())||!charge(job->actor.capacity())||!charge(job->commitment.capacity()))return {};
        }
    }
    return used;
}

template<class Ledger,class Hash>
class RpcAuditSnapshotCache {
    const void* chainstate_{};
    Hash tip_{},genesis_{};
    std::shared_ptr<const Ledger> ledger_;
public:
    void reset(){chainstate_=nullptr;ledger_.reset();}
    std::shared_ptr<const Ledger> get(const void* chainstate,const Hash& tip,const Hash& genesis)const{
        if(chainstate_==chainstate&&tip_==tip&&genesis_==genesis)return ledger_;
        return {};
    }
    // A caller must supply its verified active child's exact parent. Build
    // outside the retained snapshot: failed/oversized successors cannot leak
    // partially applied events, and readers of the old snapshot stay coherent.
    template<class Builder>
    std::shared_ptr<const Ledger> advance(const void* chainstate,const Hash& parent,
        const Hash& tip,const Hash& genesis,Builder&& build,size_t budget=RpcAuditCacheBudget){
        auto previous=get(chainstate,parent,genesis);
        reset();
        if(!previous||parent==tip)return {};
        auto next=std::forward<Builder>(build)(*previous);
        if(!next)return {};
        auto snapshot=std::make_shared<const Ledger>(std::move(*next));
        // A valid but oversized result can still be returned to this caller;
        // retain() enforces the original byte budget for future requests.
        retain(chainstate,tip,genesis,snapshot,budget);
        return snapshot;
    }
    bool retain(const void* chainstate,const Hash& tip,const Hash& genesis,
                std::shared_ptr<const Ledger> ledger,size_t budget=RpcAuditCacheBudget){
        reset();
        if(!ledger||!RpcAuditCacheCharge(*ledger,budget))return false;
        chainstate_=chainstate;tip_=tip;genesis_=genesis;ledger_=std::move(ledger);return true;
    }
};
}

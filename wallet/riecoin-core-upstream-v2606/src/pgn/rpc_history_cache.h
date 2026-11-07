// Read-only RPC accelerator; consensus never consumes this retained snapshot.
#pragma once
#include <cstddef>
#include <memory>
#include <utility>
namespace pgn::core {
inline constexpr size_t RpcHistoryCacheBudget=64*1024*1024;
template<class Registry,class Hash>
class RpcHistorySnapshotCache {
    const void* chainstate_{};
    Hash tip_{},genesis_{};
    std::shared_ptr<const Registry> registry_;
public:
    void reset(){chainstate_=nullptr;registry_.reset();}
    std::shared_ptr<const Registry> get(const void* chainstate,const Hash& tip,const Hash& genesis)const{
        if(chainstate_==chainstate&&tip_==tip&&genesis_==genesis)return registry_;
        return {};
    }
    bool retain(const void* chainstate,const Hash& tip,const Hash& genesis,
                std::shared_ptr<const Registry> registry,size_t budget=RpcHistoryCacheBudget){
        reset();
        if(!chainstate||!registry||!registry->RpcCacheCharge(budget))return false;
        chainstate_=chainstate;tip_=tip;genesis_=genesis;registry_=std::move(registry);return true;
    }
};
}

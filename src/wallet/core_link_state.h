#pragma once
#include "wallet/chain_observation.h"

namespace pgn::wallet {
enum class CoreLinkState { Starting, Ready, RpcUnavailable, Stopped, Rejected };
inline QString coreLinkKey(CoreLinkState state){
    switch(state){
    case CoreLinkState::Starting:return "starting";
    case CoreLinkState::Ready:return "ready";
    case CoreLinkState::RpcUnavailable:return "rpc_unavailable";
    case CoreLinkState::Stopped:return "stopped";
    case CoreLinkState::Rejected:return "rejected";
    }
    return "rejected";
}
inline CoreLinkState coreLinkState(const QString& key){
    if(key=="starting")return CoreLinkState::Starting;
    if(key=="ready")return CoreLinkState::Ready;
    if(key=="rpc_unavailable")return CoreLinkState::RpcUnavailable;
    if(key=="stopped")return CoreLinkState::Stopped;
    return CoreLinkState::Rejected;
}
enum class NetworkLinkCondition { Connecting, Unavailable, Disconnected, Unverified, Stale, NoPeers, PeersUnknown, Syncing, SyncUnknown, Current };
inline NetworkLinkCondition networkLinkCondition(CoreLinkState state,const std::optional<ChainObservation>& observation,const QDateTime& now){
    // A missing/stale RPC observation does not prove that the process or its
    // peer connection exited. It must never be presented as a fresh height.
    switch(state){
    case CoreLinkState::Starting:return NetworkLinkCondition::Connecting;
    case CoreLinkState::RpcUnavailable:return NetworkLinkCondition::Unavailable;
    case CoreLinkState::Stopped:return NetworkLinkCondition::Disconnected;
    case CoreLinkState::Rejected:return NetworkLinkCondition::Unverified;
    case CoreLinkState::Ready:break;
    }
    if(!observation||!observation->freshAt(now))return NetworkLinkCondition::Stale;
    if(!observation->peers)return NetworkLinkCondition::PeersUnknown;
    if(*observation->peers==0)return NetworkLinkCondition::NoPeers;
    if(!observation->synchronizing)return NetworkLinkCondition::SyncUnknown;
    return *observation->synchronizing?NetworkLinkCondition::Syncing:NetworkLinkCondition::Current;
}
inline bool verifiedCoreOnline(CoreLinkState state,const std::optional<ChainObservation>& observation,const QDateTime& now){
    return state==CoreLinkState::Ready&&observation&&observation->freshAt(now);
}
}

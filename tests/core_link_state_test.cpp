#include "wallet/core_link_state.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);using namespace pgn::wallet;
    int checks=0;
    auto require=[&](bool okay){++checks;if(!okay)throw std::runtime_error("link check "+std::to_string(checks));};
    const auto now=QDateTime::currentDateTimeUtc();
    ChainObservation observation{{42,"owned-process-birth",1},QString(64,'a'),18,now,false,QString(64,'b'),1};
    require(networkLinkCondition(CoreLinkState::Starting,{},now)==NetworkLinkCondition::Connecting);
    require(verifiedCoreOnline(CoreLinkState::Ready,observation,now));
    require(networkLinkCondition(CoreLinkState::Ready,observation,now)==NetworkLinkCondition::Current);
    // A timeout must revoke the live claim even if a previous tip is retained.
    require(!verifiedCoreOnline(CoreLinkState::RpcUnavailable,observation,now));
    require(networkLinkCondition(CoreLinkState::RpcUnavailable,observation,now)==NetworkLinkCondition::Unavailable);
    require(networkLinkCondition(CoreLinkState::RpcUnavailable,{},now)==NetworkLinkCondition::Unavailable);
    require(networkLinkCondition(CoreLinkState::Stopped,observation,now)==NetworkLinkCondition::Disconnected);
    require(networkLinkCondition(CoreLinkState::Rejected,observation,now)==NetworkLinkCondition::Unverified);
    require(networkLinkCondition(CoreLinkState::Ready,{},now)==NetworkLinkCondition::Stale);
    require(networkLinkCondition(CoreLinkState::Ready,observation,now.addMSecs(30001))==NetworkLinkCondition::Stale);
    require(!verifiedCoreOnline(CoreLinkState::Ready,observation,now.addMSecs(30001)));
    require(!verifiedCoreOnline(CoreLinkState::Ready,observation,now.addMSecs(-1)));
    observation.peers.reset();
    require(networkLinkCondition(CoreLinkState::Ready,observation,now)==NetworkLinkCondition::PeersUnknown);
    observation.peers=0;
    require(networkLinkCondition(CoreLinkState::Ready,observation,now)==NetworkLinkCondition::NoPeers);
    observation.peers=1;observation.synchronizing=true;
    require(networkLinkCondition(CoreLinkState::Ready,observation,now)==NetworkLinkCondition::Syncing);
    observation.synchronizing.reset();
    require(networkLinkCondition(CoreLinkState::Ready,observation,now)==NetworkLinkCondition::SyncUnknown);
    observation.synchronizing=false;observation.observedUtc=now.addSecs(40);
    require(networkLinkCondition(CoreLinkState::Ready,observation,now.addSecs(40))==NetworkLinkCondition::Current);
    for(auto state:{CoreLinkState::Starting,CoreLinkState::Ready,CoreLinkState::RpcUnavailable,CoreLinkState::Stopped,CoreLinkState::Rejected})require(coreLinkState(coreLinkKey(state))==state);
    require(coreLinkState("unknown")==CoreLinkState::Rejected);
    std::cout<<"PASS "<<checks<<" network states: timeout, stale, process exit, identity, peers, sync and recovery\n";
}

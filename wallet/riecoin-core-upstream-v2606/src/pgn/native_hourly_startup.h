// Copyright (c) 2026 The PGN developers. MIT.
#pragma once
#include <pgn/native_hourly_loaded_targets.h>
#include <pgn/native_admission_parent.h>
#include <util/signalinterrupt.h>
#include <algorithm>
#include <set>
#include <string>
#include <vector>
namespace pgn::core {
// Called before publishing block-index candidates or leaving chainstate load.
// No block is reclassified, blessed or written by this read-only readiness gate.
inline bool VerifyLoadedNativeHourlyTargets(Chainstate& active,std::string& failure)
{
    AssertLockHeld(cs_main);
    auto& manager=active.m_chainman;
    const auto genesis=manager.GetConsensus().hashGenesisBlock;
    const auto activation=ConfiguredNativeHourlyMerit(genesis);
    if(!activation)return true;
    if(activation->genesis!=genesis||activation->median_time<0||activation->median_time%3600){
        failure="invalid compiled hourly activation";return false;
    }
    const auto* tip=active.m_chain.Tip();
    // Empty coins are replayed through normal full block admission, not sealed
    // as an already authenticated hourly prefix.
    if(!tip)return true;
    const auto& science=manager.m_options.pgn_candidate_science_budget;
    const auto& bootstrap=manager.m_options.pgn_candidate_bootstrap_budget;
    if(!science||!bootstrap){failure="complete hourly native profile unavailable";return false;}
    std::vector<const CBlockIndex*> candidates;
    for(const auto* index:active.m_blockman.GetAllBlockIndices()){
        if(index->pprev&&index->IsValid(BLOCK_VALID_TRANSACTIONS)&&index->nNonce==0&&
           index->pprev->GetMedianTimePast()>=activation->median_time)candidates.push_back(index);
    }
    if(candidates.empty())return true;
    std::sort(candidates.begin(),candidates.end(),[](const auto* a,const auto* b){return a->nHeight>b->nHeight;});
    std::optional<VerifiedRequestRegistry> registry;
    auto replay=active.ReplayPgnRequestHistory(registry,{science->schedule.maximum_blocks,
        science->schedule.maximum_requests,science->schedule.archive,science->schedule.scan});
    while(replay.verdict==RequestReplayVerdict::MoreWork&&replay.blocks){
        if(manager.m_interrupt){failure="hourly history verification interrupted";return false;}
        replay=active.ReplayPgnRequestHistory(registry,{science->schedule.maximum_blocks,
            science->schedule.maximum_requests,science->schedule.archive,science->schedule.scan});
    }
    const auto* archive=active.PgnRequestHistory();
    if(replay.verdict!=RequestReplayVerdict::AtParent||!registry||!archive){
        failure="authenticated hourly startup history unavailable";return false;
    }
    const auto range=ReadChainRangePolicy(active.CoinsTip(),*tip,*archive,science->schedule.archive,science->policy);
    if(!range.policy){failure="authenticated hourly startup range policy unavailable";return false;}
    pgn::discussion::complete_return_detail::Cache cache;
    const auto canonical=pgn::discussion::ReadNativeHourlyHistory(active.CoinsTip(),*tip,*range.policy,*archive,
        science->policy,science->schedule,science->signature,cache);
    if(!canonical.history){failure="authenticated hourly startup counters unavailable";return false;}
    std::set<uint256> verified;
    for(const auto* candidate:candidates){
        if(manager.m_interrupt){failure="hourly header verification interrupted";return false;}
        if(verified.contains(candidate->GetBlockHash()))continue;
        // Normal active-chain loading needs one authenticated replay, then six
        // exact counter lookups per hourly target. Side prefixes are independently
        // body/script/proof checked by the existing disposable admission view.
        std::unique_ptr<NativeAdmissionParent> side;
        std::optional<pgn::discussion::NativeHourlyHistory> side_history;
        pgn::discussion::complete_return_detail::Cache side_cache;
        const auto* selected=&*canonical.history;
        if(!active.m_chain.Contains(*candidate->pprev)){
            // The non-const chainstate owns this DERIVED archive; the existing
            // side-prefix verifier stores only fully checked body records.
            auto& branch_archive=*const_cast<NativeRequestHistoryArchive*>(archive);
            side=BuildNativeAdmissionParent(active,*candidate->pprev,*science,bootstrap.get(),branch_archive,failure);
            if(!side)return false;
            const auto branch_range=ReadChainRangePolicy(*side->coins,*side->parent,branch_archive,science->schedule.archive,science->policy);
            if(!branch_range.policy){failure="hourly side-prefix range policy unavailable";return false;}
            const auto branch=pgn::discussion::ReadNativeHourlyHistory(*side->coins,*side->parent,*branch_range.policy,branch_archive,
                science->policy,science->schedule,science->signature,side_cache);
            if(!branch.history){failure="hourly side-prefix counters unavailable";return false;}
            side_history=branch.history;selected=&*side_history;
        }
        for(const auto* index=candidate;index&&index->pprev&&
                index->pprev->GetMedianTimePast()>=activation->median_time;index=index->pprev){
            if(verified.contains(index->GetBlockHash()))break;
            if(index->nNonce==0&&!NativeLoadedHourlyHeaderMatches(*index,*selected)){
                failure="hourly target mismatch at "+index->GetBlockHash().GetHex()+" height "+std::to_string(index->nHeight);
                return false;
            }
            verified.insert(index->GetBlockHash());
        }
    }
    return true;
}
}

// Copyright (c) 2026 The PGN developers
// MIT. Native database recheck; no legacy proof fallback or persistent UTXO writes.
#pragma once
#include <validation.h>
#include <algorithm>
#include <kernel/chainparams.h>
#include <util/translation.h>
#include <util/signalinterrupt.h>
#include <node/blockstorage.h>
#include <pgn/native_science_monetary_plan.h>
#include <pgn/chain_work_policy.h>
#include <pgn/native_retarget_policy.h>
#include <pgn/candidate_block_bootstrap.h>
#include <pgn/fact_connect.h>
#include <pgn/fact_disconnect.h>
#include <pgn/issuance_disconnect.h>
#include <kernel/notifications_interface.h>
#include <logging.h>
#include <undo.h>
namespace pgn::core {
struct NativeRecheckedBlock {
    std::optional<VerifiedNativeBlockScience> science;
    std::optional<VerifiedNativeBootstrapBlock> bootstrap;
    std::optional<FactHistoryStep> facts;
    std::optional<IssuanceHistoryStep> money;
};

// One recheck holds cs_main and never mutates its base. Reuse immutable reads
// within that scope while still executing every trail/value/forward comparison.
// The adapter cannot flush, does not survive a recheck, and creates no files.
class NativeRecheckReadSnapshot final:public CCoinsViewBacked {
    size_t m_budget;
    mutable size_t m_used{};
    mutable uint64_t m_hits{},m_reads{};
    mutable std::map<uint256,std::optional<IssuanceCommit>> m_money;
    mutable std::map<uint256,std::optional<FactCommit>> m_facts;
    mutable std::map<uint256,std::optional<FactClaim>> m_claims;
    mutable bool m_money_anchor_loaded{},m_fact_anchor_loaded{};
    mutable std::optional<IssuanceSnapshot> m_money_anchor;
    mutable std::optional<FactHead> m_fact_anchor;
    template<typename Value> static size_t Cost(const std::optional<Value>&){
        return sizeof(std::pair<const uint256,std::optional<Value>>)+4*sizeof(void*);
    }
    static size_t Cost(const std::optional<FactCommit>& value){
        return sizeof(std::pair<const uint256,std::optional<FactCommit>>)+4*sizeof(void*)+
            (value?value->changes.capacity()*sizeof(FactChange):0);
    }
    template<typename Value,typename Load>
    std::optional<Value> Read(std::map<uint256,std::optional<Value>>& cache,const uint256& key,Load load)const{
        AssertLockHeld(cs_main);
        if(const auto found=cache.find(key);found!=cache.end()){++m_hits;return found->second;}
        ++m_reads;auto value=load();const auto cost=Cost(value);
        if(cost<=m_budget-m_used){cache.emplace(key,value);m_used+=cost;}
        return value;
    }
public:
    NativeRecheckReadSnapshot(CCoinsView& parent,size_t budget):CCoinsViewBacked(&parent),m_budget(budget){AssertLockHeld(cs_main);}
    std::optional<IssuanceCommit> GetPgnIssuanceForward(const uint256& tip)const override{
        return Read(m_money,tip,[&]{return base->GetPgnIssuanceForward(tip);});
    }
    std::optional<FactCommit> GetPgnFactForward(const uint256& tip)const override{
        return Read(m_facts,tip,[&]{return base->GetPgnFactForward(tip);});
    }
    std::optional<FactClaim> GetPgnFact(const uint256& key)const override{
        return Read(m_claims,key,[&]{return base->GetPgnFact(key);});
    }
    std::optional<IssuanceSnapshot> GetPgnIssuanceHistoryAnchor()const override{
        if(!m_money_anchor_loaded){m_money_anchor=base->GetPgnIssuanceHistoryAnchor();m_money_anchor_loaded=true;}
        return m_money_anchor;
    }
    std::optional<FactHead> GetPgnFactHistoryAnchor()const override{
        if(!m_fact_anchor_loaded){m_fact_anchor=base->GetPgnFactHistoryAnchor();m_fact_anchor_loaded=true;}
        return m_fact_anchor;
    }
    void BatchWrite(CoinsViewCacheCursor&,const uint256&)override{
        throw std::logic_error("Native recheck read snapshot cannot publish state");
    }
    size_t MemoryUsage()const{return m_used;}
    uint64_t Reads()const{return m_reads;}
    uint64_t Hits()const{return m_hits;}
    void Relinquish(){
        m_money.clear();m_facts.clear();m_claims.clear();m_used=0;m_budget=0;
    }
};
// parent_view is the exact historical metadata state, reconstructed using
// retained native inverses, never a fabricated view masking selected claims.
inline std::optional<NativeRecheckedBlock> PrepareNativeBlockForwardAtParent(
    const CCoinsViewCache& parent_view,const CBlockIndex& index,const CBlock& block,
    const NativeRequestHistoryArchive& archive,const NativeBlockScienceBudget& science_budget,
    const NativeBlockBootstrapBudget& bootstrap_budget)
{
    if(!index.pprev||!index.phashBlock||block.GetHash()!=index.GetBlockHash()||
       block.hashPrevBlock!=index.pprev->GetBlockHash())return {};
    NativeRecheckedBlock out;
    if(HasNativeCandidateBlockScienceEnvelope(block)){
        const auto checked=VerifyNativeCandidateBlockScience(parent_view,*index.pprev,archive,block,science_budget);
        if(!checked.candidate||(!checked.candidate->AdaptiveTarget()&&block.nBits!=0))return {};
        out.science=checked.candidate;
        out.facts=PrepareNativeBlockScienceForward(parent_view,*index.pprev,index,*out.science);
    }else{
        const auto checked=VerifyNativeBootstrapBlock(parent_view,*index.pprev,archive,block,bootstrap_budget,&science_budget);
        if(!checked.candidate)return {};
        out.bootstrap=checked.candidate;
        out.facts=PrepareLocalEmptyFactForward(parent_view,index);
    }
    const auto policy=ReadChainMonetaryPolicy(parent_view,*index.pprev,archive,science_budget.schedule.archive,bootstrap_budget.policy);
    if(!policy.policy)return {};
    const NativeAuditSettlementContext audit_context{archive,science_budget.policy,science_budget.schedule,science_budget.signature};
    if(out.science){
        const auto plan=PrepareNativeScienceMonetaryPlan(parent_view,*index.pprev,block,*out.science,*policy.policy,&audit_context);
        if(!plan)return {};
        out.money=plan->Forward();
    }else{
        const auto plan=PrepareNativeAuditCoordinationPlan(parent_view,*index.pprev,block,*policy.policy,audit_context);
        if(!plan||!VerifyAuditSettlementOutputs(plan->page,*block.vtx.front(),{},0))return {};
        out.money=plan->forward;
    }
    if(!out.facts||!out.money)return {};
    return out;
}
// Recheck of PUBLISHED history has the stronger retained-forward obligation.
// A fresh side candidate uses the common full proof/native-body pipeline, not
// an option that silently weakens this authenticated recovery contract.
inline std::optional<NativeRecheckedBlock> RecheckNativeBlockAtParent(
    const CCoinsViewCache& parent_view,const CBlockIndex& index,const CBlock& block,
    const NativeRequestHistoryArchive& archive,const NativeBlockScienceBudget& science_budget,
    const NativeBlockBootstrapBudget& bootstrap_budget)
{
    auto out=PrepareNativeBlockForwardAtParent(parent_view,index,block,archive,science_budget,bootstrap_budget);
    if(!out||parent_view.GetPgnFactForward(index.GetBlockHash())!=out->facts->forward||
       parent_view.GetPgnIssuanceForward(index.GetBlockHash())!=out->money->forward)return {};
    return out;
}
inline VerifyDBResult VerifyNativeDatabase(Chainstate& chainstate,const Consensus::Params& consensus,
    CCoinsView& base,int level,int depth,kernel::Notifications& notifications)
{
    AssertLockHeld(cs_main);
    const auto held=[](const char* reason){
        LogError("Native PGN database recheck held: %s",reason);
        return VerifyDBResult::CORRUPTED_BLOCK_DB;
    };
    try{
        const auto* tip=chainstate.m_chain.Tip();
        if(!tip)return held("active native tip absent");
        const auto& options=chainstate.m_chainman.m_options;
        const auto& science=options.pgn_candidate_science_budget;
        const auto& bootstrap=options.pgn_candidate_bootstrap_budget;
        if(!science||!bootstrap||!options.pgn_candidate_fixed_block_weight||
           *options.pgn_candidate_fixed_block_weight==0)return held("complete native proof/work profile unavailable");
        level=std::clamp(level,0,4);
        if(depth<=0||depth>tip->nHeight)depth=tip->nHeight;
        if(!science->schedule.maximum_blocks)return held("native verification slice unavailable");
        const auto* genesis=tip->GetAncestor(0);
        const auto& params=chainstate.m_chainman.GetParams();
        if(!genesis||genesis->GetBlockHash()!=consensus.hashGenesisBlock||
           !ReadPinnedGenesisBootstrapBits(params.GenesisBlock(),consensus.hashGenesisBlock,bootstrap->policy))
            return held("exact selected native genesis/bootstrap identity invalid");
        const auto own_work=ResolvePinnedNativeWork(params.GenesisBlock(),consensus.hashGenesisBlock,science->policy,*options.pgn_candidate_fixed_block_weight);
        if(!own_work||genesis->nChainWork!=own_work->bootstrap)return held("native genesis/work definition mismatch");
        const auto original_head=base.GetPgnFactHead();
        const auto original_money=base.GetPgnIssuanceState();
        if(!original_head||!original_money||original_head->chain!=genesis->GetBlockHash()||original_head->tip!=tip->GetBlockHash()||
           original_money->tip!=tip->GetBlockHash()||base.GetBestBlock()!=tip->GetBlockHash())
            return held("native DB head/issuance/tip mismatch");
        std::optional<VerifiedRequestRegistry> registry;
        auto replay=chainstate.ReplayPgnRequestHistory(registry,{science->schedule.maximum_blocks,
               science->schedule.maximum_requests,science->schedule.archive,science->schedule.scan});
        // MoreWork is a valid retained prefix, not corruption. Finish the
        // requested accepted history before reporting a database verdict.
        while(replay.verdict==RequestReplayVerdict::MoreWork&&replay.blocks){
            if(chainstate.m_chainman.m_interrupt)return VerifyDBResult::INTERRUPTED;
            replay=chainstate.ReplayPgnRequestHistory(registry,{science->schedule.maximum_blocks,
                science->schedule.maximum_requests,science->schedule.archive,science->schedule.scan});
        }
        if(replay.verdict!=RequestReplayVerdict::AtParent){
            LogError("Native request-history replay verdict=%d height=%d blocks=%u",int(replay.verdict),replay.held_height.value_or(-1),replay.blocks);
            return held("complete own native history unavailable");
        }
        const auto* archive=chainstate.PgnRequestHistory();
        if(!archive)return held("native history archive absent");
        NativeRecheckReadSnapshot reads{base,std::min<size_t>(16U<<20,chainstate.m_coinstip_cache_size_bytes/4)};
        CCoinsViewCache metadata{&reads},coins{&reads};
        const CBlockIndex* lowest=tip;
        bool skipped_memory{};
        for(const auto* index=tip;index&&index->pprev&&index->nHeight>tip->nHeight-depth;index=index->pprev){
            CBlock block;
            if(!chainstate.m_blockman.ReadBlock(block,*index))return held("own native block file unavailable");
            const auto unit=QuoteConfiguredNativeHeaderWork(params.GenesisBlock(),consensus.hashGenesisBlock,science->policy,
                *options.pgn_candidate_fixed_block_weight,block.nNonce,block.nBits);
            if(!unit)return held("native adaptive work unavailable");
            const auto work=index->pprev->nChainWork+*unit;
            if(work<index->pprev->nChainWork||index->nChainWork!=work)return held("native cumulative work mismatch");
            if(level>=1){
                BlockValidationState state;
                if(!CheckBlock(block,state,consensus,false,true))return held("native whole-body structure/Merkle/amount rejected");
                const auto money_rewind=PrepareLocalIssuanceRewind(metadata,*index);
                const auto fact_rewind=PrepareLocalFactRewind(metadata,*index);
                if(!money_rewind||!fact_rewind)return held("complete native inverse missing");
                metadata.SetBestBlock(index->pprev->GetBlockHash());
                metadata.StagePgnIssuanceHistoryStep(*money_rewind);
                metadata.StagePgnFactHistoryStep(*fact_rewind);
                // Authenticate each inverse here. Full native proof and exact
                // retained-forward comparison follow in ascending order below;
                // direct-child readers need not restart at every prior parent.
                if(level>=3&&!skipped_memory){
                    const size_t usage=coins.DynamicMemoryUsage()+chainstate.CoinsTip().DynamicMemoryUsage();
                    // An optimization must not reduce the original UTXO-check
                    // coverage. Relinquish read memoization before that limit.
                    if(usage+reads.MemoryUsage()>chainstate.m_coinstip_cache_size_bytes)reads.Relinquish();
                    if(usage>chainstate.m_coinstip_cache_size_bytes)skipped_memory=true;
                    else{
                        if(coins.GetBestBlock()!=index->GetBlockHash()||
                           chainstate.DisconnectBlock(block,index,coins)!=DISCONNECT_OK)return held("native UTXO undo inconsistent");
                        coins.StagePgnIssuanceHistoryStep(*money_rewind);
                        coins.StagePgnFactHistoryStep(*fact_rewind);
                    }
                }
            }
            if(level>=2&&!index->GetUndoPos().IsNull()){
                CBlockUndo undo;
                if(!chainstate.m_blockman.ReadBlockUndo(undo,*index))return held("native undo file invalid");
            }
            lowest=index->pprev;
            notifications.progress(_("Verifying blocks…"),int(50*(tip->nHeight-index->nHeight+1)/std::max(1,depth)),false);
            if(chainstate.m_chainman.m_interrupt)return VerifyDBResult::INTERRUPTED;
        }
        // At a full rewind, authenticate the exact zero initial allocation.
        // Missing v1/v2 pools are never promoted to a fabricated v3 zero.
        if(level>=1&&lowest->nHeight==0){
            const auto money_policy=ReadChainMonetaryPolicy(metadata,*lowest,*archive,science->schedule.archive,bootstrap->policy);
            if(!money_policy.policy)return held("native genesis monetary definition missing");
            const auto& policy=*money_policy.policy;
            IssuanceSnapshot initial{lowest->GetBlockHash(),PGN_MAX_SUPPLY_ATOMIC,0,0,
                IssuanceBudgetPartition{policy.Id(),policy.Definition().ordinary_cap,0,0,0,0},OrdinaryRewardPools{0,0}};
            if(policy.Audits()&&!policy.Activation()){initial.epochs=GoldEpochBalances{};initial.audit_settlement=AuditSettlementBalances{};}
            if(metadata.GetPgnIssuanceState()!=initial||metadata.GetPgnFactHead()!=std::optional{FactHead{policy.Chain(),lowest->GetBlockHash()}})
                return held("native genesis initial issuance/head mismatch");
        }
        if(level>=1){
            const CBlockIndex* parent=lowest;
            while(parent!=tip){
                auto* index=chainstate.m_chain.Next(*parent);if(!index)return held("native reconnect ancestry missing");
                CBlock block;if(!chainstate.m_blockman.ReadBlock(block,*index))return held("native reconnect body unavailable");
                const auto checked=RecheckNativeBlockAtParent(metadata,*index,block,*archive,*science,*bootstrap);
                if(!checked)return held("native reconnect proof/history rejected");
                // Keep the original level1 zero-fee coinbase obligation even
                // when no spend view is requested or its memory budget is met.
                if(block.vtx.size()==1&&checked->science){
                    const auto money_policy=ReadChainMonetaryPolicy(metadata,*parent,*archive,science->schedule.archive,bootstrap->policy);
                    const NativeAuditSettlementContext audit_context{*archive,science->policy,science->schedule,science->signature};
                    const auto plan=money_policy.policy?PrepareNativeScienceMonetaryPlan(metadata,*parent,block,*checked->science,*money_policy.policy,&audit_context):std::nullopt;
                    if(!plan||!VerifyNativeScienceCoinbase(*plan,block,0))return held("native coinbase-only payout mismatch");
                }
                if(level>=4&&!skipped_memory){
                    // Both views began at the same base and consumed the same
                    // authenticated inverse steps. Check their common parent
                    // before using the already verified native proof seal.
                    if(coins.GetBestBlock()!=metadata.GetBestBlock()||
                       coins.GetPgnFactHead()!=metadata.GetPgnFactHead()||
                       coins.GetPgnIssuanceState()!=metadata.GetPgnIssuanceState())return held("native reconnect parent views differ");
                    BlockValidationState state;
                    const bool valid=checked->science?
                        chainstate.ConnectScientificCandidateBlock(block,state,index,coins,*checked->science,true):
                        chainstate.ConnectBootstrapCandidateBlock(block,state,index,coins,*checked->bootstrap,true);
                    if(!valid)return held("native reconnect body/input scripts/actual fees rejected");
                    // fJustCheck performs scripts and actual-fee checks, with
                    // no persistent undo/index writes, as in the original path.
                    coins.SetBestBlock(index->GetBlockHash());
                    coins.StagePgnIssuanceHistoryStep(*checked->money);
                    coins.StagePgnFactHistoryStep(*checked->facts);
                }
                metadata.SetBestBlock(index->GetBlockHash());
                metadata.StagePgnIssuanceHistoryStep(*checked->money);
                metadata.StagePgnFactHistoryStep(*checked->facts);
                parent=index;
                if(chainstate.m_chainman.m_interrupt)return VerifyDBResult::INTERRUPTED;
            }
            if(metadata.GetBestBlock()!=tip->GetBlockHash()||metadata.GetPgnFactHead()!=original_head||
               metadata.GetPgnIssuanceState()!=original_money)return held("native proof replay terminal state differs");
            if(level>=4&&!skipped_memory&&(coins.GetBestBlock()!=tip->GetBlockHash()||coins.GetPgnFactHead()!=original_head||
               coins.GetPgnIssuanceState()!=original_money))return held("native reconnect terminal state differs");
        }
        // Neither memory view is flushed. Authoritative UTXO/fact/money state
        // and active chain remain untouched; only own derived archive may fill.
        if(base.GetBestBlock()!=tip->GetBlockHash()||base.GetPgnFactHead()!=original_head||
           base.GetPgnIssuanceState()!=original_money)return held("native recheck unexpectedly changed authoritative state");
        notifications.progress(_("Verifying blocks…"),100,false);
        LogDebug(BCLog::VALIDATION,"Native recheck immutable reads=%u reused=%u bytes=%u\n",reads.Reads(),reads.Hits(),reads.MemoryUsage());
        return skipped_memory?VerifyDBResult::SKIPPED_L3_CHECKS:VerifyDBResult::SUCCESS;
    }catch(const std::exception& error){
        LogError("Native PGN database recheck exception: %s",error.what());
        return VerifyDBResult::CORRUPTED_BLOCK_DB;
    }
}
}


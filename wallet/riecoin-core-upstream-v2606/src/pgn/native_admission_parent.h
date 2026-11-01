// Copyright (c) 2026 The PGN developers
// MIT. Own active-ancestor admission state, never a published reorganization.
#pragma once
#include <validation.h>
#include <pgn/native_db_recheck.h>
#include <vector>
#include <node/blockstorage.h>
#include <pgn/fact_disconnect.h>
#include <pgn/issuance_disconnect.h>
#include <memory>
#include <string>
namespace pgn::core {
inline std::unique_ptr<CCoinsViewCache> BuildNativeActiveAncestorView(
    Chainstate& active,const CBlockIndex& parent,uint32_t maximum_blocks,std::string& failure)
{
    AssertLockHeld(cs_main);
    try{
        const auto* tip=active.m_chain.Tip();
        if(!tip||!active.m_chain.Contains(parent)||!parent.IsValid(BLOCK_VALID_SCRIPTS)){
            failure="pgn-native-admission-parent-not-active-ancestor";return {};
        }
        const auto original_tip=active.CoinsTip().GetBestBlock();
        const auto original_facts=active.CoinsTip().GetPgnFactHead();
        const auto original_money=active.CoinsTip().GetPgnIssuanceState();
        if(original_tip!=tip->GetBlockHash()||!original_facts||original_facts->tip!=original_tip){
            failure="pgn-native-admission-active-head-mismatch";return {};
        }
        auto view=std::make_unique<CCoinsViewCache>(&active.CoinsTip());uint32_t count{};
        for(const auto* index=tip;index!=&parent;index=index->pprev){
            if(!index||!index->pprev||count++>=maximum_blocks){
                failure="pgn-native-admission-ancestor-resource-held";return {};
            }
            const auto money=PrepareLocalIssuanceRewind(*view,*index);
            const auto facts=PrepareLocalFactRewind(*view,*index);
            if(!facts){failure="pgn-native-admission-ancestor-fact-history-held";return {};}
            CBlock block;
            if(!active.m_blockman.ReadBlock(block,*index)||active.DisconnectBlock(block,index,*view)!=DISCONNECT_OK){
                failure="pgn-native-admission-ancestor-native-undo-held";return {};
            }
            if(money)view->StagePgnIssuanceHistoryStep(*money);
            view->StagePgnFactHistoryStep(*facts);
        }
        if(view->GetBestBlock()!=parent.GetBlockHash()||view->GetPgnFactHead()->tip!=parent.GetBlockHash()||
           active.CoinsTip().GetBestBlock()!=original_tip||active.CoinsTip().GetPgnFactHead()!=original_facts||
           active.CoinsTip().GetPgnIssuanceState()!=original_money){
            failure="pgn-native-admission-ancestor-terminal-mismatch";return {};
        }
        // No Flush. Coins, fact and money state live only in this owned view.
        return view;
    }catch(const std::exception& error){failure=std::string{"pgn-native-admission-ancestor: "}+error.what();return {};}
}

struct NativeAdmissionParent {
    std::unique_ptr<CCoinsViewCache> coins;
    std::vector<std::unique_ptr<CBlockIndex>> prefix;
    const CBlockIndex* parent{};
};
inline std::unique_ptr<NativeAdmissionParent> BuildNativeAdmissionParent(
    Chainstate& active,const CBlockIndex& requested,const NativeBlockScienceBudget& science,
    const NativeBlockBootstrapBudget* bootstrap,NativeRequestHistoryArchive& archive,std::string& failure)
{
    AssertLockHeld(cs_main);
    try{
        auto result=std::make_unique<NativeAdmissionParent>();
        if(active.m_chain.Contains(requested)){
            result->coins=BuildNativeActiveAncestorView(active,requested,science.schedule.maximum_blocks,failure);
            if(!result->coins)return {};
            result->parent=&requested;return result;
        }
        if(!bootstrap){failure="pgn-native-side-prefix-complete-profile-required";return {};}
        const auto* fork=active.m_chain.FindFork(requested);
        if(!fork){failure="pgn-native-side-prefix-common-ancestor-unavailable";return {};}
        std::vector<const CBlockIndex*> reverse;
        const auto* cursor=&requested;
        uint64_t count=uint64_t(active.m_chain.Height()-fork->nHeight);
        while(cursor!=fork){
            if(!cursor||!cursor->pprev||count++>=science.schedule.maximum_blocks||
               !cursor->IsValid(BLOCK_VALID_TRANSACTIONS)||!(cursor->nStatus&BLOCK_HAVE_DATA)){
                failure="pgn-native-side-prefix-data-or-resource-held";return {};
            }
            reverse.push_back(cursor);cursor=cursor->pprev;
        }
        result->coins=BuildNativeActiveAncestorView(active,*fork,science.schedule.maximum_blocks,failure);
        if(!result->coins)return {};
        const CBlockIndex* parent=fork;
        for(auto it=reverse.rbegin();it!=reverse.rend();++it){
            const auto* stored=*it;CBlock block;
            if(!active.m_blockman.ReadBlock(block,*stored)||block.hashPrevBlock!=parent->GetBlockHash()){
                failure="pgn-native-side-prefix-body-unavailable";return {};
            }
            auto checked_index=std::make_unique<CBlockIndex>(block);
            checked_index->phashBlock=stored->phashBlock;checked_index->pprev=const_cast<CBlockIndex*>(parent);
            checked_index->nHeight=stored->nHeight;checked_index->nTx=stored->nTx;
            checked_index->nFile=stored->nFile;checked_index->nDataPos=stored->nDataPos;checked_index->nUndoPos=stored->nUndoPos;
            checked_index->nChainWork=stored->nChainWork;checked_index->m_chain_tx_count=stored->m_chain_tx_count;
            checked_index->nStatus=(stored->nStatus&~BLOCK_VALID_MASK)|BLOCK_VALID_TRANSACTIONS;checked_index->BuildSkip();
            if(int64_t(parent->nHeight)+1!=checked_index->nHeight){failure="pgn-native-side-prefix-height-mismatch";return {};}
            const auto prepared=PrepareNativeBlockForwardAtParent(*result->coins,*checked_index,block,archive,science,*bootstrap);
            if(!prepared){failure="pgn-native-side-prefix-own-proof-or-money-held";return {};}
            const auto own_work=ResolvePinnedNativeWork(active.m_chainman.GetParams().GenesisBlock(),active.m_chainman.GetConsensus().hashGenesisBlock,
                science.policy,*active.m_chainman.m_options.pgn_candidate_fixed_block_weight);
            if(!own_work){failure="pgn-native-side-prefix-work-policy-held";return {};}
            const auto unit=QuoteConfiguredNativeHeaderWork(active.m_chainman.GetParams().GenesisBlock(),active.m_chainman.GetConsensus().hashGenesisBlock,
                science.policy,*active.m_chainman.m_options.pgn_candidate_fixed_block_weight,block.nNonce,block.nBits);
            if(!unit){failure="pgn-native-side-prefix-adaptive-work-held";return {};}
            const auto weight=parent->nChainWork+*unit;
            if(weight<=parent->nChainWork||stored->nChainWork!=weight){failure="pgn-native-side-prefix-work-mismatch";return {};}
            CCoinsViewCache child{result->coins.get()};BlockValidationState state;
            const bool valid=prepared->science?
                active.ConnectScientificCandidateBlock(block,state,checked_index.get(),child,*prepared->science,true):
                active.ConnectBootstrapCandidateBlock(block,state,checked_index.get(),child,*prepared->bootstrap,true);
            if(!valid){failure="pgn-native-side-prefix-body: "+state.ToString();return {};}
            child.SetBestBlock(checked_index->GetBlockHash());
            child.StagePgnIssuanceHistoryStep(*prepared->money);
            child.StagePgnFactHistoryStep(*prepared->facts);
            child.Flush(false); // Merge ONLY into owned disposable view.
            // This SCOPED index copy has now actually passed native proof,
            // money, input scripts and UTXO checks. NEVER raise the persistent
            // index here; activation will independently validate it again.
            checked_index->RaiseValidity(BLOCK_VALID_SCRIPTS);
            const auto cached=archive.StoreBlock(*result->coins,*checked_index,checked_index->nHeight,block,science.schedule.archive);
            if(cached!=RequestArchiveVerdict::Stored&&cached!=RequestArchiveVerdict::AlreadyStored){
                failure="pgn-native-side-prefix-derived-history-held";return {};
            }
            parent=checked_index.get();result->prefix.push_back(std::move(checked_index));
        }
        result->parent=parent;
        if(result->coins->GetBestBlock()!=requested.GetBlockHash()){
            failure="pgn-native-side-prefix-terminal-mismatch";return {};
        }
        return result;
    }catch(const std::exception& error){failure=std::string{"pgn-native-side-prefix: "}+error.what();return {};}
}


}

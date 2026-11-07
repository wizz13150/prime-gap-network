// Copyright (c) 2026 The PGN developers. MIT.
// Isolated candidate V1. Existing chains have no genesis opt-in and retain V4.
#pragma once
#include <pgn/native_report_audit.h>
#include <pgn/chain_monetary_policy.h>
#include <pgn/audit_settlement_state.h>
#include <pgn/native_zero_issuance.h>
#include <consensus/merkle.h>
namespace pgn::core {
inline constexpr std::string_view AuditSettlementPageTag{"PGN-AUDIT-PAGE-CANDIDATE-1"};
struct NativeAuditSettlementContext {
    const NativeRequestHistoryArchive& history;
    const RangePolicyReadBudget& policy;
    const RangeScheduleBudget& schedule;
    ScriptWorkBudget signature;
};
struct AuditSettlementPage {
    std::vector<CTxOut> outputs;
    CAmount paid{};
    uint64_t first{},count{};
    std::optional<CScript> receipt;
    std::optional<CScript> activation;
};
inline bool VerifyAuditSettlementOutputs(const AuditSettlementPage& page,const CTransaction& coinbase,const CScript& winner,CAmount winner_amount){
    if(!coinbase.IsCoinBase()||winner_amount<0)return false;
    std::vector<CTxOut> wanted;if(winner_amount)wanted.emplace_back(winner_amount,winner);
    wanted.insert(wanted.end(),page.outputs.begin(),page.outputs.end());
    size_t paid{};bool receipt{},activation{};
    for(const auto& output:coinbase.vout){
        if(output.nValue<0)return false;
        auto at=output.scriptPubKey.begin();opcodetype op;Bytes tag;
        const bool is_page=output.scriptPubKey.GetOp(at,op)&&op==OP_RETURN&&output.scriptPubKey.GetOp(at,op,tag)&&tag==Bytes{AuditSettlementPageTag.begin(),AuditSettlementPageTag.end()};
        at=output.scriptPubKey.begin();tag.clear();
        const bool is_activation=output.scriptPubKey.GetOp(at,op)&&op==OP_RETURN&&output.scriptPubKey.GetOp(at,op,tag)&&tag==Bytes{AuditSettlementActivationTag.begin(),AuditSettlementActivationTag.end()};
        if(is_activation){
            if(activation||output.nValue||!page.activation||output.scriptPubKey!=*page.activation)return false;
            activation=true;
        }
        if(is_page){
            if(receipt||output.nValue||!page.receipt||output.scriptPubKey!=*page.receipt)return false;
            receipt=true;
        }
        if(!output.nValue)continue;
        if(paid>=wanted.size()||output!=wanted[paid++])return false;
    }
    return paid==wanted.size()&&receipt==page.receipt.has_value()&&activation==page.activation.has_value();
}
inline bool IsAuditSettlementPage(const CScript& script){
    auto at=script.begin();opcodetype op;Bytes tag;
    return script.GetOp(at,op)&&op==OP_RETURN&&script.GetOp(at,op,tag)&&tag==Bytes{AuditSettlementPageTag.begin(),AuditSettlementPageTag.end()};
}
inline CScript AuditSettlementReceipt(const AuditSettlementActive& active,uint64_t first,uint64_t count){
    detail::Writer out{{},EncodingLimits{1,256,32,128}};out.byte(1);out.byte(1);
    out.raw({active.cutoff.begin(),32});out.raw({active.root.begin(),32});
    for(const auto v:{active.golds,active.tasks,first,count,uint64_t(active.pot)})out.little(v,8);
    return CScript{}<<OP_RETURN<<Bytes{AuditSettlementPageTag.begin(),AuditSettlementPageTag.end()}<<out.data;
}
// Reconstruct the complete retained pre-activation issuance history and verify
// every legacy rule transition. Coinbase bodies are reauthenticated against
// active-ancestry Merkle roots, rather than trusted from a local payout counter.
inline bool VerifyNativeLegacyAuditUnpaid(const CCoinsView& view,const CBlockIndex& parent,
    const ChainMonetaryPolicy& policy,const NativeAuditSettlementContext& context){
    if(!policy.Activation()||!policy.Epochs()||int64_t(parent.nHeight)+1!=policy.Activation()->height||
       !NativeMonetaryPolicyOnBranch(view,parent,policy)||parent.nHeight<0||uint64_t(parent.nHeight)+1>context.schedule.maximum_blocks)return false;
    auto cursor=view.GetPgnIssuanceState();if(!cursor||cursor->audit_settlement)return false;
    for(const auto* index=&parent;index&&index->pprev;index=index->pprev){
        const auto step=view.GetPgnIssuanceForward(index->GetBlockHash());
        if(index->nTx!=1||!step||!step->Valid()||!step->before||step->after!=*cursor||
           step->after.tip!=index->GetBlockHash()||step->before->tip!=index->pprev->GetBlockHash()||
           !VerifyLegacyAuditUnpaidStep(*policy.Epochs(),*step->before,step->after,index->nHeight))return false;
        const auto loaded=context.history.Load(view,parent,index->nHeight,context.schedule.archive);if(!loaded.block)return false;
        const bool gold=step->after.epochs!=step->before->epochs;
        size_t positive{};CAmount amount{};
        for(const auto& output:loaded.block->Coinbase()->vout){
            if(output.nValue<0||IsAuditSettlementPage(output.scriptPubKey))return false;
            if(output.nValue){++positive;amount=output.nValue;}
        }
        if(gold){const auto q=QuoteGoldEpoch(*policy.Epochs(),step->before->epochs?step->before->epochs->golds:0);
            if(!q||positive>1||(q->gold&&positive!=1)||amount!=q->gold)return false;
        }else if(positive)return false;
        cursor=step->before;
    }
    const auto* genesis=parent.GetAncestor(0);
    if(!genesis||!cursor||cursor->tip!=genesis->GetBlockHash()||cursor->minted||cursor->promised||
       !cursor->partition||cursor->partition->policy!=policy.Id()||!cursor->pools||
       cursor->pools->audit||cursor->pools->near_miss||cursor->epochs||cursor->audit_settlement)return false;
    const IssuanceSnapshot initial{genesis->GetBlockHash(),PGN_MAX_SUPPLY_ATOMIC,0,0,
        IssuanceBudgetPartition{policy.Id(),policy.Definition().ordinary_cap,0,0,0,0},OrdinaryRewardPools{}};
    return cursor==initial&&view.GetPgnIssuanceHistoryAnchor()==cursor;
}
// Scan only; O(number of replayed reports), O(page size) additional storage.
// Production caller MUST obtain ledger from ReadNativeAuditLedger, never a DTO.
template<typename Visitor> inline void VisitAuditSettlementTasks(const NativeAuditLedger& ledger,int32_t after,int32_t cutoff,Visitor visit){
    for(const auto& [id,report]:ledger)for(uint8_t slot=0;slot<2;++slot){
        const auto& job=report.jobs[slot];
        if(!job||job->stage!=NativeAuditJob::Passed||job->passed_height<=after||job->passed_height>cutoff)continue;
        RequireAudit(report.details_root.has_value()&&!job->reveal.empty()&&!job->actor.empty()&&!job->actor.IsUnspendable(),"Unpayable passed receipt");
        visit(id,report.report.commitment.root,slot,*job);
    }
}
inline std::pair<uint64_t,uint256> AuditSettlementTaskCommitment(const NativeAuditLedger& ledger,int32_t after,int32_t cutoff){
    HashWriter hash{TaggedHash("PGN/AUDIT-SETTLEMENT/TASKS/1")};hash<<after<<cutoff;uint64_t tasks{};
    VisitAuditSettlementTasks(ledger,after,cutoff,[&](const uint256& id,const uint256& root,uint8_t slot,const NativeAuditJob& job){
        RequireAudit(tasks<std::numeric_limits<uint64_t>::max(),"Audit task count exhausted");++tasks;
        hash<<id<<root<<slot<<job.attempt<<job.actor<<job.passed_height;
    });
    return {tasks,hash.GetHash()};
}
// Financial kernel exercised separately from authentication. No mint authority.
inline std::optional<AuditSettlementPage> ApplyAuditSettlementPage(
    const NativeAuditLedger& ledger,const GoldEpochDefinition& definition,uint16_t maximum_outputs,
    uint64_t cutoff_golds,int32_t cutoff_height,const uint256& cutoff,IssuanceSnapshot& after){
    if(!maximum_outputs||maximum_outputs>128||!GoldEpochDefinitionValid(definition)||!after.Valid()||!after.audit_settlement||!after.epochs||!after.partition||!after.pools)return {};
    auto next=after;auto& state=*next.audit_settlement;
    if(state.completed_golds%definition.audit_period||state.completed_golds>next.epochs->golds||state.completed_height>next.epochs->last_gold_height)return {};
    if(state.active&&(state.active->golds%definition.audit_period||state.active->golds>next.epochs->golds||state.active->height>next.epochs->last_gold_height))return {};
    const auto accrued=QuoteCumulativeAuditAccrual(definition,next.epochs->golds);
    if(!accrued||state.total_paid>*accrued||next.pools->audit!=*accrued-state.total_paid)return {};
    AuditSettlementPage page;
    if(!state.active){
        if(cutoff_golds<=state.completed_golds)return page;
        if(cutoff_golds>next.epochs->golds||cutoff_golds%definition.audit_period||cutoff_height<=state.completed_height||cutoff.IsNull())return {};
        const auto [tasks,root]=AuditSettlementTaskCommitment(ledger,state.completed_height,cutoff_height);
        if(!tasks){state.completed_golds=cutoff_golds;state.completed_height=cutoff_height;after=next;return page;}
        const auto reserved=QuoteCumulativeAuditAccrual(definition,cutoff_golds);
        if(!reserved||*reserved<state.total_paid||*reserved-state.total_paid>next.pools->audit)return {};
        state.active=AuditSettlementActive{cutoff_golds,tasks,0,cutoff_height,cutoff,root,*reserved-state.total_paid};
    }
    auto& active=*state.active;
    if(AuditSettlementTaskCommitment(ledger,state.completed_height,active.height)!=std::pair{active.tasks,active.root})return {};
    page.first=active.next;page.count=std::min<uint64_t>(maximum_outputs,active.tasks-active.next);
    const auto amount=QuoteAuditPotRange(active.pot,active.tasks,page.first,page.count);if(!amount)return {};
    page.paid=*amount;page.receipt=AuditSettlementReceipt(active,page.first,page.count);
    uint64_t ordinal{};
    VisitAuditSettlementTasks(ledger,state.completed_height,active.height,[&](const uint256&,const uint256&,uint8_t,const NativeAuditJob& job){
        if(ordinal>=page.first&&ordinal-page.first<page.count){
            const auto value=*QuoteAuditPotRange(active.pot,active.tasks,ordinal,1);
            if(value)page.outputs.emplace_back(value,job.actor);
        }
        ++ordinal;
    });
    const auto total=IssuanceBudgetState::Create(next.cap,next.minted,next.promised);
    const auto ordinary=IssuanceBudgetState::Create(next.partition->ordinary_cap,next.partition->ordinary_minted,next.partition->ordinary_promised);
    if(!total||!ordinary)return {};
    const auto minted=total->MintReserved(page.paid),minted_ordinary=ordinary->MintReserved(page.paid);
    if(!minted||!minted_ordinary||page.paid>next.pools->audit)return {};
    next.minted=minted->CumulativeMinted();next.promised=minted->Promised();
    next.partition->ordinary_minted=minted_ordinary->CumulativeMinted();next.partition->ordinary_promised=minted_ordinary->Promised();
    next.pools->audit-=page.paid;state.total_paid+=page.paid;active.next+=page.count;
    if(active.next==active.tasks){state.completed_golds=active.golds;state.completed_height=active.height;state.active.reset();}
    if(!next.Valid())return {};
    after=std::move(next);return page;
}
inline std::optional<AuditSettlementPage> PrepareNativeAuditSettlement(
    const CCoinsView& view,const CBlockIndex& parent,const ChainMonetaryPolicy& policy,
    const NativeAuditSettlementContext* context,IssuanceSnapshot& after){
    if(!policy.Audits())return after.audit_settlement?std::nullopt:std::optional{AuditSettlementPage{}};
    const auto before=view.GetPgnIssuanceState();
    if(!context||!before||!policy.Epochs())return {};
    if(!before->audit_settlement){
        if(!policy.Activation()||!VerifyNativeLegacyAuditUnpaid(view,parent,policy,*context)||
           !InitializeAuditSettlementActivation(*policy.Activation(),*before,parent.nHeight+1,after))return {};
        // H is a dedicated zero-issuance coordination block. Settlement starts
        // only after its exact no-value migration has committed atomically.
        AuditSettlementPage page;page.activation=AuditSettlementActivationReceipt(*policy.Activation());return page;
    }
    if(!before->epochs)return {};
    const auto ledger=ReadNativeAuditLedger(view,parent,context->history,context->policy,context->schedule,context->signature);
    if(!ledger)return {};
    if(const auto& active=before->audit_settlement->active;active){
        const auto* anchor=parent.GetAncestor(active->height);
        if(!anchor||anchor->GetBlockHash()!=active->cutoff)return {};
        const auto retained=view.GetPgnIssuanceForward(active->cutoff);
        if(!retained||!retained->Valid()||!retained->after.epochs||retained->after.epochs->golds!=active->golds)return {};
    }
    uint64_t cutoff_golds=before->epochs->golds/policy.Epochs()->audit_period*policy.Epochs()->audit_period;
    int32_t cutoff_height{};uint256 cutoff;
    if(!before->audit_settlement->active&&cutoff_golds>before->audit_settlement->completed_golds){
        for(const CBlockIndex* index=&parent;index&&index->pprev;index=index->pprev){
            const auto step=view.GetPgnIssuanceForward(index->GetBlockHash());
            if(!step||!step->Valid()||!step->before||!step->after.epochs||!step->before->epochs||
               step->after.tip!=index->GetBlockHash()||step->before->tip!=index->pprev->GetBlockHash())return {};
            if(step->after.epochs->golds==cutoff_golds&&step->before->epochs->golds<cutoff_golds){cutoff_height=index->nHeight;cutoff=index->GetBlockHash();break;}
        }
        if(!cutoff_height)return {};
    }
    return ApplyAuditSettlementPage(*ledger,*policy.Epochs(),policy.Audits()->page_outputs,cutoff_golds,cutoff_height,cutoff,after);
}
struct NativeAuditCoordinationPlan {IssuanceHistoryStep forward;AuditSettlementPage page;};
// Settlement consumes already-promised audit reserves. It grants neither a Gold
// nor a subsidy and preserves scientific fact/target counters exactly.
inline std::optional<NativeAuditCoordinationPlan> PrepareNativeAuditCoordinationPlan(
    const CCoinsView& view,const CBlockIndex& parent,const CBlock& block,const ChainMonetaryPolicy& policy,
    const NativeAuditSettlementContext& context){
    if(!NativeMonetaryPolicyOnBranch(view,parent,policy)||block.hashPrevBlock!=parent.GetBlockHash()||
       BlockMerkleRoot(block)!=block.hashMerkleRoot||block.vtx.size()!=1||!block.vtx.front()||!block.vtx.front()->IsCoinBase())return {};
    const auto before=view.GetPgnIssuanceState();
    if(!before||!before->Valid()||!before->partition||before->partition->policy!=policy.Id()||
       before->partition->ordinary_cap!=policy.Definition().ordinary_cap)return {};
    auto after=*before;after.tip=block.GetHash();
    const auto page=PrepareNativeAuditSettlement(view,parent,policy,&context,after);if(!page)return {};
    IssuanceHistoryStep forward{IssuanceDirection::Forward,{before,after}};
    bool epochs_preserved=after.epochs==before->epochs;
    if(!epochs_preserved&&page->activation&&policy.Activation()){
        // The sole allowed coordination change to epoch counters is the exact
        // authenticated migration from absent v3 counters to explicit zero.
        // Reconstruct the entire snapshot, rather than waiving the invariant.
        auto migration=*before;migration.tip=after.tip;
        epochs_preserved=InitializeAuditSettlementActivation(*policy.Activation(),*before,parent.nHeight+1,migration)&&migration==after;
    }
    if(!forward.Valid()||after.minted-before->minted!=page->paid||before->promised-after.promised!=page->paid||!epochs_preserved)return {};
    return NativeAuditCoordinationPlan{std::move(forward),*page};
}
}

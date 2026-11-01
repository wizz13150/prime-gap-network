// Copyright (c) 2026 The PGN developers. MIT.
// A signed report commitment is evidence availability, never audited coverage.
#pragma once
#include <pgn/candidate_range_schedule.h>
#include <pgn/canonical_encoding.h>
#include <pgn/script_work_proof.h>
#include <hash.h>
#include <chain.h>
#include <coins.h>
#include <algorithm>
#include <primitives/block.h>
#include <map>
namespace pgn::core {
inline constexpr std::string_view NativeReportTag{"PGNREPORT-COMMIT-1"};
struct NativeReportCommitment {
    uint256 request,ticket,root,parent;
    Bytes signature;
};
struct ConfirmedNativeReport {
    NativeReportCommitment commitment;
    uint256 block;
    int32_t height;
};
inline bool IsNativeReportScript(const CScript& script){
    auto it=script.begin();opcodetype op;Bytes tag;
    return script.GetOp(it,op)&&op==OP_RETURN&&script.GetOp(it,op,tag)&&
        tag==Bytes{NativeReportTag.begin(),NativeReportTag.end()};
}
inline std::optional<CScript> BuildNativeReportEnvelope(const NativeReportCommitment& report){
    if(report.request.IsNull()||report.ticket.IsNull()||report.root.IsNull()||report.parent.IsNull()||
       report.signature.empty()||report.signature.size()>4096)return {};
    Bytes payload{1};
    for(const auto* value:{&report.request,&report.ticket,&report.root,&report.parent})
        payload.insert(payload.end(),value->begin(),value->end());
    EncodingLimits limits{1,8192,32,100};detail::Writer w{std::move(payload),limits};
    w.count(report.signature.size());w.raw(report.signature);
    return CScript{}<<OP_RETURN<<Bytes{NativeReportTag.begin(),NativeReportTag.end()}<<w.data;
}
inline std::optional<NativeReportCommitment> DecodeNativeReportEnvelope(const CScript& script){
    if(script.size()>8192||!IsNativeReportScript(script))return {};
    try{
        auto it=script.begin();opcodetype op;Bytes tag,payload;
        if(!script.GetOp(it,op)||!script.GetOp(it,op,tag)||!script.GetOp(it,op,payload)||it!=script.end()||
           script!=(CScript{}<<OP_RETURN<<tag<<payload))return {};
        EncodingLimits limits{1,8192,32,100};detail::Reader reader{payload,limits};
        if(reader.byte()!=1)return {};
        NativeReportCommitment report;
        for(auto* value:{&report.request,&report.ticket,&report.root,&report.parent}){
            const auto raw=reader.digest();std::copy(raw.begin(),raw.end(),value->begin());
        }
        const auto bytes=reader.count();if(bytes==0||bytes>4096||bytes!=payload.size()-reader.position)return {};
        report.signature.assign(payload.begin()+reader.position,payload.end());
        const auto canonical=BuildNativeReportEnvelope(report);
        return canonical&&*canonical==script?std::optional{std::move(report)}:std::nullopt;
    }catch(const std::exception&){return {};}
}
inline uint256 NativeReportWorkIdentity(const uint256& chain,const NativeReportCommitment& report){
    HashWriter hash{TaggedHash("PGN/REPORT-COMMITMENT/1")};
    hash<<chain<<report.request<<report.ticket<<report.root;
    return hash.GetHash();
}
// Full current-branch authentication is inherited from the native archive.
// Missing history fails closed; a restart never substitutes an empty registry.
inline std::optional<std::map<uint256,ConfirmedNativeReport>> ReadConfirmedNativeReports(
    const CCoinsView& view,const CBlockIndex& parent,const NativeRequestHistoryArchive& history,
    const RangeScheduleBudget& budget)
{
    AssertLockHeld(cs_main);
    const auto head=view.GetPgnFactHead();
    if(!head||!head->Valid()||!parent.phashBlock||parent.nHeight<0||
       !parent.IsValid(BLOCK_VALID_SCRIPTS)||head->tip!=parent.GetBlockHash()||
       view.GetBestBlock()!=parent.GetBlockHash()||!budget.maximum_blocks||!budget.maximum_requests)return {};
    struct Snapshot {
        uint256 chain,tip;int32_t height{-1};
        std::map<uint256,ConfirmedNativeReport> reports;
    };
    static Snapshot cached;
    const auto* anchor=cached.height>=0&&cached.height<=parent.nHeight?parent.GetAncestor(cached.height):nullptr;
    if(cached.chain!=head->chain||(cached.height>=0&&
       (!anchor||!anchor->phashBlock||!anchor->IsValid(BLOCK_VALID_SCRIPTS)||anchor->GetBlockHash()!=cached.tip)))
        cached=Snapshot{};
    cached.chain=head->chain;
    if(cached.reports.size()>budget.maximum_requests)return {};
    const auto reject=[&](){cached=Snapshot{};return std::optional<std::map<uint256,ConfirmedNativeReport>>{};};
    try{
        while(cached.height<parent.nHeight){
            const int64_t first=int64_t(cached.height)+1;
            const int64_t last=std::min<int64_t>(parent.nHeight,first+budget.maximum_blocks-1);
            std::vector<const CBlockIndex*> ancestry(size_t(last-first+1));
            auto* index=parent.GetAncestor(int32_t(last));
            for(int64_t height=last;height>=first;--height){
                if(!index||!index->phashBlock||index->nHeight!=height||!index->IsValid(BLOCK_VALID_SCRIPTS))return reject();
                ancestry[size_t(height-first)]=index;index=index->pprev;
            }
            if(first&&(!index||!index->phashBlock||index->GetBlockHash()!=cached.tip))return reject();
            for(const auto* block:ancestry){
                const auto loaded=history.Load(view,parent,block->nHeight,budget.archive);
                if(!loaded.block||!loaded.block->Coinbase())return reject();
                std::map<uint256,ConfirmedNativeReport> additions;
                for(const auto& output:loaded.block->Coinbase()->vout){
                    if(!IsNativeReportScript(output.scriptPubKey))continue;
                    auto report=DecodeNativeReportEnvelope(output.scriptPubKey);
                    if(!report||output.nValue!=0||!block->pprev||report->parent!=block->pprev->GetBlockHash())return reject();
                    const auto known=cached.reports.find(report->request);
                    if(known!=cached.reports.end()){
                        if(known->second.commitment.root!=report->root||known->second.commitment.ticket!=report->ticket)return reject();
                        continue;
                    }
                    const auto pending=additions.find(report->request);
                    if(pending!=additions.end()){
                        if(pending->second.commitment.root!=report->root||pending->second.commitment.ticket!=report->ticket)return reject();
                        continue;
                    }
                    if(additions.size()>=budget.maximum_requests-cached.reports.size())return reject();
                    const auto request=report->request;
                    additions.emplace(request,ConfirmedNativeReport{std::move(*report),block->GetBlockHash(),block->nHeight});
                }
                // Complete one verified block before publishing its prefix.
                cached.reports.merge(additions);cached.height=block->nHeight;cached.tip=block->GetBlockHash();
            }
        }
        return cached.reports;
    }catch(...){cached=Snapshot{};throw;}
}
inline bool VerifyProposedNativeReportCommitments(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& history,const CBlock& block,const RangePolicyReadBudget& policy_budget,
    const RangeScheduleBudget& schedule_budget,ScriptWorkBudget signature_budget){
    bool has_report{};
    for(const auto& output:block.vtx.front()->vout)has_report|=IsNativeReportScript(output.scriptPubKey);
    if(!has_report)return true;
    const auto policy=ReadChainRangePolicy(view,parent,history,schedule_budget.archive,policy_budget);
    auto known=ReadConfirmedNativeReports(view,parent,history,schedule_budget);
    if(!policy.policy||!known)return false;
    size_t count{};
    for(const auto& output:block.vtx.front()->vout){
        if(!IsNativeReportScript(output.scriptPubKey))continue;
        if(++count>256||output.nValue!=0)return false;
        const auto report=DecodeNativeReportEnvelope(output.scriptPubKey);
        if(!report||report->parent!=parent.GetBlockHash())return false;
        const auto reservation=DeriveNativeCandidateReservation(view,parent,*policy.policy,history,schedule_budget,report->request);
        if(!reservation.reservation||report->ticket!=reservation.reservation->Ticket())return false;
        const auto& owner=reservation.reservation->Lease().owner.Request().miner;
        const auto identity=NativeReportWorkIdentity(policy.policy->Chain(),*report);
        if(!VerifyScriptWork({policy.policy->Chain(),identity,parent.GetBlockHash()},owner,report->signature,signature_budget).proof)return false;
        const auto old=known->find(report->request);
        if(old!=known->end()&&(old->second.commitment.root!=report->root||old->second.commitment.ticket!=report->ticket))return false;
        known->insert_or_assign(report->request,ConfirmedNativeReport{*report,block.GetHash(),parent.nHeight+1});
    }
    return true;
}
}

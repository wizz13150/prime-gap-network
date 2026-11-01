// Copyright (c) 2026 The PGN developers. MIT.
// Blind audit events: commitment precedes every answer disclosure.
#pragma once
#include <pgn/native_report_commitment.h>
#include <pgn/audit_lease_clock.h>
#include <crypto/sha256.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <array>
#include <set>
namespace pgn::core {
inline constexpr std::string_view NativeAuditTag{"PGNAUDIT-EVENT-1"};
inline constexpr int32_t NativeAuditLeaseBlocks=12; // Replay of legacy empty-body assignments only.
inline constexpr int64_t NativeAuditLeaseSeconds=24*60*60;
inline Bytes NativeTimedAuditAssignmentBody(){
 const std::string text="{\"lease_seconds\":"+std::to_string(NativeAuditLeaseSeconds)+",\"schema\":\"PGN_AUDIT_ASSIGNMENT_V2\"}";
 return Bytes{text.begin(),text.end()};
}
enum class NativeAuditAction:uint8_t {Assign,Commit,Reveal,ReportReveal,Enroll};
struct NativeAuditEvent {
    uint256 request,parent;
    NativeAuditAction action;
    uint8_t slot{};uint32_t attempt{};
    CScript actor;Bytes body,signature;
};
inline Digest AuditSha(std::span<const uint8_t> bytes){Digest out;CSHA256{}.Write(bytes.data(),bytes.size()).Finalize(out.data());return out;}
inline void RequireAudit(bool condition,const char* message){if(!condition)throw std::invalid_argument(message);}
inline bool IsNativeAuditScript(const CScript& script){
    auto cursor=script.begin();opcodetype op;Bytes tag;
    return script.GetOp(cursor,op)&&op==OP_RETURN&&script.GetOp(cursor,op,tag)&&tag==Bytes{NativeAuditTag.begin(),NativeAuditTag.end()};
}
inline std::optional<CScript> BuildNativeAuditEnvelope(const NativeAuditEvent& event){
    try{
        RequireAudit(!event.request.IsNull()&&!event.parent.IsNull()&&uint8_t(event.action)<=4&&event.slot<2&&
            !event.actor.empty()&&event.actor.size()<=4096&&event.body.size()<=65536&&event.signature.size()<=4096,"Invalid audit fields");
        EncodingLimits limits{1,80000,32,100};detail::Writer out{{},limits};out.byte(1);
        out.raw({event.request.begin(),32});out.raw({event.parent.begin(),32});out.byte(uint8_t(event.action));out.byte(event.slot);out.little(event.attempt,4);
        const auto put=[&out](const auto& bytes){out.count(bytes.size());out.raw({bytes.data(),bytes.size()});};
        put(event.actor);put(event.body);put(event.signature);
        return CScript{}<<OP_RETURN<<Bytes{NativeAuditTag.begin(),NativeAuditTag.end()}<<out.data;
    }catch(const std::exception&){return {};}
}
inline std::optional<NativeAuditEvent> DecodeNativeAuditEnvelope(const CScript& script){
    if(script.size()>80000||!IsNativeAuditScript(script))return {};
    try{
        auto cursor=script.begin();opcodetype op;Bytes tag,payload;
        RequireAudit(script.GetOp(cursor,op)&&script.GetOp(cursor,op,tag)&&script.GetOp(cursor,op,payload)&&cursor==script.end()&&
            script==(CScript{}<<OP_RETURN<<tag<<payload),"Noncanonical audit script");
        EncodingLimits limits{1,80000,32,100};detail::Reader in{payload,limits};RequireAudit(in.byte()==1,"Unknown audit version");
        NativeAuditEvent event;
        for(auto* id:{&event.request,&event.parent}){const auto raw=in.digest();std::copy(raw.begin(),raw.end(),id->begin());}
        event.action=NativeAuditAction(in.byte());event.slot=in.byte();event.attempt=uint32_t(in.little(4));
        const auto get=[&in,&payload](size_t maximum){
            const auto size=in.count();RequireAudit(size<=maximum&&size<=payload.size()-in.position,"Oversized audit field");
            Bytes field(payload.begin()+in.position,payload.begin()+in.position+size);in.position+=size;return field;
        };
        const auto actor=get(4096);event.actor=CScript{actor.begin(),actor.end()};event.body=get(65536);event.signature=get(4096);
        RequireAudit(in.position==payload.size(),"Trailing audit bytes");const auto canonical=BuildNativeAuditEnvelope(event);
        return canonical&&*canonical==script?std::optional{std::move(event)}:std::nullopt;
    }catch(const std::exception&){return {};}
}
inline uint256 NativeAuditWorkIdentity(const uint256& chain,const uint256& report_root,const NativeAuditEvent& event){
    HashWriter hash{TaggedHash("PGN/AUDIT-EVENT/1")};hash<<chain<<report_root<<event.request<<uint8_t(event.action)<<event.slot<<event.attempt<<event.actor<<event.body;
    return hash.GetHash();
}
inline std::array<std::array<uint32_t,5>,2> NativeAuditPartitions(const uint256& root,const uint256& seed){
    const std::string prefix="PGN/audit-partitions/1";Bytes input{prefix.begin(),prefix.end()};input.push_back(0);
    const auto root_bytes=ParseHex(root.GetHex()),seed_bytes=ParseHex(seed.GetHex());input.insert(input.end(),root_bytes.begin(),root_bytes.end());input.insert(input.end(),seed_bytes.begin(),seed_bytes.end());
    std::vector<uint32_t> remaining(100);for(uint32_t i=0;i<100;++i)remaining[i]=i;
    std::array<std::array<uint32_t,5>,2> result;uint64_t counter{};unsigned selected{};
    const Integer space=Integer{1}<<256;
    while(selected<10){
        auto bytes=input;for(int i=7;i>=0;--i)bytes.push_back(uint8_t(counter>>(8*i)));++counter;
        const auto raw=AuditSha(bytes);Integer value;import_bits(value,raw.begin(),raw.end(),8,true);
        const Integer bound=space-space%remaining.size();if(value>=bound)continue;
        const auto position=(value%remaining.size()).convert_to<size_t>();result[selected/5][selected%5]=remaining[position];remaining.erase(remaining.begin()+position);++selected;
    }
    for(auto& group:result)std::sort(group.begin(),group.end());return result;
}
inline uint64_t AuditCount(const UniValue& value){
    RequireAudit(value.isStr(),"Count must be a decimal string");const auto& text=value.get_str();
    RequireAudit(!text.empty()&&text.size()<=20&&(text.size()==1||text[0]!='0'),"Noncanonical count");uint64_t result{};
    for(char c:text){RequireAudit(c>='0'&&c<='9'&&result<=(std::numeric_limits<uint64_t>::max()-(c-'0'))/10,"Count overflow");result=result*10+(c-'0');}return result;
}
inline Integer AuditInteger(const UniValue& value){
    RequireAudit(value.isStr(),"Position must be a decimal string");const auto& text=value.get_str();RequireAudit(!text.empty()&&text.size()<=29&&(text.size()==1||text[0]!='0'),"Noncanonical position");Integer result{};
    for(char c:text){RequireAudit(c>='0'&&c<='9',"Invalid position");result=result*10+(c-'0');}RequireAudit(result<=(Integer{1}<<95)+(Integer{1}<<32),"Position domain exceeded");return result;
}
inline void AuditKeys(const UniValue& value){
    if(value.isObject()){const auto& keys=value.getKeys();RequireAudit(std::set<std::string>(keys.begin(),keys.end()).size()==keys.size(),"Duplicate JSON key");}
    if(value.isObject()||value.isArray())for(const auto& child:value.getValues())AuditKeys(child);
}
struct NativeAuditJob {
    CScript actor;uint32_t attempt{};int32_t issued{},deadline{};
    enum Stage {Assigned,Committed,Passed,Failed} stage{Assigned};
    std::string commitment,reveal;
    std::optional<AuditAssignmentLease> time_lease;
};
inline bool NativeAuditAssignmentExpired(const NativeAuditJob& job,int32_t height,int64_t median_time){
 return job.time_lease?job.time_lease->expired(height,median_time):height>=job.deadline;
}
struct NativeReportAudit {
    ConfirmedNativeReport report;CScript owner;RangeContract range;
    std::array<std::optional<NativeAuditJob>,2> jobs;
    std::optional<std::array<uint64_t,100>> expected;
    std::optional<Digest> details_root;
    std::string owner_reveal;
    bool AllCommitted()const{return jobs[0]&&jobs[1]&&jobs[0]->stage!=NativeAuditJob::Assigned&&jobs[1]->stage!=NativeAuditJob::Assigned;}
    bool Audited()const{return jobs[0]&&jobs[1]&&jobs[0]->stage==NativeAuditJob::Passed&&jobs[1]->stage==NativeAuditJob::Passed&&details_root.has_value();}
};
struct NativeAuditMembership {uint256 operator_id;bool may_audit{};};
struct NativeAuditLedger:std::map<uint256,NativeReportAudit> {
    CScript authority;uint256 authority_request;int32_t authority_height{};
    std::map<Bytes,NativeAuditMembership> participants;
};
inline uint256 NativeAuditOperator(const NativeAuditLedger& ledger,const CScript& script){
    const auto found=ledger.participants.find(Bytes{script.begin(),script.end()});
    if(found!=ledger.participants.end())return found->second.operator_id;
    return *uint256::FromHex(HexStr(AuditSha({script.data(),script.size()})));
}
inline bool NativeAuditEligible(const NativeAuditLedger& ledger,const CScript& actor){
    const auto found=ledger.participants.find(Bytes{actor.begin(),actor.end()});return found!=ledger.participants.end()&&found->second.may_audit;
}
inline bool NativeAuditOperatorsIndependent(const NativeAuditLedger& ledger,const NativeReportAudit& report){
    if(!report.jobs[0]||!report.jobs[1])return false;
    const auto owner=NativeAuditOperator(ledger,report.owner);
    const auto first=NativeAuditOperator(ledger,report.jobs[0]->actor),second=NativeAuditOperator(ledger,report.jobs[1]->actor);
    return first!=owner&&second!=owner&&first!=second;
}
inline std::optional<std::pair<uint256,uint8_t>> SelectNativeAudit(const NativeAuditLedger& ledger,const CScript& actor,int32_t parent_height,int64_t parent_median_time){
    if(!NativeAuditEligible(ledger,actor))return {};
    std::optional<std::pair<uint256,uint8_t>> chosen;int32_t oldest=std::numeric_limits<int32_t>::max();
    for(const auto& [id,report]:ledger){
        if(NativeAuditOperator(ledger,report.owner)==NativeAuditOperator(ledger,actor)||report.report.height>=parent_height||report.Audited())continue;
        for(uint8_t slot=0;slot<2;++slot){const auto& job=report.jobs[slot];
            if(job&&(job->stage!=NativeAuditJob::Assigned||!NativeAuditAssignmentExpired(*job,parent_height,parent_median_time)))continue;
            const auto& other=report.jobs[1-slot];
            if(other&&NativeAuditOperator(ledger,other->actor)==NativeAuditOperator(ledger,actor))continue;
            if(!chosen||report.report.height<oldest){chosen=std::pair{id,slot};oldest=report.report.height;}break;
        }
    }return chosen;
}
inline void ApplyNativeAuditEvent(NativeAuditLedger& ledger,const NativeAuditEvent& event,const CBlockIndex& parent,
    const uint256& chain,ScriptWorkBudget budget){
    if(event.action==NativeAuditAction::Enroll){
        RequireAudit(!ledger.authority.empty()&&event.actor==ledger.authority&&event.request==ledger.authority_request&&event.parent==parent.GetBlockHash()&&
            parent.nHeight>=ledger.authority_height&&event.slot==0&&event.attempt==0,"Participant admission requires the testnet authority");
        const auto identity=NativeAuditWorkIdentity(chain,uint256{},event);
        RequireAudit(VerifyScriptWork({chain,identity,event.parent},event.actor,event.signature,budget).proof.has_value(),"Invalid participant authority signature");
        const std::string text{event.body.begin(),event.body.end()};UniValue body;RequireAudit(body.read(text)&&body.isObject(),"Invalid participant admission");AuditKeys(body);
        RequireAudit(body["schema"].get_str()=="PGN_TESTNET_PARTICIPANT_V1"&&body["may_audit"].isBool(),"Unknown participant policy");
        const auto operator_text=body["operator_id"].get_str(),script_text=body["signing_script"].get_str();const auto operator_id=uint256::FromHex(operator_text);
        RequireAudit(operator_id&&!operator_id->IsNull()&&operator_id->GetHex()==operator_text&&script_text.size()<=8192&&IsHex(script_text)&&HexStr(ParseHex(script_text))==script_text,"Invalid participant identity");
        const auto script=ParseHex(script_text);RequireAudit(!script.empty(),"Empty participant script");const auto previous=ledger.participants.find(script);
        if(previous!=ledger.participants.end())RequireAudit(previous->second.operator_id==*operator_id,"An enrolled key cannot change operator identity");
        RequireAudit(ledger.participants.size()<262144||previous!=ledger.participants.end(),"Local participant budget exceeded");
        ledger.participants.insert_or_assign(script,NativeAuditMembership{*operator_id,body["may_audit"].get_bool()});return;
    }
    auto found=ledger.find(event.request);RequireAudit(found!=ledger.end()&&event.parent==parent.GetBlockHash(),"Audit parent or report unavailable");auto& report=found->second;
    RequireAudit(report.report.height<parent.nHeight+1,"Audit precedes report commitment");
    const auto identity=NativeAuditWorkIdentity(chain,report.report.commitment.root,event);
    RequireAudit(VerifyScriptWork({chain,identity,event.parent},event.actor,event.signature,budget).proof.has_value(),"Invalid audit actor signature");
    if(event.action==NativeAuditAction::Assign){
        const auto selected=SelectNativeAudit(ledger,event.actor,parent.nHeight,parent.GetMedianTimePast());RequireAudit(selected&&selected->first==event.request&&selected->second==event.slot,"Audit selection is not automatic");
        auto& job=report.jobs[event.slot];const auto attempt=job?job->attempt+1:0;RequireAudit((!job||job->attempt<std::numeric_limits<uint32_t>::max())&&event.attempt==attempt,"Stale audit attempt");
        RequireAudit(parent.nHeight<std::numeric_limits<int32_t>::max()-NativeAuditLeaseBlocks-1,"Audit height exhausted");
        if(event.body.empty()){
            // Historic signed envelopes keep their original interpretation.
            job=NativeAuditJob{event.actor,event.attempt,parent.nHeight+1,parent.nHeight+1+NativeAuditLeaseBlocks};
        }else{
            RequireAudit(event.body==NativeTimedAuditAssignmentBody(),"Unknown audit assignment lease policy");
            const auto lease=auditTimeLease(parent.GetMedianTimePast(),NativeAuditLeaseSeconds);
            RequireAudit(lease.has_value(),"Audit assignment time exhausted");
            job=NativeAuditJob{event.actor,event.attempt,parent.nHeight+1,0};job->time_lease=*lease;
        }
        return;
    }
    if(event.action==NativeAuditAction::ReportReveal){
        RequireAudit(event.actor==report.owner&&event.slot==0&&event.attempt==0&&report.AllCommitted(),"Report answers cannot be revealed before both audit commitments");
        RequireAudit(NativeAuditOperatorsIndependent(ledger,report),"Report disclosure requires two independent audit operators");
        RequireAudit(HexStr(AuditSha(event.body))==report.report.commitment.root.GetHex(),"Report reveal hash mismatch");
        const std::string text{event.body.begin(),event.body.end()};UniValue body;RequireAudit(body.read(text)&&body.isObject(),"Invalid report JSON");AuditKeys(body);
        RequireAudit(body["schema"].get_str()=="PGN_NETWORK_RANGE_REPORT_V1"&&body["genesis"].get_str()==chain.GetHex()&&body["request_id"].get_str()==event.request.GetHex()&&
            body["ticket"].get_str()==HexStr(report.range.ticket_id),"Report identity mismatch");
        const auto nonce=body["commitment_nonce"].get_str();RequireAudit(nonce.size()==64&&IsHex(nonce)&&HexStr(ParseHex(nonce))==nonce,"Invalid report hiding nonce");
        const auto& computed=body["computed"];RequireAudit(computed["schema"].get_str()=="PGN_HORIZON_COMPUTED_REPORT_V1"&&
            AuditInteger(computed["start"])==report.range.start&&AuditInteger(computed["end"])==report.range.start+report.range.length&&AuditInteger(computed["positions"])==report.range.length&&computed["ticket_id"].get_str()==HexStr(report.range.ticket_id),"Wrong report coverage");
        const auto& counts=computed["counts"].get_array();RequireAudit(counts.size()==100,"One hundred report counts required");std::array<uint64_t,100> values;Integer total{};for(size_t i=0;i<100;++i){values[i]=AuditCount(counts[i]);total+=values[i];}RequireAudit(total<=report.range.length/2+1,"Report counts exceed owned positions");
        const auto& best=computed["best"].get_array();RequireAudit(best.size()<=100&&Integer{best.size()}<=total,"Too many detailed gaps");std::vector<SegmentResult> segments(100);for(uint32_t i=0;i<100;++i)segments[i].index=i;
        std::set<Integer> seen;
        for(const auto& item:best.getValues()){
            const Integer lower=AuditInteger(item["lower_prime"]),upper=AuditInteger(item["upper_prime"]);const auto gap=item["gap"].getInt<int64_t>();
            RequireAudit(gap>0&&gap<=(int64_t{1}<<32)&&gap%2==0&&lower%2==1&&lower>=report.range.start&&lower<report.range.start+report.range.length&&upper==lower+gap&&seen.insert(lower).second,"Invalid larger-gap record");
            const auto index=(((lower-report.range.start+1)*100-1)/report.range.length).convert_to<size_t>();segments[index].records.push_back({lower,Integer{gap},0});
        }
        for(auto& segment:segments)std::sort(segment.records.begin(),segment.records.end(),[](const auto& a,const auto& b){return a.lower<b.lower;});
        const auto commitment=commitRange(report.range,segments,{100,65536,32,100},AuditSha);
        if(!report.owner_reveal.empty())RequireAudit(report.owner_reveal==text,"Report answers already revealed");
        report.expected=values;report.details_root=commitment.root;report.owner_reveal=text;return;
    }
    auto& job=report.jobs[event.slot];RequireAudit(job&&job->attempt==event.attempt&&job->actor==event.actor&&NativeAuditEligible(ledger,event.actor),"Audit is not assigned to an admitted actor");
    RequireAudit(NativeAuditOperator(ledger,report.owner)!=NativeAuditOperator(ledger,event.actor),"Owner operator cannot audit its own report");
    const auto& other=report.jobs[1-event.slot];
    RequireAudit(!other||NativeAuditOperator(ledger,other->actor)!=NativeAuditOperator(ledger,event.actor),"The two audit slots require distinct operators");
    if(event.action==NativeAuditAction::Commit){
        RequireAudit(event.body.size()==32,"Audit commitment must be 32 bytes");const auto commitment=HexStr(event.body);
        if(job->stage==NativeAuditJob::Committed){RequireAudit(job->commitment==commitment,"Audit commitment already fixed");return;}
        RequireAudit(job->stage==NativeAuditJob::Assigned&&!NativeAuditAssignmentExpired(*job,parent.nHeight,parent.GetMedianTimePast()),"Expired audit assignment");job->commitment=commitment;job->stage=NativeAuditJob::Committed;return;
    }
    RequireAudit(event.action==NativeAuditAction::Reveal&&report.AllCommitted()&&report.expected&&job->stage!=NativeAuditJob::Assigned,"Audit reveal stage not open");
    const std::string text{event.body.begin(),event.body.end()};RequireAudit(HexStr(AuditSha(event.body))==job->commitment,"Audit answer differs from commitment");
    if(!job->reveal.empty()){RequireAudit(job->reveal==text,"Audit answer already revealed");return;}
    UniValue body;RequireAudit(body.read(text)&&body.isObject(),"Invalid audit answer JSON");AuditKeys(body);
    RequireAudit(body["schema"].get_str()=="PGN_AUDIT_ANSWER_V1"&&body["ticket"].get_str()==HexStr(report.range.ticket_id)&&body["attempt"].getInt<int64_t>()==event.attempt,"Audit answer identity mismatch");
    const auto* seed=parent.GetAncestor(report.report.height+1);RequireAudit(seed,"Post-commit audit seed unavailable");const auto partitions=NativeAuditPartitions(report.report.commitment.root,seed->GetBlockHash());
    const auto& indices=body["partitions"].get_array();const auto& counts=body["counts"].get_array();RequireAudit(indices.size()==5&&counts.size()==5,"Five audit results required");bool passed=true;
    for(size_t i=0;i<5;++i){RequireAudit(indices[i].getInt<int64_t>()==partitions[event.slot][i],"Wrong audit partition");passed&=AuditCount(counts[i])==(*report.expected)[partitions[event.slot][i]];}
    const auto nonce=body["nonce"].get_str();RequireAudit(nonce.size()==64&&IsHex(nonce)&&HexStr(ParseHex(nonce))==nonce,"Invalid audit nonce");job->stage=passed?NativeAuditJob::Passed:NativeAuditJob::Failed;job->reveal=text;
}
inline std::optional<NativeAuditLedger> ReadNativeAuditLedger(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& history,const RangePolicyReadBudget& policy_budget,
    const RangeScheduleBudget& schedule_budget,ScriptWorkBudget signature_budget){
    try{
        auto reports=ReadConfirmedNativeReports(view,parent,history,schedule_budget);if(!reports)return {};
        NativeAuditLedger ledger;
        for(int32_t height=0;height<=parent.nHeight&&ledger.authority.empty();++height){
            const auto loaded=history.Load(view,parent,height,schedule_budget.archive);if(!loaded.block)return {};
            const auto scan=ScanConfirmedTicketRequests(view,parent,*loaded.block,schedule_budget.scan);if(!scan.batch)return {};
            if(!scan.batch->Requests().empty()){const auto& first=scan.batch->Requests().front().Request();ledger.authority=first.Request().miner;ledger.authority_request=first.Id();ledger.authority_height=height;
                const auto operator_id=NativeAuditOperator(ledger,ledger.authority);ledger.participants.emplace(Bytes{ledger.authority.begin(),ledger.authority.end()},NativeAuditMembership{operator_id,false});}
        }
        const auto policy=ReadChainRangePolicy(view,parent,history,schedule_budget.archive,policy_budget);if(!policy.policy)return {};
        if(!reports->empty()){
            // Every report uses the same authenticated parent and policy.
            // Replaying that entire schedule separately per report is quadratic.
            if(!ChainRangePolicyOnBranch(view,parent,*policy.policy))return {};
            const auto schedule=ReconstructCandidateRangeSchedule(view,parent,*policy.policy,history,schedule_budget);
            if(!schedule.schedule)return {};
            std::map<uint256,const CandidateRangeLease*> leases;
            for(const auto& lease:schedule.schedule->active)leases.emplace(lease.owner.Id(),&lease);
            for(const auto& [id,report]:*reports){
                const auto found=leases.find(id);if(found==leases.end())return {};
                const auto& lease=*found->second;NativeReportAudit entry;entry.report=report;entry.owner=lease.owner.Request().miner;
                std::copy(report.commitment.ticket.begin(),report.commitment.ticket.end(),entry.range.ticket_id.begin());
                entry.range.start=lease.assignment.start;entry.range.length=lease.assignment.end-lease.assignment.start;entry.range.segments=100;
                ledger.emplace(id,std::move(entry));
            }
        }
        for(int32_t height=1;height<=parent.nHeight;++height){
            const auto loaded=history.Load(view,parent,height,schedule_budget.archive);const auto* index=parent.GetAncestor(height);
            if(!loaded.block||!index||!index->pprev)return {};
            for(const auto& output:loaded.block->Coinbase()->vout){
                if(!IsNativeAuditScript(output.scriptPubKey))continue;
                const auto event=DecodeNativeAuditEnvelope(output.scriptPubKey);if(!event||output.nValue!=0)return {};
                ApplyNativeAuditEvent(ledger,*event,*index->pprev,policy.policy->Chain(),signature_budget);
            }
        }return ledger;
    }catch(const std::exception&){return {};}
}
inline bool VerifyProposedNativeAuditEvents(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& history,const CBlock& block,const RangePolicyReadBudget& policy_budget,
    const RangeScheduleBudget& schedule_budget,ScriptWorkBudget signature_budget){
    bool present{};for(const auto& out:block.vtx.front()->vout)present|=IsNativeAuditScript(out.scriptPubKey);if(!present)return true;
    auto ledger=ReadNativeAuditLedger(view,parent,history,policy_budget,schedule_budget,signature_budget);if(!ledger)return false;
    const auto head=view.GetPgnFactHead();if(!head)return false;
    try{
        size_t count{};for(const auto& out:block.vtx.front()->vout){
            if(!IsNativeAuditScript(out.scriptPubKey))continue;if(++count>256||out.nValue!=0)return false;
            const auto event=DecodeNativeAuditEnvelope(out.scriptPubKey);if(!event)return false;
            ApplyNativeAuditEvent(*ledger,*event,parent,head->chain,signature_budget);
        }return true;
    }catch(const std::exception&){return false;}
}

}

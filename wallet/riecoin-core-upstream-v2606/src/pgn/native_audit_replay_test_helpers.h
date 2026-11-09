// Test-only complete audit-state comparison. No runtime inclusion.
#pragma once
#include <pgn/native_report_audit.h>
namespace pgn::core {
inline bool NativeAuditReplaySame(const NativeAuditLedger& a,const NativeAuditLedger& b)
{
    if(a.size()!=b.size()||a.authority!=b.authority||a.authority_request!=b.authority_request||
       a.authority_height!=b.authority_height||a.public_admission!=b.public_admission||
       a.participants.size()!=b.participants.size())return false;
    for(const auto& [id,member]:a.participants){
        const auto j=b.participants.find(id);
        if(j==b.participants.end()||member.operator_id!=j->second.operator_id||member.may_audit!=j->second.may_audit)return false;
    }
    for(const auto& [id,x]:a){
        const auto found=b.find(id);if(found==b.end())return false;const auto& y=found->second;
        if(!native_audit_replay_detail::SameReportBase(x,y)||x.expected!=y.expected||x.details_root!=y.details_root||x.owner_reveal!=y.owner_reveal)return false;
        for(unsigned slot=0;slot<2;++slot){
            if(bool(x.jobs[slot])!=bool(y.jobs[slot]))return false;
            if(!x.jobs[slot])continue;const auto& u=*x.jobs[slot];const auto& v=*y.jobs[slot];
            if(u.actor!=v.actor||u.attempt!=v.attempt||u.issued!=v.issued||u.deadline!=v.deadline||u.stage!=v.stage||
               u.commitment!=v.commitment||u.reveal!=v.reveal||u.passed_height!=v.passed_height||bool(u.time_lease)!=bool(v.time_lease))return false;
            if(u.time_lease&&(u.time_lease->clock!=v.time_lease->clock||u.time_lease->issued!=v.time_lease->issued||u.time_lease->deadline!=v.time_lease->deadline))return false;
        }
    }
    return true;
}
}

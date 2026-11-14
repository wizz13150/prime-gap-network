// Local replay encoding only. Its digest is kept in process memory after
// authenticated replay; no disk content creates consensus authority.
inline void WriteCheckpointLedger(DataStream& s,const NativeAuditLedger& ledger,
    std::vector<std::pair<uint256,size_t>>* offsets=nullptr){
    if(offsets)offsets->emplace_back(uint256{},0);
    s<<uint8_t{3}<<ledger.authority<<ledger.authority_request<<ledger.authority_height<<ledger.public_admission<<ledger.audit_chain;
    s<<bool(ledger.audit_operator_rules);if(ledger.audit_operator_rules)s<<ledger.audit_operator_rules->chain<<ledger.audit_operator_rules->activation_height;
    s<<bool(ledger.publication_height);if(ledger.publication_height)s<<*ledger.publication_height;
    s<<uint64_t(ledger.participants.size());for(const auto& [script,member]:ledger.participants)s<<script<<member.operator_id<<member.may_audit;
    s<<uint64_t(ledger.size());for(const auto& [id,e]:ledger){
        if(offsets){HashWriter key{TaggedHash("PGN/replay-checkpoint/report/1")};key<<id;offsets->emplace_back(key.GetHash(),s.size());}
        const auto& r=e.report;const auto& c=r.commitment;
        s<<id<<r.block<<r.height<<c.request<<c.ticket<<c.root<<c.parent<<c.signature<<e.owner<<e.range.ticket_id<<e.range.start.str()<<e.range.length.str()<<e.range.segments;
        s<<bool(e.expected);if(e.expected)for(auto v:*e.expected)s<<v;
        s<<bool(e.details_root);if(e.details_root)s<<*e.details_root;
        s<<e.owner_reveal;
        for(const auto& j:e.jobs){
            s<<bool(j);if(!j)continue;
            s<<j->actor<<j->attempt<<j->issued<<j->deadline<<uint8_t(j->stage)<<j->commitment<<j->reveal<<j->passed_height<<j->committed_height<<j->committed_parent_time<<bool(j->time_lease);
            if(j->time_lease)s<<uint8_t(j->time_lease->clock)<<j->time_lease->issued<<j->time_lease->deadline;
        }
    }
}
inline NativeAuditLedger ReadCheckpointLedger(DataStream& s){
    NativeAuditLedger ledger;uint8_t version{};bool present{};uint64_t count{};
    s>>version;RequireAudit(version>=1&&version<=3,"Unknown replay checkpoint codec");
    s>>ledger.authority>>ledger.authority_request>>ledger.authority_height>>ledger.public_admission>>ledger.audit_chain>>present;
    if(present){NativeAuditOperatorRules rules;s>>rules.chain>>rules.activation_height;ledger.audit_operator_rules=rules;}
    if(version>=2){s>>present;if(present){int32_t height;s>>height;RequireAudit(height>=0,"Invalid publication activation");ledger.publication_height=height;}}
    s>>count;RequireAudit(count<=s.size(),"Invalid participant checkpoint count");
    while(count--){Bytes script;NativeAuditMembership member;s>>script>>member.operator_id>>member.may_audit;RequireAudit(ledger.participants.emplace(std::move(script),member).second,"Duplicate participant checkpoint");}
    s>>count;RequireAudit(count<=s.size(),"Invalid report checkpoint count");
    while(count--){
        uint256 id;NativeReportAudit e;auto& r=e.report;auto& c=r.commitment;std::string start,length;
        s>>id>>r.block>>r.height>>c.request>>c.ticket>>c.root>>c.parent>>c.signature>>e.owner>>e.range.ticket_id>>start>>length>>e.range.segments;
        e.range.start=Integer{start};e.range.length=Integer{length};
        s>>present;if(present){e.expected.emplace();for(auto& v:*e.expected)s>>v;}
        s>>present;if(present){e.details_root.emplace();s>>*e.details_root;}
        s>>e.owner_reveal;
        for(auto& j:e.jobs){
            s>>present;if(!present)continue;j.emplace();uint8_t stage{},clock{};
            s>>j->actor>>j->attempt>>j->issued>>j->deadline>>stage>>j->commitment>>j->reveal>>j->passed_height;
            if(version>=3)s>>j->committed_height>>j->committed_parent_time;
            s>>present;j->stage=NativeAuditJob::Stage(stage);
            if(present){j->time_lease.emplace();s>>clock>>j->time_lease->issued>>j->time_lease->deadline;j->time_lease->clock=WorkLease::Clock(clock);}
        }
        RequireAudit(ledger.emplace(id,std::move(e)).second,"Duplicate report checkpoint");
    }
    return ledger;
}

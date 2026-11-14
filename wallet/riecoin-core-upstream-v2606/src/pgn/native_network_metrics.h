// Copyright (c) 2026 The PGN developers. MIT.
#pragma once
#include <pgn/native_network_target.h>
#include <pgn/native_report_audit.h>
#include <pgn/native_retarget_policy.h>
#include <pgn/bit_range_coverage.h>
#include <algorithm>
#include <vector>

namespace pgn::core {
// First active-chain height whose median time is strictly after the boundary.
// Median chain time is monotone on a validated branch; equal timestamps stay
// together instead of dividing by a zero inter-block duration.
inline int32_t NativeHeightAfterTime(const CBlockIndex& tip,int64_t boundary){
    int64_t low=0,high=int64_t(tip.nHeight)+1;
    while(low<high){
        const auto middle=low+(high-low)/2;
        const auto* index=tip.GetAncestor(int32_t(middle));
        if(!index)throw std::runtime_error("Metrics ancestry unavailable");
        if(index->GetMedianTimePast()<=boundary)low=middle+1;else high=middle;
    }
    return int32_t(low);
}

inline UniValue NativeNetworkMetrics(const NativeAuditLedger& ledger,const CBlockIndex& tip,
    const MeritControllerDefinition& policy,unsigned maximum_points,int64_t span_seconds,int64_t window_seconds,
    const RangePoolDefinition* range_domain=nullptr,
    const pgn::discussion::NativeHourlyHistory* hourly_history=nullptr){
    if(maximum_points<2||maximum_points>256||span_seconds<60||span_seconds>604800||
       window_seconds<30||window_seconds>86400||tip.nHeight<0||tip.nHeight==std::numeric_limits<int32_t>::max())
        throw std::invalid_argument("Bounded metrics window required");
    const auto* genesis=tip.GetAncestor(0);
    if(!genesis||!genesis->phashBlock)throw std::runtime_error("Metrics genesis unavailable");
    const int64_t origin=genesis->GetMedianTimePast(),now=tip.GetMedianTimePast();
    if(origin<0||now<origin)throw std::runtime_error("Metrics chain clock invalid");
    const int64_t start=std::max(origin,now-span_seconds);
    const int64_t step=std::max(int64_t(1),(span_seconds+maximum_points-2)/(maximum_points-1));
    // Epoch-aligned historic samples stay stable as new ledger records arrive.
    std::vector<int64_t> timestamps;
    for(int64_t time=((start+step-1)/step)*step;time<now;time+=step)timestamps.push_back(time);
    if(timestamps.size()>=maximum_points)timestamps.erase(timestamps.begin());
    timestamps.push_back(now);
    std::vector<int32_t> first,last;
    for(const auto time:timestamps){
        first.push_back(NativeHeightAfterTime(tip,std::max(origin-1,time-window_seconds)));
        last.push_back(NativeHeightAfterTime(tip,time)-1);
    }
    std::vector<Integer> reported_delta(timestamps.size()+1),audited_delta(timestamps.size()+1);
    Integer reported_total{},audited_total{},audit_compute_positions{};
    uint64_t waiting{},assigned{},committed{},passed{},failed{},fully_audited{},not_seeded{},distinct_operator_reports{},self_rechecked_reports{};
    int64_t oldest_pending=now;
    std::set<uint256> recent_operators,enabled_operators;
    CoverageSpans reported_ranges,audited_ranges;
    for(const auto& [script,member]:ledger.participants)if(member.may_audit)enabled_operators.insert(member.operator_id);
    for(const auto& [id,report]:ledger){
        const auto* confirmation=tip.GetAncestor(report.report.height);
        if(!confirmation||confirmation->GetBlockHash()!=report.report.block||report.range.length<=0)
            throw std::runtime_error("Metrics report provenance invalid");
        const bool audited=NativeAuditReportQualified(ledger,report);
        if(audited){if(NativeAuditOperatorsIndependent(ledger,report))++distinct_operator_reports;else ++self_rechecked_reports;}
        if(range_domain){
            const Integer end=report.range.start+report.range.length;
            reported_ranges.emplace_back(report.range.start,end);
            if(audited)audited_ranges.emplace_back(report.range.start,end);
        }
        reported_total+=report.range.length;
        if(audited){audited_total+=report.range.length;++fully_audited;}
        const auto begin=std::lower_bound(last.begin(),last.end(),report.report.height)-last.begin();
        const auto end=std::upper_bound(first.begin(),first.end(),report.report.height)-first.begin();
        if(begin<end){
            reported_delta[begin]+=report.range.length;reported_delta[end]-=report.range.length;
            if(audited){audited_delta[begin]+=report.range.length;audited_delta[end]-=report.range.length;}
            if(end==int64_t(timestamps.size()))recent_operators.insert(NativeAuditOperator(ledger,report.owner));
        }
        if(report.report.height>=tip.nHeight){not_seeded+=2;continue;}
        const auto auditPositions=[&](size_t slot)->Integer{
            if(report.range.length%100==0)return report.range.length/20;
            const auto* seed=tip.GetAncestor(report.report.height+1);if(!seed)throw std::runtime_error("Metrics audit seed unavailable");
            const auto partitions=NativeAuditPartitions(report.report.commitment.root,seed->GetBlockHash());Integer positions{};
            for(auto part:partitions[slot])positions+=report.range.length*(part+1)/100-report.range.length*part/100;
            return positions;
        };
        for(size_t slot=0;slot<report.jobs.size();++slot){
            const auto& optional=report.jobs[slot];
            if(!optional){++waiting;audit_compute_positions+=auditPositions(slot);oldest_pending=std::min(oldest_pending,confirmation->GetMedianTimePast());continue;}
            const auto& job=*optional;
            if(job.stage==NativeAuditJob::Passed){++passed;continue;}
            if(job.stage==NativeAuditJob::Failed){++failed;continue;}
            if(job.stage==NativeAuditJob::Committed){++committed;continue;}
            if(NativeAuditAssignmentExpired(job,tip.nHeight,now))++waiting;else ++assigned;
            audit_compute_positions+=auditPositions(slot);
            oldest_pending=std::min(oldest_pending,confirmation->GetMedianTimePast());
        }
    }
    UniValue result{UniValue::VOBJ};
    result.pushKV("schema","PGN_NETWORK_METRICS_V1");
    result.pushKV("genesis",genesis->GetBlockHash().GetHex());result.pushKV("bestblockhash",tip.GetBlockHash().GetHex());
    result.pushKV("height",tip.nHeight);result.pushKV("median_time_past",now);
    UniValue operator_policy{UniValue::VOBJ};operator_policy.pushKV("schema","PGN_AUDIT_OPERATOR_POLICY_V1");
    operator_policy.pushKV("genesis",genesis->GetBlockHash().GetHex());
    const bool self_rechecks=NativeAuditRecheckAt(ledger,tip.nHeight);
    operator_policy.pushKV("self_rechecks_permitted",self_rechecks);
    operator_policy.pushKV("required_distinct_other_operators",self_rechecks?0:2);
    if(ledger.audit_operator_rules)operator_policy.pushKV("activation_height",ledger.audit_operator_rules->activation_height);
    else operator_policy.pushKV("activation_height",UniValue{});
    result.pushKV("audit_operator_policy",operator_policy);
    result.pushKV("rate_scope","reported_search_positions_per_chain_second_excludes_audits");
    result.pushKV("audit_validation_scope","historical_reports_qualified_at_snapshot_tip");
    result.pushKV("window_seconds",window_seconds);result.pushKV("span_seconds",span_seconds);
    result.pushKV("report_count",uint64_t(ledger.size()));result.pushKV("audited_report_count",fully_audited);
    result.pushKV("distinct_operator_audited_reports",distinct_operator_reports);result.pushKV("self_rechecked_reports",self_rechecked_reports);
    result.pushKV("reported_positions",reported_total.convert_to<std::string>());
    result.pushKV("audited_positions",audited_total.convert_to<std::string>());
    result.pushKV("reporting_operators_in_window",uint64_t(recent_operators.size()));
    result.pushKV("audit_enabled_operators",uint64_t(enabled_operators.size()));
    if(range_domain){
        const Integer limit=range_domain->limit.value_or(range_domain->origin+range_domain->width*range_domain->slots);
        UniValue coverage{UniValue::VOBJ},bands{UniValue::VARR};
        coverage.pushKV("schema","PGN_BIT_RANGE_COVERAGE_V1");
        coverage.pushKV("scope","unique_reported_ranges_audited_subset_excluded_prefix_separate");
        coverage.pushKV("origin",range_domain->origin.convert_to<std::string>());coverage.pushKV("limit",limit.convert_to<std::string>());
        for(const auto& band:ComputeBitRangeCoverage(range_domain->origin,limit,std::move(reported_ranges),std::move(audited_ranges))){
            UniValue row{UniValue::VOBJ};row.pushKV("bits",band.bits);
            const auto integer=[&](const char* name,const Integer& number){row.pushKV(name,number.convert_to<std::string>());};
            integer("lower",band.lower);integer("upper",band.upper);integer("search_start",band.search_start);integer("search_end",band.search_end);
            integer("excluded_prefix",band.excluded_prefix);integer("reported_positions",band.reported);integer("audited_positions",band.audited);
            const Integer width=band.upper-band.lower;
            row.pushKV("excluded_ppb",CoveragePartsPerBillion(band.excluded_prefix,width));
            row.pushKV("reported_ppb",CoveragePartsPerBillion(band.reported,width));
            row.pushKV("audited_ppb",CoveragePartsPerBillion(band.audited,width));
            bands.push_back(row);
        }
        coverage.pushKV("bands",bands);result.pushKV("range_coverage",coverage);
    }
    UniValue queue{UniValue::VOBJ};queue.pushKV("awaiting",waiting);queue.pushKV("assigned",assigned);
    queue.pushKV("committed",committed);queue.pushKV("passed",passed);queue.pushKV("failed",failed);
    queue.pushKV("awaiting_seed",not_seeded);queue.pushKV("compute_positions_pending",audit_compute_positions.convert_to<std::string>());
    queue.pushKV("oldest_compute_pending_age_seconds",now-oldest_pending);result.pushKV("audit_queue",queue);
    UniValue points{UniValue::VARR};Integer reported{},audited{};
    for(size_t i=0;i<timestamps.size();++i){
        reported+=reported_delta[i];audited+=audited_delta[i];
        const auto* index=tip.GetAncestor(last[i]);
        if(!index)throw std::runtime_error("Metrics sample ancestry unavailable");
        const auto quote=NativeNetworkTargetAt(*index,policy,hourly_history);
        if(!quote)throw std::runtime_error("Metrics target unavailable");
        UniValue point{UniValue::VOBJ};point.pushKV("time",timestamps[i]);point.pushKV("height",index->nHeight);
        point.pushKV("hash",index->GetBlockHash().GetHex());point.pushKV("merit_target_q16",uint64_t(quote->header_q16));
        point.pushKV("gold_count",uint64_t(quote->scientific_count));
        point.pushKV("merit_target_numerator",quote->numerator.convert_to<std::string>());
        point.pushKV("merit_target_denominator",quote->denominator.convert_to<std::string>());
        point.pushKV("merit_target_source",quote->hourly?"authenticated_hourly_returns":"historical_elapsed_controller");
        point.pushKV("window_seconds",std::min(window_seconds,timestamps[i]-origin));
        point.pushKV("reported_positions",reported.convert_to<std::string>());
        point.pushKV("audited_positions",audited.convert_to<std::string>());points.push_back(point);
    }
    result.pushKV("points",points);return result;
}
}

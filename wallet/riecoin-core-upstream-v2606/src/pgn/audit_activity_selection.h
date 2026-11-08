// Copyright (c) 2026 The PGN developers. MIT.
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <set>
#include <stdexcept>
#include <utility>
#include <tuple>
#include <vector>

namespace pgn::core {
// RPC presentation selection only. No state is changed and no consensus,
// auditor eligibility, verdict or reward decision depends on this projection.
template<class Id> struct AuditActivitySelection {
 std::vector<Id> requests,absent;
 uint64_t active_reports{},active_jobs{},selected_active_reports{},recent_reports{},selected_recent_reports{};
};
struct AuditActivityRating{unsigned active_jobs{};int64_t newest_height{};bool recent{};};
// predicate(report) returns current active slots and whether a recent chain
// transition must remain visible after the job has left the active set.
// Global counts examine the cached ledger; retained selection memory and the
// RPC response remain O(limit), even when historical report count grows.
template<class Ledger,class Predicate>
AuditActivitySelection<typename Ledger::key_type> SelectAuditActivity(
 const Ledger& ledger,const std::vector<typename Ledger::key_type>& watched,
 size_t limit,Predicate predicate)
{
 using Id=typename Ledger::key_type;using Entry=std::tuple<bool,int64_t,Id,unsigned>;
 if(!limit||limit>100||watched.size()>limit)throw std::invalid_argument("Bounded audit activity selection required");
 AuditActivitySelection<Id> result;std::set<Id> chosen,watched_unique;
 for(const auto& id:watched){
  if(!watched_unique.insert(id).second)throw std::invalid_argument("Distinct watched audit requests required");
  if(ledger.contains(id)){result.requests.push_back(id);chosen.insert(id);}else result.absent.push_back(id);
 }
 const size_t capacity=limit-result.requests.size();std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> newest;
 for(const auto& [id,report]:ledger){
  const auto rating=predicate(report);const auto jobs=rating.active_jobs;if(jobs>2)throw std::invalid_argument("Invalid audit slot count");
  if(jobs){++result.active_reports;result.active_jobs+=jobs;}if(rating.recent)++result.recent_reports;
  if(chosen.contains(id)){result.selected_active_reports+=jobs!=0;result.selected_recent_reports+=rating.recent;continue;}
  if(!capacity||(!jobs&&!rating.recent))continue;const Entry entry{rating.recent,rating.newest_height,id,jobs};
  if(newest.size()<capacity)newest.push(entry);else if(entry>newest.top()){newest.pop();newest.push(entry);}
 }
 std::vector<Entry> ordered;while(!newest.empty()){ordered.push_back(newest.top());newest.pop();}std::sort(ordered.rbegin(),ordered.rend());
 for(const auto& entry:ordered){result.requests.push_back(std::get<2>(entry));result.selected_active_reports+=std::get<3>(entry)!=0;result.selected_recent_reports+=std::get<0>(entry);}
 return result;
}
}

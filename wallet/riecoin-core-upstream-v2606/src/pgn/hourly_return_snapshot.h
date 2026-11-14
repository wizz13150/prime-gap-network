// Private task13 controller. No wire, audit, activation or live policy change.
#pragma once
#include <pgn/hourly_gold_target.h>
#include <algorithm>
#include <limits>
#include <span>

namespace pgn::discussion {
using ReceiptId=std::array<unsigned char,32>;
// CONTRACT: the adapter authenticates and canonically admits a COMPLETE,
// structurally valid ticket return before constructing this projection. A bare
// ReportCommit does not satisfy that contract. Mathematical blessing is later.
// Input is ordered by ticket ID; this permits streaming deduplication with O(1)
// working storage, without retaining another copy of years of receipt history.
struct AdmittedReturn {
    ReceiptId ticket;
    int64_t first_received_utc;
    bool non_expired_at_first_receipt;
    std::optional<int64_t> failure_known_utc;
    bool operator==(const AdmittedReturn&) const=default;
};
inline std::optional<std::array<uint64_t,6>> ProjectHourlyReturns(
    int64_t hour,std::span<const AdmittedReturn> returns)
{
    if(hour<3900||hour%3600)throw std::invalid_argument("UTC hour required");
    std::array<uint64_t,6> counts{};
    const AdmittedReturn* previous=nullptr;
    for(const auto& row:returns){
        if(std::ranges::all_of(row.ticket,[](auto b){return b==0;}))return {};
        if(previous){
            if(row.ticket<previous->ticket)return {};
            if(row.ticket==previous->ticket){
                if(row!=*previous)return {}; // Conflicting first-receipt facts.
                continue; // Exact replay never creates another return.
            }
        }
        previous=&row;
        if(row.first_received_utc<0||
           (row.failure_known_utc&&*row.failure_known_utc<0))return {};
        // The authenticated adapter supplies the existing protocol's expiry
        // verdict, including suspension extensions. This projection does not
        // invent its own expiry boundary or grant eligibility to an old return.
        if(!row.non_expired_at_first_receipt||
           (row.failure_known_utc&&*row.failure_known_utc<=hour))continue;
        auto window=ReceiptWindow(hour,row.first_received_utc);
        if(!window)continue;
        if(counts[*window]==std::numeric_limits<uint64_t>::max())return {};
        ++counts[*window];
    }
    return counts;
}
enum class HourlyReason {Measured,Startup,TrueZero,UnavailableRetained};
struct HourlySnapshot {
    int64_t hour;
    ReceiptId activation_anchor,calibration_id;
    std::array<uint64_t,6> counts;
    unsigned target_tenths;
    HourlyReason reason;
    bool operator==(const HourlySnapshot&) const=default;
};
// Exactly one snapshot per canonical activation anchor/hour. This module never
// rewrites issued-ticket targets. On reorg the caller reconstructs snapshots
// from the new accepted ancestry, rather than carrying an abandoned anchor.
inline std::optional<HourlySnapshot> PrepareHourlySnapshot(
    int64_t hour,const ReceiptId& anchor,const ReceiptId& calibration,
    const std::array<uint64_t,6>& counts,Observation observation,
    HourlyCalibration model,unsigned last_valid_target_tenths,
    const std::optional<HourlySnapshot>& already_applied={})
{
    if(hour<3900||hour%3600)throw std::invalid_argument("UTC hour required");
    if(std::ranges::all_of(anchor,[](auto b){return b==0;})||
       std::ranges::all_of(calibration,[](auto b){return b==0;}))return {};
    if(already_applied){
        if(already_applied->hour!=hour||already_applied->activation_anchor!=anchor||
           already_applied->calibration_id!=calibration)return {};
        return already_applied; // Later failures cannot retarget this hour.
    }
    if(!model.frequency_numerator||!model.frequency_denominator||
       model.reference_merit<0||model.reference_merit>95)return {};
    if(observation==Observation::Unavailable){
        if(last_valid_target_tenths<275)return {};
        return HourlySnapshot{hour,anchor,calibration,{},last_valid_target_tenths,HourlyReason::UnavailableRetained};
    }
    const auto target=HourlyTarget(counts,observation,model);
    if(!target)return {};
    const auto reason=observation==Observation::Startup?HourlyReason::Startup:
        WeightedReturns(counts)==0?HourlyReason::TrueZero:HourlyReason::Measured;
    return HourlySnapshot{hour,anchor,calibration,counts,*target,reason};
}
}

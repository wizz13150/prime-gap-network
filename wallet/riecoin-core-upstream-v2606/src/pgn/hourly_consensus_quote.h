// Private task13 integration boundary. No activation or audit rule changes.
#pragma once
#include <pgn/hourly_return_snapshot.h>
#include <pgn/hourly_target_quote.h>
#include <algorithm>
namespace pgn::discussion {
enum class CanonicalHistory {Complete,Startup,Unavailable};
enum class HourlyConsensusVerdict {Ready,AwaitHistory,InvalidInput};
struct CanonicalHourlyQuote {HourlySnapshot snapshot;HourlyTargetQuote target;};
struct HourlyConsensusResult {
    HourlyConsensusVerdict verdict;
    std::optional<CanonicalHourlyQuote> quote;
};
// This pure arithmetic helper never authenticates counts or admits a block.
inline HourlyConsensusResult PrepareCanonicalHourlyCountQuote(
    int64_t hour,const ReceiptId& anchor,const ReceiptId& calibration,
    const std::array<uint64_t,6>& counts,CanonicalHistory history,
    HourlyCalibration model,uint32_t maximum_header_q16)
{
    if(history==CanonicalHistory::Unavailable)return {HourlyConsensusVerdict::AwaitHistory,{}};
    if(history!=CanonicalHistory::Complete&&history!=CanonicalHistory::Startup)
        return {HourlyConsensusVerdict::InvalidInput,{}};
    if(history==CanonicalHistory::Startup&&std::any_of(counts.begin(),counts.end(),[](auto n){return n!=0;}))
        return {HourlyConsensusVerdict::InvalidInput,{}};
    try {
        const auto snapshot=PrepareHourlySnapshot(hour,anchor,calibration,counts,
            history==CanonicalHistory::Startup?Observation::Startup:Observation::Complete,model,275);
        if(!snapshot)return {HourlyConsensusVerdict::InvalidInput,{}};
        const auto target=QuoteHourlyTarget(snapshot->target_tenths,maximum_header_q16);
        if(!target)return {HourlyConsensusVerdict::InvalidInput,{}};
        return {HourlyConsensusVerdict::Ready,CanonicalHourlyQuote{*snapshot,*target}};
    }catch(const std::invalid_argument&){return {HourlyConsensusVerdict::InvalidInput,{}};}
}
// The caller supplies native-authenticated canonical receipts. Unlike the
// display/model fallback, unavailable local history NEVER produces a target.
// A previous value is intentionally not an argument to this authorization API.
// Startup is a canonical chain condition, never an alias for a failed read.
inline HourlyConsensusResult PrepareCanonicalHourlyQuote(
    int64_t hour,const ReceiptId& anchor,const ReceiptId& calibration,
    std::span<const AdmittedReturn> receipts,CanonicalHistory history,
    HourlyCalibration model,uint32_t maximum_header_q16)
{
    if(history==CanonicalHistory::Unavailable)
        return {HourlyConsensusVerdict::AwaitHistory,{}};
    if(history!=CanonicalHistory::Complete&&history!=CanonicalHistory::Startup)
        return {HourlyConsensusVerdict::InvalidInput,{}};
    if(history==CanonicalHistory::Startup&&!receipts.empty())
        return {HourlyConsensusVerdict::InvalidInput,{}};
    try {
        const auto counts=ProjectHourlyReturns(hour,receipts);
        if(!counts)return {HourlyConsensusVerdict::InvalidInput,{}};
        return PrepareCanonicalHourlyCountQuote(hour,anchor,calibration,*counts,history,model,maximum_header_q16);
    }catch(const std::invalid_argument&){return {HourlyConsensusVerdict::InvalidInput,{}};}
}
}

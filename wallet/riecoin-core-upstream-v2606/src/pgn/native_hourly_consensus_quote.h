// Private task13 composition with the authenticated native history reader.
// No mining, header-validation or public activation hook is installed here.
#pragma once
#include <pgn/hourly_consensus_quote.h>
#include <pgn/native_hourly_complete_returns.h>
#include <hash.h>
namespace pgn::discussion {
inline HourlyConsensusResult ReadNativeHourlyConsensusQuote(
    const CCoinsView& view,const CBlockIndex& parent,
    const pgn::core::ChainRangePolicy& policy,
    const pgn::core::NativeRequestHistoryArchive& history,
    const pgn::core::RangePolicyReadBudget& policy_budget,
    const pgn::core::RangeScheduleBudget& schedule_budget,
    pgn::core::ScriptWorkBudget signature_budget,
    complete_return_detail::Cache& cache,uint32_t maximum_header_q16)
{
    AssertLockHeld(cs_main);
    if(!pgn::core::ChainRangePolicyOnBranch(view,parent,policy))
        return {HourlyConsensusVerdict::InvalidInput,{}};
    const auto rows=ReadNativeCompleteReturns(view,parent,policy,history,
        policy_budget,schedule_budget,signature_budget,cache);
    if(!rows)return {HourlyConsensusVerdict::AwaitHistory,{}};
    HashWriter model{TaggedHash("PGN/HOURLY-C1-M24-B10/1")};
    model<<policy.Id();
    const auto calibration=complete_return_detail::Id(model.GetHash());
    const auto hour=parent.GetMedianTimePast()/3600*3600;
    return PrepareCanonicalHourlyQuote(hour,complete_return_detail::Id(parent.GetBlockHash()),
        calibration,*rows,CanonicalHistory::Complete,HourlyCalibration{24,10,1},maximum_header_q16);
}
}

// Private task13 composition with the authenticated native history reader.
// No mining, header-validation or public activation hook is installed here.
#pragma once
#include <pgn/hourly_consensus_quote.h>
#include <pgn/native_hourly_complete_returns.h>
#include <pgn/native_retarget_policy.h>
#include <hash.h>
namespace pgn::discussion {
struct NativeHourlyHistoryCheck;
class NativeHourlyHistory {
    const CBlockIndex* m_tip;
    uint256 m_tip_hash;
    ReceiptId m_calibration;
    HourlyReceiptHistogram m_histogram;
    uint32_t m_maximum_header;
    NativeHourlyHistory(const CBlockIndex& tip,ReceiptId calibration,HourlyReceiptHistogram histogram,uint32_t maximum)
        :m_tip(&tip),m_tip_hash(tip.GetBlockHash()),m_calibration(calibration),
         m_histogram(std::move(histogram)),m_maximum_header(maximum){}
    friend NativeHourlyHistoryCheck ReadNativeHourlyHistory(
        const CCoinsView&,const CBlockIndex&,const pgn::core::ChainRangePolicy&,
        const pgn::core::NativeRequestHistoryArchive&,const pgn::core::RangePolicyReadBudget&,
        const pgn::core::RangeScheduleBudget&,pgn::core::ScriptWorkBudget,complete_return_detail::Cache&);
public:
    NativeHourlyHistory(const NativeHourlyHistory&)=default;
    NativeHourlyHistory& operator=(const NativeHourlyHistory&)=default;
    // Select from the authenticated prefix, never caller-supplied time fields.
    HourlyConsensusResult QuoteAt(int32_t height,const uint256& expected,uint32_t maximum=UINT32_MAX)const {
        AssertLockHeld(cs_main);
        if(!m_tip->phashBlock||m_tip->GetBlockHash()!=m_tip_hash||height<0||height>m_tip->nHeight)
            return {HourlyConsensusVerdict::InvalidInput,{}};
        const auto* parent=m_tip->GetAncestor(height);
        if(!parent||!parent->phashBlock||parent->GetBlockHash()!=expected)
            return {HourlyConsensusVerdict::InvalidInput,{}};
        const auto hour=parent->GetMedianTimePast()/3600*3600;
        const auto counts=m_histogram.Counts(hour);
        if(!counts)return {HourlyConsensusVerdict::InvalidInput,{}};
        return PrepareCanonicalHourlyCountQuote(hour,complete_return_detail::Id(expected),m_calibration,*counts,
            CanonicalHistory::Complete,HourlyCalibration{24,10,1},std::min(maximum,m_maximum_header));
    }
};
struct NativeHourlyHistoryCheck {HourlyConsensusVerdict verdict;std::optional<NativeHourlyHistory> history;};
inline NativeHourlyHistoryCheck ReadNativeHourlyHistory(
    const CCoinsView& view,const CBlockIndex& parent,
    const pgn::core::ChainRangePolicy& policy,
    const pgn::core::NativeRequestHistoryArchive& history,
    const pgn::core::RangePolicyReadBudget& policy_budget,
    const pgn::core::RangeScheduleBudget& schedule_budget,
    pgn::core::ScriptWorkBudget signature_budget,
    complete_return_detail::Cache& cache)
{
    AssertLockHeld(cs_main);
    if(!pgn::core::ChainRangePolicyOnBranch(view,parent,policy))
        return {HourlyConsensusVerdict::InvalidInput,{}};
    // C1/B10 is calibrated per standard200T return. Other widths require
    // an explicitly calibrated model, never this hidden unit assumption.
    if(policy.Definition().width!=pgn::core::Integer{"200000000000000"})
        return {HourlyConsensusVerdict::InvalidInput,{}};
    const auto pinned=pgn::core::ReadCandidateGenesisPolicyPayload(view,parent,history,schedule_budget.archive,policy_budget,pgn::core::NativeRetargetPolicyTag);
    if(!pinned.genesis)return {HourlyConsensusVerdict::AwaitHistory,{}};
    const auto target=pgn::core::DecodeNativeRetargetPayload(pinned.payload);
    if(!target)return {HourlyConsensusVerdict::InvalidInput,{}};
    HourlyReceiptHistogram histogram;
    const auto rows=ReadNativeCompleteReturns(view,parent,policy,history,
        policy_budget,schedule_budget,signature_budget,cache,&histogram);
    if(!rows)return {HourlyConsensusVerdict::AwaitHistory,{}};
    HashWriter model{TaggedHash("PGN/HOURLY-C1-M24-B10/1")};
    model<<policy.Id();
    const auto calibration=complete_return_detail::Id(model.GetHash());
    return {HourlyConsensusVerdict::Ready,NativeHourlyHistory{parent,calibration,std::move(histogram),target->maximum_q16}};
}
inline HourlyConsensusResult ReadNativeHourlyConsensusQuote(
    const CCoinsView& view,const CBlockIndex& parent,const pgn::core::ChainRangePolicy& policy,
    const pgn::core::NativeRequestHistoryArchive& history,const pgn::core::RangePolicyReadBudget& policy_budget,
    const pgn::core::RangeScheduleBudget& schedule_budget,pgn::core::ScriptWorkBudget signature_budget,
    complete_return_detail::Cache& cache,uint32_t maximum_header_q16)
{
    const auto checked=ReadNativeHourlyHistory(view,parent,policy,history,policy_budget,schedule_budget,signature_budget,cache);
    if(!checked.history)return {checked.verdict,{}};
    return checked.history->QuoteAt(parent.nHeight,parent.GetBlockHash(),maximum_header_q16);
}
}

#include <pgn/candidate_range_schedule.h>
#include <pgn/candidate_range_reader.h>
#include <pgn/native_report_commitment.h>
#include <coins.h>
#include <chain.h>
#include <kernel/cs_main.h>
#include <hash.h>
#include <limits>
#include <map>
namespace pgn::core {
namespace {
CandidateRangeScheduleReader& VerifiedScheduleReader(){
    AssertLockHeld(cs_main);
    static CandidateRangeScheduleReader reader;
    return reader;
}
uint256 ReservationIdentity(const uint256& policy,const CandidateRangeLease& lease){
    HashWriter hash{TaggedHash(lease.time_lease?"PGN/UNAPPROVED/NATIVE-RESERVATION/2":"PGN/UNAPPROVED/NATIVE-RESERVATION/1")};
    hash<<policy<<lease.assignment.request<<lease.assignment.seed<<lease.issued;
    if(lease.time_lease)hash<<lease.time_lease->issued<<lease.time_lease->deadline;
    else hash<<lease.deadline;
    return hash.GetHash();
}
}
RangeScheduleCheck ReconstructCandidateRangeSchedule(const CCoinsView& view,const CBlockIndex& parent,
    const ChainRangePolicy& policy,const NativeRequestHistoryArchive& archive,RangeScheduleBudget budget){
    AssertLockHeld(cs_main);
    auto& reader=VerifiedScheduleReader();
    // Admission and RPC require a complete answer. Pay accepted-parent cold
    // replay once in slices; a continuation is not an invalid reservation.
    for(;;){
        auto result=reader.Read(view,parent,policy,archive,budget);
        if(result.verdict!=RangeScheduleVerdict::BudgetExceeded||!reader.MoreWorkPending())return result;
    }
}
RangeLeaseCheck ReconstructCandidateRangeLease(const CCoinsView& view,const CBlockIndex& parent,
    const ChainRangePolicy& policy,const NativeRequestHistoryArchive& archive,RangeScheduleBudget budget,const uint256& request){
    AssertLockHeld(cs_main);
    auto& reader=VerifiedScheduleReader();
    for(;;){
        auto result=reader.ReadLease(view,parent,policy,archive,budget,request);
        if(result.verdict!=RangeScheduleVerdict::BudgetExceeded||!reader.MoreWorkPending())return result;
    }
}
RangeScheduleCheck VisitCandidateRangeSchedule(const CCoinsView& view,const CBlockIndex& parent,
    const ChainRangePolicy& policy,const NativeRequestHistoryArchive& archive,RangeScheduleBudget budget,
    const std::function<void(const CandidateRangeLease&)>& visitor){
    AssertLockHeld(cs_main);auto& reader=VerifiedScheduleReader();
    for(;;){auto result=reader.Visit(view,parent,policy,archive,budget,visitor);
        if(result.verdict!=RangeScheduleVerdict::BudgetExceeded||!reader.MoreWorkPending())return result;
    }
}
CandidateReservationCheck DeriveNativeCandidateReservation(const CCoinsView& view,const CBlockIndex& parent,
    const ChainRangePolicy& policy,const NativeRequestHistoryArchive& archive,RangeScheduleBudget budget,const uint256& request){
    AssertLockHeld(cs_main);
    if(!ChainRangePolicyOnBranch(view,parent,policy))return {CandidateReservationVerdict::InvalidParent,{},{}};
    auto found=ReconstructCandidateRangeLease(view,parent,policy,archive,budget,request);
    if(found.verdict!=RangeScheduleVerdict::ReconstructedCandidate)
        return {CandidateReservationVerdict::HistoryHeld,{},found.verdict};
    if(!found.lease)return {CandidateReservationVerdict::NotReserved,{},found.verdict};
    const auto identity=ReservationIdentity(policy.Id(),*found.lease);
    return {CandidateReservationVerdict::BoundToNativeHistory,
        NativeCandidateReservation{parent.GetBlockHash(),policy.Id(),identity,std::move(*found.lease)},found.verdict};
}
bool NativeCandidateReservationAtParent(const CCoinsView& view,const CBlockIndex& parent,const ChainRangePolicy& policy,
    const NativeCandidateReservation& reservation){
    AssertLockHeld(cs_main);
    return ChainRangePolicyOnBranch(view,parent,policy)&&reservation.Parent()==parent.GetBlockHash()&&reservation.Policy()==policy.Id()&&
        reservation.Lease().issued<=parent.nHeight&&(reservation.Lease().report_root||
            (reservation.Lease().time_lease?!reservation.Lease().time_lease->expired(parent.nHeight,parent.GetMedianTimePast()):reservation.Lease().deadline>parent.nHeight));
}
}

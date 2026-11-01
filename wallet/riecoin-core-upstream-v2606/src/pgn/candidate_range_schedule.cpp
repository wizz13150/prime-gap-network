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
    static CandidateRangeScheduleReader reader;
    // Admission and RPC require a complete answer. Pay accepted-parent cold
    // replay once in slices; a continuation is not an invalid reservation.
    for(;;){
        auto result=reader.Read(view,parent,policy,archive,budget);
        if(result.verdict!=RangeScheduleVerdict::BudgetExceeded||!reader.MoreWorkPending())return result;
    }
}
CandidateReservationCheck DeriveNativeCandidateReservation(const CCoinsView& view,const CBlockIndex& parent,
    const ChainRangePolicy& policy,const NativeRequestHistoryArchive& archive,RangeScheduleBudget budget,const uint256& request){
    AssertLockHeld(cs_main);
    if(!ChainRangePolicyOnBranch(view,parent,policy))return {CandidateReservationVerdict::InvalidParent,{},{}};
    auto schedule=ReconstructCandidateRangeSchedule(view,parent,policy,archive,budget);
    if(!schedule.schedule)return {CandidateReservationVerdict::HistoryHeld,{},schedule.verdict};
    for(auto& lease:schedule.schedule->active)if(lease.owner.Id()==request){
        const auto identity=ReservationIdentity(policy.Id(),lease);
        return {CandidateReservationVerdict::BoundToNativeHistory,
            NativeCandidateReservation{parent.GetBlockHash(),policy.Id(),identity,std::move(lease)},schedule.verdict};
    }
    return {CandidateReservationVerdict::NotReserved,{},schedule.verdict};
}
bool NativeCandidateReservationAtParent(const CCoinsView& view,const CBlockIndex& parent,const ChainRangePolicy& policy,
    const NativeCandidateReservation& reservation){
    AssertLockHeld(cs_main);
    return ChainRangePolicyOnBranch(view,parent,policy)&&reservation.Parent()==parent.GetBlockHash()&&reservation.Policy()==policy.Id()&&
        reservation.Lease().issued<=parent.nHeight&&(reservation.Lease().report_root||
            (reservation.Lease().time_lease?!reservation.Lease().time_lease->expired(parent.nHeight,parent.GetMedianTimePast()):reservation.Lease().deadline>parent.nHeight));
}
}

// MIT. Read-only UNAPPROVED schedule reconstruction, NOT consensus authority.
#pragma once
#include <pgn/chain_range_policy.h>
#include <pgn/candidate_range_plan.h>
#include <pgn/request_registry.h>
#include <pgn/work_lease_clock.h>
namespace pgn::core {
struct CandidateRangeLease {
    CandidateRangeAssignment assignment;
    VerifiedTicketRequest owner;
    int32_t issued;int64_t deadline;
    std::optional<uint256> report_root{};
    std::optional<WorkLease> time_lease{};
};
struct RangeScheduleBudget {
    uint32_t maximum_blocks;size_t maximum_requests,maximum_active_leases;
    RequestHistoryArchiveBudget archive;ConfirmedRequestScanBudget scan;RangeDrawBudget draw;
};
enum class RangeScheduleVerdict {ReconstructedCandidate,InvalidParent,MissingHistory,InvalidHistory,
    BudgetExceeded,PoolExhausted};
struct CandidateRangeSchedule {
    uint256 chain,parent,policy;
    int32_t height;
    std::vector<CandidateRangeLease> active;
    uint64_t issued{},expired{},hashes{};
};
struct RangeScheduleCheck {RangeScheduleVerdict verdict;std::optional<CandidateRangeSchedule> schedule;};
// cs_main held. NO caller occupancy, selected request list, seed or range.
// Replays EVERY authenticated native block from this chain's genesis and ALL
// unique signed requests. At block h, assignments use the already known source
// at h-1 (never that block's own hash). Tickets precede their proof block:
// Legacy: issued=h, first proof=h+1, last proof=h+lease_blocks. Timed request
// bodies explicitly bind a duration beginning at the issue block's median
// chain time. The two clock domains have separate expiry queues and identities.
// Expire before allocating for the next block. Full-pool/missing history holds
// result, never drops a request. The genesis policy is chain-authenticated but
// UNAPPROVED; expiry, saturation policy and implicit assignment are candidates.
// A confirmed report keeps its slot occupied while audits are pending.
// A report commitment alone never authorizes completed coverage or rewards.
// One sequential visit per block plus request/lease tree work; native Merkle,
// signature, ancestor, integer-bit and ranked-selection costs remain charged.
// No allocation proportional to the numeric slot domain. No node/DB writes.
RangeScheduleCheck ReconstructCandidateRangeSchedule(const CCoinsView&,const CBlockIndex&,
    const ChainRangePolicy&,const NativeRequestHistoryArchive&,RangeScheduleBudget);

struct CandidateReservationCheck;
class NativeCandidateReservation {
    uint256 m_parent,m_policy,m_ticket;
    CandidateRangeLease m_lease;
    NativeCandidateReservation(uint256 parent,uint256 policy,uint256 ticket,CandidateRangeLease lease)
        :m_parent(parent),m_policy(policy),m_ticket(ticket),m_lease(std::move(lease)){}
    friend CandidateReservationCheck DeriveNativeCandidateReservation(const CCoinsView&,const CBlockIndex&,
        const ChainRangePolicy&,const NativeRequestHistoryArchive&,RangeScheduleBudget,const uint256&);
public:
    NativeCandidateReservation(const NativeCandidateReservation&)=default;
    NativeCandidateReservation& operator=(const NativeCandidateReservation&)=default;
    const uint256& Parent()const{return m_parent;}
    const uint256& Policy()const{return m_policy;}
    const uint256& Ticket()const{return m_ticket;}
    const CandidateRangeLease& Lease()const{return m_lease;}
};
enum class CandidateReservationVerdict {BoundToNativeHistory,InvalidParent,NotReserved,HistoryHeld};
struct CandidateReservationCheck {
    CandidateReservationVerdict verdict;std::optional<NativeCandidateReservation> reservation;
    std::optional<RangeScheduleVerdict> history;
};
// Derives owner/reward/range/issue/deadline from the WHOLE native candidate
// history internally, never seals caller DTOs. Native chain authenticity does
// NOT approve these candidate rules, solve bootstrap or authorize chain credit.
CandidateReservationCheck DeriveNativeCandidateReservation(const CCoinsView&,const CBlockIndex&,
    const ChainRangePolicy&,const NativeRequestHistoryArchive&,RangeScheduleBudget,const uint256& request);
bool NativeCandidateReservationAtParent(const CCoinsView&,const CBlockIndex&,const ChainRangePolicy&,
    const NativeCandidateReservation&);
}

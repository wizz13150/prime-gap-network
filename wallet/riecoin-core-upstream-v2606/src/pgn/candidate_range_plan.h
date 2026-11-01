// MIT. UNAPPROVED allocation computation, not an eligible-ticket factory.
#pragma once
#include <pgn/ticket_seed.h>
#include <pgn/sparse_range_pool.h>
namespace pgn::core {
struct RangeDrawBudget {uint32_t maximum_attempts;size_t maximum_draw_bytes;uint64_t maximum_hashes;};
struct CandidateRangeAssignment {uint256 request,seed;Integer free_count,slot,start,end;uint32_t attempt;};
enum class CandidateRangeVerdict {Planned,InvalidParent,InvalidBudget,PoolExhausted,BudgetExceeded,PoolChanged};
struct CandidateRangeCheck {
    CandidateRangeVerdict verdict;
    std::optional<std::vector<CandidateRangeAssignment>> assignments;
    uint64_t hashes{};
};
// Native cs_main held, pool exclusively owned/serialized by this caller.
// Complete native due set, no chosen subset. The pool is RESTORED on success,
// rejection or exception: this plans assignments, never commits leases.
// Rejection sampling removes modulo bias IF input bits are uniform. A native
// SHA-derived seed is deterministic; resistance to producer/request grinding,
// authoritative available-state, approved pool policy and block admission are
// NOT established here. Public result DTO is not a consensus authority token.
CandidateRangeCheck PlanCandidateRanges(const CCoinsView&,const CBlockIndex&,
    const CompleteMaturedTicketSeeds&,SparseRangePool&,RangeDrawBudget);
}

// MIT. Exact remaining-issued-supply curve candidate, NOT selected consensus.
#pragma once
#include <pgn/issuance_budget.h>
#include <arith_uint256.h>
#include <algorithm>
namespace pgn::core {
struct IssuedSupplyLinearDefinition {CAmount initial_subsidy,minimum_tail;};
struct IssuedSupplySubsidyQuote {CAmount curve_subsidy,maximum_unpromised_grant;};
// B(S)=max(tail,floor(B0*(CAP-S)/CAP)), then budget-clamp the grant.
// S is CUMULATIVE minted across ordinary+exceptional budgets, never balance.
// Promises consume availability but are not misreported as already minted.
// All parameters explicit; no height, default curve, chosen split or authority.
inline std::optional<IssuedSupplySubsidyQuote> QuoteIssuedSupplyLinearSubsidy(
    const IssuedSupplyLinearDefinition& policy,const IssuanceBudgetState& total,const IssuanceBudgetState& ordinary){
    if(total.Cap()!=PGN_MAX_SUPPLY_ATOMIC||ordinary.Cap()>total.Cap()||policy.initial_subsidy<0||policy.initial_subsidy>total.Cap()||
       policy.minimum_tail<0||policy.minimum_tail>policy.initial_subsidy||ordinary.CumulativeMinted()>total.CumulativeMinted()||ordinary.Promised()>total.Promised())return {};
    const auto other_cap=total.Cap()-ordinary.Cap(),other_minted=total.CumulativeMinted()-ordinary.CumulativeMinted(),other_promised=total.Promised()-ordinary.Promised();
    if(other_minted>other_cap||other_promised>other_cap-other_minted)return {};
    const auto remaining=total.Cap()-total.CumulativeMinted();
    if(!remaining)return IssuedSupplySubsidyQuote{0,0};
    // Each factor <=1e18: product <2^120, so native uint256 is exact and
    // portable without floating point, signed overflow or an MSVC-only ABI.
    const arith_uint256 numerator=arith_uint256{uint64_t(policy.initial_subsidy)}*arith_uint256{uint64_t(remaining)};
    const auto raw=static_cast<CAmount>((numerator/arith_uint256{uint64_t(total.Cap())}).GetLow64());
    const auto base=std::max(policy.minimum_tail,raw);
    return IssuedSupplySubsidyQuote{base,std::min({base,ordinary.Unallocated(),total.Unallocated()})};
}
// A quote is NOT block issuance: authenticated genesis policy, native parent
// counter, recipient/distribution/fees, obligations and atomic forward/undo
// admission must still be bound before any positive coinbase can be enabled.
}

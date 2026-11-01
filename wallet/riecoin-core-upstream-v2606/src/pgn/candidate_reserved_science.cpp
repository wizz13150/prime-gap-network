#include <pgn/candidate_reserved_science.h>
#include <chain.h>
#include <coins.h>
#include <kernel/cs_main.h>
#include <algorithm>
namespace pgn::core {
ReservedScienceCheck VerifyNativeReservedScience(const CCoinsView& view,const CBlockIndex& parent,const ChainRangePolicy& policy,
    const NativeCandidateReservation& reservation,const ScientificFactContext& context,std::span<const unsigned char> signature,
    const EncodedCommittedGapEvidence& evidence,ScriptWorkBudget budget){
    AssertLockHeld(cs_main);
    if(!NativeCandidateReservationAtParent(view,parent,policy,reservation))return {ReservedScienceVerdict::InvalidParent,{},{}};
    const auto& lease=reservation.Lease();
    if(context.chain!=policy.Chain()||context.range.start!=lease.assignment.start||
       context.range.length!=lease.assignment.end-lease.assignment.start||
       !std::equal(context.range.ticket_id.begin(),context.range.ticket_id.end(),reservation.Ticket().begin()))
        return {ReservedScienceVerdict::RangeMismatch,{},{}};
    const auto work=ScientificWorkIdentity(context);
    if(!work)return {ReservedScienceVerdict::InvalidContext,{},{}};
    auto owner=VerifyScriptWork({context.chain,*work,parent.GetBlockHash()},lease.owner.Request().miner,signature,budget);
    if(!owner.proof)return {ReservedScienceVerdict::InvalidSignature,{},{}};
    auto science=VerifyScriptScientificCandidate(parent.GetBlockHash(),*owner.proof,context,evidence);
    if(!science.candidate)return {ReservedScienceVerdict::InvalidEvidence,{},science.evidence_verdict};
    const auto state=InspectScientificCandidateParent(view,*science.candidate);
    if(state==ScientificParentVerdict::AlreadyClaimed)return {ReservedScienceVerdict::AlreadyClaimed,{},science.evidence_verdict};
    if(state!=ScientificParentVerdict::UnclaimedAtParent)return {ReservedScienceVerdict::Unenrolled,{},science.evidence_verdict};
    return {ReservedScienceVerdict::VerifiedCandidate,VerifiedNativeReservedScience{reservation,std::move(*science.candidate)},science.evidence_verdict};
}
FactHistoryStep PrepareNativeReservedScienceForward(const CCoinsView& view,const CBlockIndex& parent,
    const CBlockIndex& proposed,const ChainRangePolicy& policy,std::span<const VerifiedNativeReservedScience> candidates){
    AssertLockHeld(cs_main);
    if(!ChainRangePolicyOnBranch(view,parent,policy)||!proposed.phashBlock||proposed.pprev!=&parent||
       int64_t(proposed.nHeight)!=int64_t(parent.nHeight)+1||proposed.GetBlockHash()==parent.GetBlockHash())
        throw std::logic_error("Reserved science publication requires exact native direct parent");
    const auto head=view.GetPgnFactHead();FactCommit forward{head,FactHead{head->chain,proposed.GetBlockHash()},{}};
    forward.changes.reserve(candidates.size());
    for(const auto& candidate:candidates){
        const auto& reservation=candidate.Reservation();const auto& science=candidate.Science();
        if(!NativeCandidateReservationAtParent(view,parent,policy,reservation)||
           science.OwnerProof().Script()!=reservation.Lease().owner.Request().miner||
           InspectScientificCandidateParent(view,science)!=ScientificParentVerdict::UnclaimedAtParent)
            throw std::logic_error("Reserved scientific claim is stale, duplicated or unauthorized");
        forward.changes.push_back({science.Key(),{},FactClaim{science.OwnerProof().Context().work,proposed.GetBlockHash()}});
    }
    std::sort(forward.changes.begin(),forward.changes.end(),[](const auto& a,const auto& b){return a.fact<b.fact;});
    if(!forward.Valid())throw std::logic_error("Reserved scientific publication has duplicate or invalid global facts");
    return {FactDirection::Forward,std::move(forward)};
}
std::optional<ScientificFactContext> NativeProfiledWorkContext(const CCoinsView& view,const CBlockIndex& parent,
    const ChainRangePolicy& ranges,const ChainSciencePolicy& science,const NativeCandidateReservation& reservation,const Digest& root){
    AssertLockHeld(cs_main);
    if(!NativeCandidateReservationAtParent(view,parent,ranges,reservation)||!ChainSciencePolicyOnBranch(view,parent,science)||
       ranges.Genesis()!=science.Genesis())return {};
    const auto& lease=reservation.Lease();const auto& definition=science.Definition();Digest ticket;
    std::copy(reservation.Ticket().begin(),reservation.Ticket().end(),ticket.begin());
    return ScientificFactContext{ranges.Chain(),{ticket,lease.assignment.start,lease.assignment.end-lease.assignment.start,definition.segments},
        root,definition.evidence,definition.proof};
}
ProfiledScienceCheck VerifyNativeProfiledScience(const CCoinsView& view,const CBlockIndex& parent,
    const ChainRangePolicy& ranges,const ChainSciencePolicy& science,const NativeCandidateReservation& reservation,const Digest& root,
    std::span<const unsigned char> signature,const EncodedCommittedGapEvidence& evidence,ScriptWorkBudget budget){
    const auto context=NativeProfiledWorkContext(view,parent,ranges,science,reservation,root);
    if(!context)return {ReservedScienceVerdict::InvalidParent,{},{}};
    auto checked=VerifyNativeReservedScience(view,parent,ranges,reservation,*context,signature,evidence,budget);
    if(!checked.candidate)return {checked.verdict,{},checked.evidence};
    return {ReservedScienceVerdict::VerifiedCandidate,VerifiedNativeProfiledScience{science.Id(),std::move(*checked.candidate)},checked.evidence};
}
FactHistoryStep PrepareNativeProfiledScienceForward(const CCoinsView& view,const CBlockIndex& parent,const CBlockIndex& proposed,
    const ChainRangePolicy& ranges,const ChainSciencePolicy& science,std::span<const VerifiedNativeProfiledScience> candidates){
    AssertLockHeld(cs_main);
    if(!ChainSciencePolicyOnBranch(view,parent,science)||science.Genesis()!=ranges.Genesis())
        throw std::logic_error("Scientific publication requires actual native verifier policy");
    std::vector<VerifiedNativeReservedScience> checked;checked.reserve(candidates.size());
    for(const auto& candidate:candidates){if(candidate.Policy()!=science.Id())throw std::logic_error("Scientific policy changed");checked.push_back(candidate.Science());}
    return PrepareNativeReservedScienceForward(view,parent,proposed,ranges,checked);
}
}

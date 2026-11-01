// MIT. Native UTXO authorization + exact proof + global mathematical identity.
#include <pgn/scientific_fact.h>
#include <pgn/canonical_encoding.h>
#include <chain.h>
#include <coins.h>
#include <crypto/sha256.h>
#include <hash.h>
#include <algorithm>

namespace pgn::core {
namespace {
Digest Sha(std::span<const uint8_t> bytes) {
    Digest result;CSHA256().Write(bytes.data(),bytes.size()).Finalize(result.data());return result;
}
void Limits(detail::Writer& w,const EncodingLimits& l) {
    w.little(l.max_records,8);w.little(l.max_encoded_bytes,8);
    w.little(l.max_integer_bytes,4);w.little(l.max_segments,4);
}
bool CurrentOwner(const CCoinsView& view,const UtxoOwnerContext& c,const CScript& script) {
    if(view.GetBestBlock()!=c.expected_tip)return false;
    const auto coin=view.GetCoin(c.owner_coin);return coin&&!coin->IsSpent()&&coin->out.scriptPubKey==script;
}
uint256 GlobalKey(const CertifiedGap& gap,const EncodingLimits& limits) {
    detail::Writer w{{},limits};w.domain("PGN/UNAPPROVED/GLOBAL-GAP-FACT/1");
    w.integer(gap.lower);w.integer(gap.gap);
    const auto digest=Sha(w.data);uint256 result;std::copy(digest.begin(),digest.end(),result.begin());return result;
}
struct CheckedEvidence {ScientificFactVerdict verdict;std::optional<CommittedGapVerdict> evidence;std::optional<uint256> key;};
CheckedEvidence CheckEvidence(const ScientificFactContext& context,const EncodedCommittedGapEvidence& encoded){
    try{
        auto segment=decodeSegment(context.range,encoded.segment_bytes,context.policy.encoding);
        auto science=decodeCertifiedGap(encoded.scientific_bytes,{context.proof_encoding,context.policy.scientific});
        const auto key=GlobalKey(science,context.policy.encoding);
        const auto check=verifyCommittedGap(context.range,context.report_root,
            {std::move(segment),encoded.inclusion,encoded.record_index,std::move(science)},context.policy,Sha);
        if(check.verdict!=CommittedGapVerdict::VerifiedEvidenceAtTarget)return {ScientificFactVerdict::InvalidEvidence,check.verdict,{}};
        if(key.IsNull())return {ScientificFactVerdict::InvalidContext,check.verdict,{}};
        return {ScientificFactVerdict::Verified,check.verdict,key};
    }catch(const std::invalid_argument&){return {ScientificFactVerdict::InvalidEvidence,CommittedGapVerdict::InvalidEncoding,{}};}
}
}
std::optional<uint256> ScientificWorkIdentity(const ScientificFactContext& c) {
    try {
        if(c.chain.IsNull()||c.range.start<0||c.range.length<=0||!c.range.segments)return {};
        detail::Writer w{{},c.policy.encoding};w.domain("PGN/UNAPPROVED/SCIENTIFIC-WORK/1");
        w.raw(c.chain);w.raw(c.range.ticket_id);w.integer(c.range.start);w.integer(c.range.length);w.little(c.range.segments,4);w.raw(c.report_root);
        Limits(w,c.policy.encoding);Limits(w,c.proof_encoding);
        const auto& s=c.policy.scientific;w.little(s.endpoint_limits.max_integer_bits,4);w.little(s.endpoint_limits.max_nodes,4);w.little(s.endpoint_limits.max_factors_per_node,4);w.little(s.max_interior_positions,8);
        const auto& a=c.policy.arithmetic;w.little(a.max_input_bits,4);w.little(a.max_fractional_bits,4);w.little(a.max_series_terms,4);
        w.little(c.policy.accuracy.fractional_bits,4);w.little(c.policy.accuracy.series_terms,4);
        w.integer(c.policy.target_numerator);w.integer(c.policy.target_denominator);
        const auto digest=Sha(w.data);uint256 result;std::copy(digest.begin(),digest.end(),result.begin());
        return result.IsNull()?std::nullopt:std::optional{result};
    } catch(const std::invalid_argument&) { return {}; }
}
ScientificFactCheck VerifyScientificFact(const CCoinsView& view,const UtxoOwnerAuthorization& owner,
    const ScientificFactContext& context,const EncodedCommittedGapEvidence& encoded) {
    const auto work=ScientificWorkIdentity(context);
    if(!work)return {ScientificFactVerdict::InvalidContext,{},{}};
    if(owner.Context().chain!=context.chain||owner.Context().work!=*work)return {ScientificFactVerdict::OwnerMismatch,{},{}};
    if(!CurrentOwner(view,owner.Context(),owner.Script()))return {ScientificFactVerdict::StaleOwner,{},{}};
    const auto checked=CheckEvidence(context,encoded);
    if(!checked.key)return {checked.verdict,checked.evidence,{}};
    if(!CurrentOwner(view,owner.Context(),owner.Script()))return {ScientificFactVerdict::StaleOwner,checked.evidence,{}};
    return {ScientificFactVerdict::Verified,checked.evidence,VerifiedScientificFact{*checked.key,owner}};
}
ScientificEvidenceCheck VerifyScientificEvidence(const ScientificFactContext& context,const EncodedCommittedGapEvidence& encoded){
    const auto work=ScientificWorkIdentity(context);
    if(!work)return {ScientificFactVerdict::InvalidContext,{},{}};
    const auto checked=CheckEvidence(context,encoded);
    if(!checked.key)return {checked.verdict,checked.evidence,{}};
    return {ScientificFactVerdict::Verified,checked.evidence,VerifiedScientificEvidence{context.chain,*work,*checked.key}};
}
ScriptScientificCheck BindScientificEvidenceToScript(const uint256& expectedParent,const VerifiedScriptWork& owner,
    const VerifiedScientificEvidence& science){
    if(expectedParent.IsNull())return {ScientificFactVerdict::InvalidContext,{},{}};
    if(owner.Context().chain!=science.Chain()||owner.Context().work!=science.Work()||owner.Context().parent!=expectedParent)
        return {ScientificFactVerdict::OwnerMismatch,{},{}};
    return {ScientificFactVerdict::Verified,CommittedGapVerdict::VerifiedEvidenceAtTarget,VerifiedScriptScientificCandidate{science,owner}};
}
ScriptScientificCheck VerifyScriptScientificCandidate(const uint256& expectedParent,const VerifiedScriptWork& owner,
    const ScientificFactContext& context,const EncodedCommittedGapEvidence& encoded){
    const auto work=ScientificWorkIdentity(context);
    if(!work||expectedParent.IsNull())return {ScientificFactVerdict::InvalidContext,{},{}};
    if(owner.Context().chain!=context.chain||owner.Context().work!=*work||owner.Context().parent!=expectedParent)
        return {ScientificFactVerdict::OwnerMismatch,{},{}};
    const auto checked=CheckEvidence(context,encoded);
    if(!checked.key)return {checked.verdict,checked.evidence,{}};
    return BindScientificEvidenceToScript(expectedParent,owner,VerifiedScientificEvidence{context.chain,*work,*checked.key});
}
ScientificParentVerdict InspectScientificCandidateParent(const CCoinsView& parent,const VerifiedScriptScientificCandidate& candidate){
    const auto head=parent.GetPgnFactHead();
    if(!head)return ScientificParentVerdict::Unenrolled;
    const auto tip=parent.GetBestBlock();
    if(head->tip!=tip||candidate.OwnerProof().Context().parent!=tip)return ScientificParentVerdict::StaleParent;
    if(head->chain!=candidate.Science().Chain())return ScientificParentVerdict::WrongChain;
    if(parent.GetPgnFact(candidate.Key()))return ScientificParentVerdict::AlreadyClaimed;
    return ScientificParentVerdict::UnclaimedAtParent;
}
FactHistoryStep PrepareScientificFactForward(const CCoinsView& parent,const CBlockIndex& index,
    std::span<const VerifiedScientificFact> facts) {
    const auto head=parent.GetPgnFactHead();
    if(!head||!index.phashBlock||!index.pprev||!index.pprev->phashBlock||
        head->tip!=parent.GetBestBlock()||index.pprev->GetBlockHash()!=head->tip||index.GetBlockHash()==head->tip)
        throw std::logic_error("Scientific forward requires exact enrolled native parent");
    FactCommit forward{head,FactHead{head->chain,index.GetBlockHash()},{}};
    for(const auto& fact:facts){
        if(fact.Owner().chain!=head->chain||fact.Owner().expected_tip!=head->tip||!CurrentOwner(parent,fact.Owner(),fact.OwnerScript()))
            throw std::logic_error("Scientific forward owner/context changed");
        if(parent.GetPgnFact(fact.Key()))throw std::logic_error("Global mathematical fact already claimed");
        forward.changes.push_back({fact.Key(),{},FactClaim{fact.Owner().work,index.GetBlockHash()}});
    }
    std::sort(forward.changes.begin(),forward.changes.end(),[](const auto& a,const auto& b){return a.fact<b.fact;});
    if(!forward.Valid())throw std::logic_error("Scientific forward has duplicate or invalid global facts");
    return {FactDirection::Forward,std::move(forward)};
}
} // namespace pgn::core

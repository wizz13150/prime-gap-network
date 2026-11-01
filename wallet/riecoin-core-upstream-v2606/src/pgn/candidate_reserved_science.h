// MIT. Native candidate composition, NOT approved consensus admission.
#pragma once
#include <pgn/candidate_range_schedule.h>
#include <pgn/scientific_fact.h>
#include <pgn/chain_science_policy.h>
namespace pgn::core {
enum class ReservedScienceVerdict {VerifiedCandidate,InvalidParent,RangeMismatch,InvalidContext,
    InvalidSignature,InvalidEvidence,AlreadyClaimed,Unenrolled};
struct ReservedScienceCheck;
class VerifiedNativeReservedScience {
    NativeCandidateReservation m_reservation;
    VerifiedScriptScientificCandidate m_science;
    VerifiedNativeReservedScience(NativeCandidateReservation reservation,VerifiedScriptScientificCandidate science)
        :m_reservation(std::move(reservation)),m_science(std::move(science)){}
    friend ReservedScienceCheck VerifyNativeReservedScience(const CCoinsView&,const CBlockIndex&,const ChainRangePolicy&,
        const NativeCandidateReservation&,const ScientificFactContext&,std::span<const unsigned char>,
        const EncodedCommittedGapEvidence&,ScriptWorkBudget);
public:
    // Copy-only seal: a moved-from object must not retain a verified label
    // while its owner script has been emptied by an implicit member move.
    VerifiedNativeReservedScience(const VerifiedNativeReservedScience&)=default;
    VerifiedNativeReservedScience& operator=(const VerifiedNativeReservedScience&)=default;
    const NativeCandidateReservation& Reservation()const{return m_reservation;}
    const VerifiedScriptScientificCandidate& Science()const{return m_science;}
};
struct ReservedScienceCheck {
    ReservedScienceVerdict verdict;std::optional<VerifiedNativeReservedScience> candidate;
    std::optional<CommittedGapVerdict> evidence;
};
// cs_main held. Native reservation seals WHOLE authenticated candidate history;
// range/ticket/owner are never selected by the evidence sender. Maintained
// BIP322 signature binds chain+work+CURRENT parent and the native request miner.
// Same exact scientific verifier and GLOBAL duplicate key as existing paths.
// Context's target/segmentation/verifier budgets remain explicit candidate
// inputs, NOT a chain-approved science profile. This type cannot enter the
// funded-owner PrepareScientificFactForward, ConnectBlock or issuance APIs.
// No complete-range coverage, audit assurance, reward or block-weight claim.
ReservedScienceCheck VerifyNativeReservedScience(const CCoinsView&,const CBlockIndex&,const ChainRangePolicy&,
    const NativeCandidateReservation&,const ScientificFactContext&,std::span<const unsigned char> signature,
    const EncodedCommittedGapEvidence&,ScriptWorkBudget);
// Read-only metadata preparation ONLY. Native block/transaction validation
// must succeed before a caller may stage this in its private coins child.
// Exact current reservation + script proof + GLOBAL fact identity, no funded
// coin prerequisite. This does not admit the block, mint/pay coins or approve
// target, candidate allocation rules, chain work or a consensus profile.
FactHistoryStep PrepareNativeReservedScienceForward(const CCoinsView&,const CBlockIndex& parent,
    const CBlockIndex& proposed,const ChainRangePolicy&,std::span<const VerifiedNativeReservedScience>);

struct ProfiledScienceCheck;
class VerifiedNativeProfiledScience {
    uint256 m_policy;VerifiedNativeReservedScience m_science;
    VerifiedNativeProfiledScience(uint256 policy,VerifiedNativeReservedScience science)
        :m_policy(policy),m_science(std::move(science)){}
    friend ProfiledScienceCheck VerifyNativeProfiledScience(const CCoinsView&,const CBlockIndex&,
        const ChainRangePolicy&,const ChainSciencePolicy&,const NativeCandidateReservation&,const Digest&,
        std::span<const unsigned char>,const EncodedCommittedGapEvidence&,ScriptWorkBudget);
public:
    VerifiedNativeProfiledScience(const VerifiedNativeProfiledScience&)=default;
    VerifiedNativeProfiledScience& operator=(const VerifiedNativeProfiledScience&)=default;
    const uint256& Policy()const{return m_policy;}
    const VerifiedNativeReservedScience& Science()const{return m_science;}
};
struct ProfiledScienceCheck {
    ReservedScienceVerdict verdict;std::optional<VerifiedNativeProfiledScience> candidate;
    std::optional<CommittedGapVerdict> evidence;
};
// Wallet challenge preparation only. Returned DTO is not trusted by verifier.
std::optional<ScientificFactContext> NativeProfiledWorkContext(const CCoinsView&,const CBlockIndex&,
    const ChainRangePolicy&,const ChainSciencePolicy&,const NativeCandidateReservation&,const Digest& report_root);
// NO sender-chosen target/segmentation/verifier limits. Rebuilds context from
// BOTH actual native genesis policies internally. Chain-authenticity is still
// NOT human approval, retarget, block admission or production security.
ProfiledScienceCheck VerifyNativeProfiledScience(const CCoinsView&,const CBlockIndex&,
    const ChainRangePolicy&,const ChainSciencePolicy&,const NativeCandidateReservation&,const Digest&,
    std::span<const unsigned char>,const EncodedCommittedGapEvidence&,ScriptWorkBudget);
FactHistoryStep PrepareNativeProfiledScienceForward(const CCoinsView&,const CBlockIndex& parent,const CBlockIndex& proposed,
    const ChainRangePolicy&,const ChainSciencePolicy&,std::span<const VerifiedNativeProfiledScience>);
}

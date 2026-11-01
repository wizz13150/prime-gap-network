// MIT. UNAPPROVED composition candidate, no ticket/consensus activation.
#pragma once
#include <pgn/committed_gap.h>
#include <pgn/fact_history.h>
#include <pgn/utxo_owner_authorization.h>
#include <pgn/script_work_proof.h>

class CBlockIndex;
namespace pgn::core {
struct ScientificFactContext {
    uint256 chain;
    RangeContract range;
    Digest report_root;
    CommittedGapPolicy policy;
    EncodingLimits proof_encoding;
};
// Hashes once per submitted report/context, never a new GPU/hash-PoW loop.
// Commits ALL science-verifier limits/target, range/ticket, report root and chain.
std::optional<uint256> ScientificWorkIdentity(const ScientificFactContext&);
enum class ScientificFactVerdict { Verified, InvalidContext, OwnerMismatch, StaleOwner, InvalidEvidence };
struct ScientificFactCheck;
class VerifiedScientificFact {
    uint256 m_key;
    UtxoOwnerContext m_owner;
    CScript m_script;
    VerifiedScientificFact(uint256 key,const UtxoOwnerAuthorization& owner)
        :m_key(key),m_owner(owner.Context()),m_script(owner.Script()){}
    friend ScientificFactCheck VerifyScientificFact(const CCoinsView&,const UtxoOwnerAuthorization&,
        const ScientificFactContext&,const EncodedCommittedGapEvidence&);
public:
    const uint256& Key() const { return m_key; }
    const UtxoOwnerContext& Owner() const { return m_owner; }
    const CScript& OwnerScript() const { return m_script; }
};
struct ScientificFactCheck {
    ScientificFactVerdict verdict;
    std::optional<CommittedGapVerdict> evidence_verdict;
    std::optional<VerifiedScientificFact> fact;
};
// Caller holds native parent-state lock. Uses the existing EXACT supplied-
// certificate verifier and actual Core SHA256, never a caller-selected hash.
// Chain-sourced ticket/root/policy, eligibility, report completeness, merit
// class, monetary reward, difficulty and approved wire rules remain UNPAID.
ScientificFactCheck VerifyScientificFact(const CCoinsView&,const UtxoOwnerAuthorization&,
    const ScientificFactContext&,const EncodedCommittedGapEvidence&);
struct ScriptScientificCheck;
struct ScientificEvidenceCheck;
// Immutable process-local mathematics, minted ONLY by the exact verifier.
// No signature, current parent, ticket eligibility or chain credit is cached.
class VerifiedScientificEvidence {
    uint256 m_chain,m_work,m_key;
    VerifiedScientificEvidence(uint256 chain,uint256 work,uint256 key):m_chain(chain),m_work(work),m_key(key){}
    friend ScientificEvidenceCheck VerifyScientificEvidence(const ScientificFactContext&,const EncodedCommittedGapEvidence&);
    friend ScriptScientificCheck VerifyScriptScientificCandidate(const uint256&,const VerifiedScriptWork&,
        const ScientificFactContext&,const EncodedCommittedGapEvidence&);
public:
    const uint256& Chain()const{return m_chain;}
    const uint256& Work()const{return m_work;}
    const uint256& Key()const{return m_key;}
};
struct ScientificEvidenceCheck {
    ScientificFactVerdict verdict;
    std::optional<CommittedGapVerdict> evidence_verdict;
    std::optional<VerifiedScientificEvidence> evidence;
};
ScientificEvidenceCheck VerifyScientificEvidence(const ScientificFactContext&,const EncodedCommittedGapEvidence&);
class VerifiedScriptScientificCandidate {
    VerifiedScientificEvidence m_science;VerifiedScriptWork m_owner;
    VerifiedScriptScientificCandidate(VerifiedScientificEvidence science,VerifiedScriptWork owner):m_science(std::move(science)),m_owner(std::move(owner)){}
    friend ScriptScientificCheck VerifyScriptScientificCandidate(const uint256&,const VerifiedScriptWork&,
        const ScientificFactContext&,const EncodedCommittedGapEvidence&);
    friend ScriptScientificCheck BindScientificEvidenceToScript(const uint256&,const VerifiedScriptWork&,const VerifiedScientificEvidence&);
public:
    const uint256& Key()const{return m_science.Key();}
    const VerifiedScientificEvidence& Science()const{return m_science;}
    const VerifiedScriptWork& OwnerProof()const{return m_owner;}
};
struct ScriptScientificCheck {
    ScientificFactVerdict verdict;
    std::optional<CommittedGapVerdict> evidence_verdict;
    std::optional<VerifiedScriptScientificCandidate> candidate;
};
// Same independent mathematical verifier/global key, but NO funded-coin
// prerequisite. expected_parent is caller-provided and NOT authenticated here.
// This distinct candidate CANNOT enter PrepareScientificFactForward. Actual
// chain-sourced range/ticket/parent/policy, bootstrap availability, eligibility,
// complete-report audit and block/reward admission remain separate/unpaid.
ScriptScientificCheck VerifyScriptScientificCandidate(const uint256& expected_parent,const VerifiedScriptWork&,
    const ScientificFactContext&,const EncodedCommittedGapEvidence&);
// A new parent needs a fresh native script proof, not another mathematical
// proof/search. This function cannot parse a certificate, hash a report or
// touch a device. It still grants NO ticket/chain eligibility.
ScriptScientificCheck BindScientificEvidenceToScript(const uint256& expected_parent,const VerifiedScriptWork&,
    const VerifiedScientificEvidence&);
enum class ScientificParentVerdict {Unenrolled,StaleParent,WrongChain,AlreadyClaimed,UnclaimedAtParent};
// READONLY native duplicate/freshness preflight under the caller's parent-state
// lock. UnclaimedAtParent means ONLY that: not a ticket, admission or reward.
// Storage exceptions propagate; corruption is never converted into absence.
ScientificParentVerdict InspectScientificCandidateParent(const CCoinsView&,const VerifiedScriptScientificCandidate&);
// READ-only preparation against a real native direct-parent index. Pass the
// unchanged PARENT view, not a post-transaction view. Stage only after native
// block validation succeeds in its private child; this is NOT ConnectBlock.
FactHistoryStep PrepareScientificFactForward(const CCoinsView&,const CBlockIndex&,
    std::span<const VerifiedScientificFact>);
} // namespace pgn::core

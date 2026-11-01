// MIT. Native full-block proof ingress candidate; NOT activated consensus.
#pragma once
#include <pgn/candidate_reserved_science.h>
class CBlock;
namespace pgn::core {
inline constexpr std::string_view BlockScienceEnvelopeTag{"PGNPROOF-UNAPPROVED-1"};
// Untrusted sender bytes, not a ticket, signature, mathematics or block seal.
struct CandidateBlockScienceProof {
    uint256 request;Digest report_root;Bytes signature;EncodedCommittedGapEvidence evidence;
};
struct NativeBlockScienceBudget {
    RangePolicyReadBudget policy;RangeScheduleBudget schedule;ScriptWorkBudget signature;
    uint32_t maximum_coinbase_outputs;uint64_t maximum_script_bytes,maximum_payload_bytes;
};
std::optional<CScript> BuildCandidateBlockScienceEnvelope(const CandidateBlockScienceProof&,const NativeBlockScienceBudget&);
// Recognition ONLY (including nonminimal pushes), never proof acceptance.
// An enrolled connection must not silently discard a known proof domain when
// its caller omitted the opaque whole-body verification seal.
bool HasNativeCandidateBlockScienceEnvelope(const CBlock&);
enum class NativeBlockScienceVerdict {VerifiedCandidate,InvalidBlock,MissingProof,DuplicateProof,
    InvalidEncoding,BudgetExceeded,NativeHistoryHeld,ScienceRejected};
struct NativeBlockScienceCheck;
class VerifiedNativeBlockScience {
    uint256 m_block,m_parent,m_source;size_t m_transactions;
    ChainRangePolicy m_ranges;ChainSciencePolicy m_policy;VerifiedNativeProfiledScience m_science;
    VerifiedNativeBlockScience(uint256 block,uint256 parent,uint256 source,size_t transactions,ChainRangePolicy ranges,
        ChainSciencePolicy policy,VerifiedNativeProfiledScience science)
        :m_block(block),m_parent(parent),m_source(source),m_transactions(transactions),m_ranges(ranges),m_policy(policy),m_science(science){}
    friend NativeBlockScienceCheck VerifyNativeCandidateBlockScience(const CCoinsView&,const CBlockIndex&,
        const NativeRequestHistoryArchive&,const CBlock&,const NativeBlockScienceBudget&);
    friend FactHistoryStep PrepareNativeBlockScienceForward(const CCoinsView&,const CBlockIndex&,
        const CBlockIndex&,const VerifiedNativeBlockScience&);
    friend TicketSeedCheck DeriveScientificTicketSeed(const CCoinsView&,const CBlockIndex&,
        const ConfirmedTicketRequest&,uint32_t,const VerifiedNativeBlockScience&);
public:
    VerifiedNativeBlockScience(const VerifiedNativeBlockScience&)=default;
    VerifiedNativeBlockScience& operator=(const VerifiedNativeBlockScience&)=default;
    const uint256& Block()const{return m_block;}
    bool AdaptiveTarget()const{return !m_policy.EffectiveParent().IsNull();}
    // Canonical PAID mathematical material only. No block nonce/time/root,
    // signature bytes, arbitrary transaction ordering or report metadata.
    // This removes FREE header rerolls; choosing/withholding different valid
    // scientific facts is still an UNPAID bias/liveness analysis obligation.
    // NOT an unbiased beacon or a replacement for the bootstrap source.
    const uint256& SourceMaterial()const{return m_source;}
    const VerifiedNativeProfiledScience& Science()const{return m_science;}
};
struct NativeBlockScienceCheck {
    NativeBlockScienceVerdict verdict;std::optional<VerifiedNativeBlockScience> candidate;
    std::optional<ReservedScienceVerdict> science;
};
// cs_main held. WHOLE actual candidate block, exact parent and Merkle root,
// one canonical zero-value coinbase proof. Derives BOTH policies from the
// authenticated native genesis and owner/range/ticket from COMPLETE history.
// Sender supplies no target, verifier limits, range, miner or reward address.
// This validates supplied science, NOT legacy/PGN header work, difficulty,
// full-range coverage/audits, transactions, issuance or ConnectBlock admission.
// No mutation, device search or activation. Full history replay cost remains
// explicit; do not put this candidate in a production per-block hot path.
NativeBlockScienceCheck VerifyNativeCandidateBlockScience(const CCoinsView&,const CBlockIndex&,
    const NativeRequestHistoryArchive&,const CBlock&,const NativeBlockScienceBudget&);
// Read-only cold reconstruction. No previous in-memory science seal or wallet
// key is accepted as input. Source must be on the ACTUAL current native branch,
// with its exact retained forward and still-published global claim. Recheck
// full source body/proof against a narrowly masked PRE-source fact view, then
// compare the independently reconstructed forward with native retained bytes.
// Missing/pruned history holds; DB errors propagate. No reward, startup or
// consensus activation, and no assertion of efficient whole-history replay.
NativeBlockScienceCheck RestoreNativePublishedBlockScience(const CCoinsView&,
    const CBlockIndex& current_tip,const CBlockIndex& source,
    const NativeRequestHistoryArchive&,const CBlock&,const NativeBlockScienceBudget&);
// Metadata ONLY, bound to the exact checked block body/hash/transaction count.
// Stage in a private child ONLY AFTER actual native block validation succeeds.
FactHistoryStep PrepareNativeBlockScienceForward(const CCoinsView&,const CBlockIndex&,
    const CBlockIndex&,const VerifiedNativeBlockScience&);
}

// MIT. Chain-authenticated candidate verifier policy, NOT adopted consensus.
#pragma once
#include <pgn/chain_range_policy.h>
#include <pgn/scientific_fact.h>
namespace pgn::core {
inline constexpr std::string_view SciencePolicyEnvelopeTag{"PGNSCIENCE-POLICY-UNAPPROVED-1"};
struct ScienceVerificationDefinition {
    uint32_t segments;CommittedGapPolicy evidence;EncodingLimits proof;
};
struct ChainSciencePolicyCheck;
class ChainSciencePolicy {
    uint256 m_chain,m_genesis,m_id,m_effective_parent;ScienceVerificationDefinition m_definition;
    ChainSciencePolicy(uint256 chain,uint256 genesis,uint256 id,ScienceVerificationDefinition definition,uint256 effective_parent={})
        :m_chain(chain),m_genesis(genesis),m_id(id),m_effective_parent(effective_parent),m_definition(std::move(definition)){}
    friend ChainSciencePolicyCheck ReadChainSciencePolicy(const CCoinsView&,const CBlockIndex&,
        const NativeRequestHistoryArchive&,RequestHistoryArchiveBudget,RangePolicyReadBudget);
public:
    ChainSciencePolicy(const ChainSciencePolicy&)=default;
    ChainSciencePolicy& operator=(const ChainSciencePolicy&)=default;
    const uint256& Chain()const{return m_chain;}
    const uint256& Genesis()const{return m_genesis;}
    const uint256& Id()const{return m_id;}
    const uint256& EffectiveParent()const{return m_effective_parent;}
    const ScienceVerificationDefinition& Definition()const{return m_definition;}
};
struct ChainSciencePolicyCheck {ChainRangePolicyVerdict verdict;std::optional<ChainSciencePolicy> policy;};
// Pure payload validation only, never a native admission/configuration seal.
std::optional<ScienceVerificationDefinition> DecodeCandidateScienceVerificationDefinitionPayload(const Bytes&,RangePolicyReadBudget);
std::optional<CScript> BuildCandidateSciencePolicyEnvelope(const ScienceVerificationDefinition&,RangePolicyReadBudget);
ChainSciencePolicyCheck ReadChainSciencePolicy(const CCoinsView&,const CBlockIndex&,
    const NativeRequestHistoryArchive&,RequestHistoryArchiveBudget,RangePolicyReadBudget);
bool ChainSciencePolicyOnBranch(const CCoinsView&,const CBlockIndex&,const ChainSciencePolicy&);
// This fixes the initial candidate verifier configuration only: no retarget,
// proof-weight/fork rule, economics, coverage or approved mainnet profile.
}

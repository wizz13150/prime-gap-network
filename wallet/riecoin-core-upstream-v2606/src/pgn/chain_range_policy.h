// MIT. Candidate genesis-committed definition, NOT adopted mainnet policy.
#pragma once
#include <pgn/request_history_archive.h>
#include <pgn/sparse_range_pool.h>
#include <string_view>
namespace pgn::core {
inline constexpr std::string_view RangePolicyEnvelopeTag{"PGNRANGE-POLICY-UNAPPROVED-1"};
struct RangePoolDefinition {
    Integer origin,width,slots;uint32_t seed_delay,lease_blocks;
    uint32_t selection_window{};
    std::optional<Integer> limit{};
};
struct RangePolicyReadBudget {
    uint32_t maximum_outputs,maximum_integer_bytes;
    uint64_t maximum_script_bytes,maximum_payload_bytes;
};
// Candidate encoder for a zero-valued genesis output. Does not create a
// genesis, approve parameters, initialize a node or grant chain authority.
// Pure payload validation only, never a native admission/configuration seal.
std::optional<RangePoolDefinition> DecodeCandidateRangePoolDefinitionPayload(const Bytes&,RangePolicyReadBudget);
std::optional<CScript> BuildCandidateRangePolicyEnvelope(const RangePoolDefinition&,RangePolicyReadBudget);
enum class ChainRangePolicyVerdict {Confirmed,MissingHistory,MissingConfiguration,InvalidParent,
    InvalidConfiguration,DuplicateConfiguration,BudgetExceeded};
struct ChainRangePolicyCheck;
class ChainRangePolicy {
    uint256 m_chain,m_genesis,m_id;RangePoolDefinition m_definition;
    ChainRangePolicy(uint256 chain,uint256 genesis,uint256 id,RangePoolDefinition definition)
        :m_chain(chain),m_genesis(genesis),m_id(id),m_definition(std::move(definition)){}
    friend ChainRangePolicyCheck ReadChainRangePolicy(const CCoinsView&,const CBlockIndex&,
        const NativeRequestHistoryArchive&,RequestHistoryArchiveBudget,RangePolicyReadBudget);
public:
    // Copy semantics preserve the complete immutable definition. No default
    // constructor and no moved-from empty specification retaining a seal.
    ChainRangePolicy(const ChainRangePolicy&)=default;
    ChainRangePolicy& operator=(const ChainRangePolicy&)=default;
    const uint256& Chain()const{return m_chain;}
    const uint256& Genesis()const{return m_genesis;}
    const uint256& Id()const{return m_id;}
    const RangePoolDefinition& Definition()const{return m_definition;}
};
struct ChainRangePolicyCheck {ChainRangePolicyVerdict verdict;std::optional<ChainRangePolicy> policy;};
struct GenesisPolicyPayloadRead {
    ChainRangePolicyVerdict verdict;std::optional<VerifiedNativeRequestBlock> genesis;Bytes payload;
};
// Shared full-coinbase/native-proof/canonical-push gate. Public DTO is NOT a
// sealed configuration; semantic factories call this reader internally.
GenesisPolicyPayloadRead ReadCandidateGenesisPolicyPayload(const CCoinsView&,const CBlockIndex&,
    const NativeRequestHistoryArchive&,RequestHistoryArchiveBudget,RangePolicyReadBudget,std::string_view tag);
// cs_main held. Reads the WHOLE authenticated genesis coinbase from actual
// current ancestry. Exactly one canonical zero-value candidate configuration.
// Absence never imports an upstream/Riecoin default or caller pool definition.
// Authentication is to THIS native chain, not human approval or network launch.
ChainRangePolicyCheck ReadChainRangePolicy(const CCoinsView&,const CBlockIndex&,
    const NativeRequestHistoryArchive&,RequestHistoryArchiveBudget,RangePolicyReadBudget);
bool ChainRangePolicyOnBranch(const CCoinsView&,const CBlockIndex&,const ChainRangePolicy&);
}

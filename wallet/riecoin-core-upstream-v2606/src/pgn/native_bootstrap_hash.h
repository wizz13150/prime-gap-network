// MIT. UNAPPROVED native-header bootstrap WORK component, not admission.
// No CPU search, GPU actuator, genesis creation or startup approval.
#pragma once
#include <pgn/chain_range_policy.h>
#include <chain.h>
#include <coins.h>
#include <consensus/merkle.h>
#include <hash.h>
#include <kernel/cs_main.h>
#include <primitives/block.h>
#include <streams.h>
#include <array>
#include <cstring>
namespace pgn::core {
inline constexpr std::string_view BootstrapHashPolicyEnvelopeTag{"PGNBOOT-HASH-UNAPPROVED-1"};
inline std::optional<arith_uint256> CanonicalBootstrapHashTarget(uint32_t bits){
    arith_uint256 target;bool negative{},overflow{};target.SetCompact(bits,&negative,&overflow);
    if(negative||overflow||target==0||target.GetCompact()!=bits)return {};
    return target;
}
inline std::optional<CScript> BuildCandidateBootstrapHashPolicyEnvelope(uint32_t bits,RangePolicyReadBudget budget){
    if(!CanonicalBootstrapHashTarget(bits)||!budget.maximum_outputs||!budget.maximum_integer_bytes||
       budget.maximum_payload_bytes<5||!budget.maximum_script_bytes)return {};
    Bytes bytes{1};for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<unsigned char>(bits>>(8*i)));
    CScript script=CScript{}<<OP_RETURN<<Bytes{BootstrapHashPolicyEnvelopeTag.begin(),BootstrapHashPolicyEnvelopeTag.end()}<<bytes;
    if(script.size()>budget.maximum_script_bytes)return {};
    return script;
}
struct ChainBootstrapHashPolicyCheck;
class ChainBootstrapHashPolicy {
    uint256 m_chain,m_genesis,m_id;uint32_t m_bits;
    ChainBootstrapHashPolicy(uint256 chain,uint256 genesis,uint256 id,uint32_t bits)
        :m_chain(chain),m_genesis(genesis),m_id(id),m_bits(bits){}
    friend ChainBootstrapHashPolicyCheck ReadChainBootstrapHashPolicy(const CCoinsView&,const CBlockIndex&,
        const NativeRequestHistoryArchive&,RequestHistoryArchiveBudget,RangePolicyReadBudget);
public:
    ChainBootstrapHashPolicy(const ChainBootstrapHashPolicy&)=default;
    ChainBootstrapHashPolicy& operator=(const ChainBootstrapHashPolicy&)=default;
    const uint256& Chain()const{return m_chain;}
    const uint256& Genesis()const{return m_genesis;}
    const uint256& Id()const{return m_id;}
    uint32_t Bits()const{return m_bits;}
};
struct ChainBootstrapHashPolicyCheck {ChainRangePolicyVerdict verdict;std::optional<ChainBootstrapHashPolicy> policy;};
inline ChainBootstrapHashPolicyCheck ReadChainBootstrapHashPolicy(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& archive,RequestHistoryArchiveBudget archive_budget,RangePolicyReadBudget budget){
    const auto payload=ReadCandidateGenesisPolicyPayload(view,parent,archive,archive_budget,budget,BootstrapHashPolicyEnvelopeTag);
    if(!payload.genesis)return {payload.verdict,{}};
    if(payload.payload.size()!=5||payload.payload[0]!=1)return {ChainRangePolicyVerdict::InvalidConfiguration,{}};
    uint32_t bits{};for(unsigned i=0;i<4;++i)bits|=uint32_t(payload.payload[i+1])<<(8*i);
    if(!CanonicalBootstrapHashTarget(bits))return {ChainRangePolicyVerdict::InvalidConfiguration,{}};
    HashWriter id{TaggedHash("PGN/UNAPPROVED/NATIVE-BOOTSTRAP-HASH-POLICY/1")};
    id<<payload.genesis->Chain()<<payload.genesis->Block()<<payload.payload;
    return {ChainRangePolicyVerdict::Confirmed,ChainBootstrapHashPolicy{payload.genesis->Chain(),payload.genesis->Block(),id.GetHash(),bits}};
}
inline bool BootstrapHashPolicyOnBranch(const CCoinsView& view,const CBlockIndex& parent,const ChainBootstrapHashPolicy& policy){
    AssertLockHeld(cs_main);const auto head=view.GetPgnFactHead();
    if(!head||!parent.phashBlock||parent.nHeight<0||!parent.IsValid(BLOCK_VALID_SCRIPTS)||
       head->chain!=policy.Chain()||head->tip!=parent.GetBlockHash()||view.GetBestBlock()!=head->tip)return false;
    const auto* genesis=parent.GetAncestor(0);
    return genesis&&genesis->phashBlock&&genesis->IsValid(BLOCK_VALID_SCRIPTS)&&genesis->GetBlockHash()==policy.Genesis()&&
        CanonicalBootstrapHashTarget(policy.Bits()).has_value();
}
// Explicit native-v2606 serialization bridge, NOT an adopted nonce rule.
// Low16==2 selects its canonical GetHash branch, avoiding legacy time/nBits
// swapping. Counter occupies the next64 bits; all higher bits remain zero.
// Thus the actual Core block ID equals SHA256d of its 112 serialized bytes.
inline arith_uint256 NativeBootstrapNonce(uint64_t counter){arith_uint256 value{counter};value<<=16;return value+2;}
inline bool NativeBootstrapBodyBound(const CCoinsView& view,const CBlockIndex& parent,
    const ChainBootstrapHashPolicy& policy,const CBlock& block){
    if(!BootstrapHashPolicyOnBranch(view,parent,policy)||block.hashPrevBlock!=parent.GetBlockHash()||block.nBits!=policy.Bits()||
       block.vtx.empty()||!block.vtx.front()||!block.vtx.front()->IsCoinBase())return false;
    for(size_t i=1;i<block.vtx.size();++i)if(!block.vtx[i]||block.vtx[i]->IsCoinBase())return false;
    bool mutated{};return BlockMerkleRoot(block,&mutated)==block.hashMerkleRoot&&!mutated;
}
// Public work DTO only: never a valid block, ticket, target choice or lease.
struct NativeBootstrapHashPlan {uint256 chain,parent,policy,target;std::array<unsigned char,112> header;};
inline std::optional<NativeBootstrapHashPlan> PlanNativeBootstrapHash(const CCoinsView& view,const CBlockIndex& parent,
    const ChainBootstrapHashPolicy& policy,const CBlock& block){
    AssertLockHeld(cs_main);
    if(block.nNonce!=0||!NativeBootstrapBodyBound(view,parent,policy,block))return {};
    CBlockHeader header{static_cast<const CBlockHeader&>(block)};header.nNonce=NativeBootstrapNonce(0);
    DataStream serialized;serialized<<header;
    if(serialized.size()!=112)return {};
    NativeBootstrapHashPlan plan{policy.Chain(),parent.GetBlockHash(),policy.Id(),ArithToUint256(*CanonicalBootstrapHashTarget(policy.Bits())),{}};
    std::memcpy(plan.header.data(),serialized.data(),plan.header.size());return plan;
}
enum class NativeBootstrapHashVerdict {VerifiedHeaderWork,InvalidNativeBody,InvalidNonceFamily,InsufficientWork};
struct NativeBootstrapHashCheck;
class VerifiedNativeBootstrapHash {
    uint256 m_chain,m_parent,m_policy,m_block;uint64_t m_counter;
    VerifiedNativeBootstrapHash(uint256 chain,uint256 parent,uint256 policy,uint256 block,uint64_t counter)
        :m_chain(chain),m_parent(parent),m_policy(policy),m_block(block),m_counter(counter){}
    friend NativeBootstrapHashCheck VerifyNativeBootstrapHash(const CCoinsView&,const CBlockIndex&,
        const ChainBootstrapHashPolicy&,const CBlock&);
public:
    VerifiedNativeBootstrapHash(const VerifiedNativeBootstrapHash&)=default;
    VerifiedNativeBootstrapHash& operator=(const VerifiedNativeBootstrapHash&)=default;
    const uint256& Block()const{return m_block;}
    const uint256& Policy()const{return m_policy;}
    uint64_t Counter()const{return m_counter;}
};
struct NativeBootstrapHashCheck {NativeBootstrapHashVerdict verdict;std::optional<VerifiedNativeBootstrapHash> proof;};
inline NativeBootstrapHashCheck VerifyNativeBootstrapHash(const CCoinsView& view,const CBlockIndex& parent,
    const ChainBootstrapHashPolicy& policy,const CBlock& block){
    AssertLockHeld(cs_main);
    if(!NativeBootstrapBodyBound(view,parent,policy,block))return {NativeBootstrapHashVerdict::InvalidNativeBody,{}};
    if((block.nNonce&65535)!=2||(block.nNonce>>80)!=0)return {NativeBootstrapHashVerdict::InvalidNonceFamily,{}};
    const auto hash=block.GetHash();
    // Standard native uint256/arith_uint256 ordering, NOT raw digest byte order.
    if(UintToArith256(hash)>*CanonicalBootstrapHashTarget(policy.Bits()))return {NativeBootstrapHashVerdict::InsufficientWork,{}};
    return {NativeBootstrapHashVerdict::VerifiedHeaderWork,
        VerifiedNativeBootstrapHash{policy.Chain(),parent.GetBlockHash(),policy.Id(),hash,(block.nNonce>>16).GetLow64()}};
}
// This seal CANNOT enter scientific ConnectBlock APIs. Phase eligibility,
// signed-request admission, seed grinding, first/recurring ticket liveness,
// difficulty/fork weight/economics and production safety remain UNPAID.
// No claim that an easy unit-test target is mined work or mainnet security.
}

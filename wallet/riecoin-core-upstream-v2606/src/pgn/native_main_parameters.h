// Copyright (c) 2026 The PGN developers. MIT.
#pragma once
#include <kernel/chainparams.h>
#include <pgn/approved_currency.h>
#include <pgn/candidate_block_bootstrap.h>
#include <pgn/chain_monetary_policy.h>
#include <pgn/native_retarget_policy.h>
#include <array>
#include <set>
#include <map>
#include <limits>
namespace pgn::core {
// Explicit immutable constructor inputs, not chain selection or an admission
// seal. No economics, HRP, network magic or peer is inferred from Riecoin.
struct NativeMainParameterDefinition {
    CBlock genesis;
    uint256 pinned_genesis;
    MessageStartChars magic;
    uint16_t peer_port, rpc_port;
    NativeAddressPolicy addresses;
    std::array<Bytes,2> hd_versions;
    ExactAmountTextBudget text;
    AtomicFeeRatePolicy fees;
    RangePolicyReadBudget policy;
};
inline bool NativeMainParameterDefinitionValid(const NativeMainParameterDefinition& d) {
    if(d.peer_port<1024||d.rpc_port<1024||d.peer_port==d.rpc_port||
       d.pinned_genesis.IsNull()||d.genesis.GetHash()!=d.pinned_genesis||
       !d.genesis.hashPrevBlock.IsNull()||d.genesis.nVersion!=4||!d.genesis.nTime||
       ValidateNativeAddressPolicy(d.addresses)!=NativeAddressPolicyStatus::Ready||
       d.addresses.hrp=="ric"||d.addresses.hrp=="tric"||d.addresses.hrp=="rric"||
       ValidateMonetaryContext(ApprovedPgnMonetaryContext(d.text))!=ExactAmountError::None||
       d.fees.max_atomic_per_kvbyte<=0||d.fees.max_atomic_per_kvbyte>PGN_MAX_SUPPLY_ATOMIC||
       validateExactAmountPolicy(ExactAmountPolicy{PGN_DECIMAL_PLACES,d.fees.max_atomic_per_kvbyte},d.fees.text_budget)!=ExactAmountError::None)
        return false;
    const std::array<MessageStartChars,4> excluded{{MessageStartChars{},MessageStartChars{0xfc,0xbc,0xb2,0xdb},
        MessageStartChars{0x0e,0x09,0x11,0x05},MessageStartChars{0xfa,0xbf,0xb5,0xda}}};
    if(std::find(excluded.begin(),excluded.end(),d.magic)!=excluded.end()||
       d.hd_versions[0].size()!=4||d.hd_versions[1].size()!=4||d.hd_versions[0]==d.hd_versions[1])return false;
    const auto bootstrap=ReadPinnedGenesisBootstrapBits(d.genesis,d.pinned_genesis,d.policy);
    const auto work=ReadPinnedNativeWorkPolicy(d.genesis,d.pinned_genesis,d.policy);
    const auto target=ReadPinnedNativeRetarget(d.genesis,d.pinned_genesis,d.policy);
    if(!bootstrap||*bootstrap!=d.genesis.nBits||!work.policy||!target.policy||
       !QuoteAdaptiveScientificWork(work.policy->scientific,target.policy->maximum_q16-target.policy->minimum_q16))return false;
    // Structural uniqueness here; actual native policy readers independently
    // validate ALL semantic payloads against the authenticated genesis before
    // child admission. This factory does not promote caller payload DTOs.
    const std::array<std::string_view,6> required{RangePolicyEnvelopeTag,SciencePolicyEnvelopeTag,
        MonetaryPolicyEnvelopeTag,BootstrapHashPolicyEnvelopeTag,NativeWorkPolicyTag,NativeRetargetPolicyTag};
    std::map<std::string,Bytes> seen;
    for(const auto& out:d.genesis.vtx.front()->vout) {
        auto at=out.scriptPubKey.begin();opcodetype op;Bytes tag,payload;
        if(out.nValue!=0||!out.scriptPubKey.GetOp(at,op)||op!=OP_RETURN||
           !out.scriptPubKey.GetOp(at,op,tag)||!out.scriptPubKey.GetOp(at,op,payload)||at!=out.scriptPubKey.end()||
           payload.empty()||payload.size()>d.policy.maximum_payload_bytes||
           out.scriptPubKey!=(CScript{}<<OP_RETURN<<tag<<payload))return false;
        if(!seen.emplace(std::string{tag.begin(),tag.end()},payload).second)return false;
    }
    for(const auto tag:required)if(!seen.contains(std::string{tag}))return false;
    const auto range=DecodeCandidateRangePoolDefinitionPayload(seen.at(std::string{RangePolicyEnvelopeTag}),d.policy);
    const auto science=DecodeCandidateScienceVerificationDefinitionPayload(seen.at(std::string{SciencePolicyEnvelopeTag}),d.policy);
    const auto money=DecodeCandidateMonetaryPolicyPayload(seen.at(std::string{MonetaryPolicyEnvelopeTag}));
    return range&&science&&money&&
        science->evidence.target_numerator*65536==Integer(target.policy->initial_q16)*science->evidence.target_denominator;
}
inline std::unique_ptr<const CChainParams> CreateNativeParameters(const NativeMainParameterDefinition& d,ChainType chain_type) {
    if(!NativeMainParameterDefinitionValid(d)){
        const auto boot=ReadPinnedGenesisBootstrapBits(d.genesis,d.pinned_genesis,d.policy);
        const auto work=ReadPinnedNativeWorkPolicy(d.genesis,d.pinned_genesis,d.policy);
        const auto target=ReadPinnedNativeRetarget(d.genesis,d.pinned_genesis,d.policy);
        throw std::invalid_argument("Native PGN parameter definition rejected: genesis="+d.genesis.GetHash().GetHex()+
            " bootstrap="+std::to_string(bool(boot))+" work="+std::to_string(bool(work.policy))+
            " target="+std::to_string(bool(target.policy))+" outputs="+std::to_string(d.genesis.vtx.front()->vout.size()));
    }
    class NativeMain final:public CChainParams {
    public:
        explicit NativeMain(const NativeMainParameterDefinition& d,ChainType chain_type) {
            m_chain_type=chain_type;genesis=d.genesis;
            consensus.amount_rules=ApprovedPgnAmountRules();
            consensus.hashGenesisBlock=d.pinned_genesis;consensus.hashGenesisBlockForPoW=genesis.GetHashForPoW();
            // Historical tuple/height-subsidy functions are not native proof or
            // issuance authority. Their complete metadata never stays uninitialized.
            consensus.nSubsidyHalvingInterval=std::numeric_limits<int>::max();consensus.fork1Height=0;consensus.fork2Height=0;
            consensus.MinBIP9WarningHeight=0;consensus.powAcceptedPatterns.clear();
            const auto target=ReadPinnedNativeRetarget(genesis,d.pinned_genesis,d.policy).policy.value();
            const auto work=ReadPinnedNativeWorkPolicy(genesis,d.pinned_genesis,d.policy).policy.value();
            consensus.pgn_native_proof=Consensus::NativeProofParameters{target.initial_q16,target.minimum_q16,target.maximum_q16,
                target.spacing_seconds,target.half_life_seconds,genesis.nBits,ArithToUint256(work.scientific)};
            consensus.nBitsMin=target.minimum_q16;consensus.nPowTargetSpacing=target.spacing_seconds;
            consensus.fPowNoRetargeting=false;consensus.nMinimumChainWork.SetNull();
            consensus.vDeployments={};
            m_pgn_monetary=ApprovedPgnMonetaryContext(d.text);m_pgn_atomic_fee=d.fees;m_pgn_native_address=d.addresses;
            pchMessageStart=d.magic;nDefaultPort=d.peer_port;nPruneAfterHeight=100000;
            m_assumed_blockchain_size=0;m_assumed_chain_state_size=0;
            vSeeds.clear();vFixedSeeds.clear();m_assumeutxo_data.clear();
            base58Prefixes[EXT_PUBLIC_KEY]=d.hd_versions[0];base58Prefixes[EXT_SECRET_KEY]=d.hd_versions[1];
            bech32_hrp=d.addresses.hrp;fDefaultConsistencyChecks=false;m_is_mockable_chain=false;
            checkpointData={{},{},0};chainTxData={genesis.nTime,1,0.0};
            CheckPgnParameterCoherence();
        }
    };
    return std::make_unique<const NativeMain>(d,chain_type);
}
inline std::unique_ptr<const CChainParams> CreateNativeMainParameters(const NativeMainParameterDefinition& d){
    return CreateNativeParameters(d,ChainType::MAIN);
}
inline std::unique_ptr<const CChainParams> CreateNativeTestnetParameters(const NativeMainParameterDefinition& d){
    return CreateNativeParameters(d,ChainType::TESTNET);
}
} // namespace pgn::core

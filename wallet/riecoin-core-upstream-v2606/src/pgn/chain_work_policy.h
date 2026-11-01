// Copyright (c) 2026 The PGN developers
// MIT. Candidate constant-science-target epoch work policy; no retarget approval.
#pragma once
#include <pgn/native_bootstrap_hash.h>
#include <limits>
#include <algorithm>
namespace pgn::core {
inline constexpr std::string_view NativeWorkPolicyTag{"PGNWORK-POLICY-CANDIDATE-1"};
struct NativeWorkDefinition {arith_uint256 scientific,bootstrap;};
inline bool NativeWorkDefinitionValid(const NativeWorkDefinition& d){
    // At most2^31 blocks in a signed32-bit height domain. One scientific unit
    // outranks the ENTIRE bootstrap-only count; all cumulative sums fit uint256.
    return d.bootstrap==1&&d.scientific>=(arith_uint256{1}<<32)&&d.scientific<=(~arith_uint256{}>>31);
}
inline std::optional<CScript> BuildCandidateNativeWorkEnvelope(const NativeWorkDefinition& d,RangePolicyReadBudget b){
    if(!NativeWorkDefinitionValid(d)||!b.maximum_outputs||b.maximum_payload_bytes<65)return {};
    Bytes payload{1};const auto scientific=ArithToUint256(d.scientific),bootstrap=ArithToUint256(d.bootstrap);
    payload.insert(payload.end(),scientific.begin(),scientific.end());payload.insert(payload.end(),bootstrap.begin(),bootstrap.end());
    CScript s=CScript{}<<OP_RETURN<<Bytes{NativeWorkPolicyTag.begin(),NativeWorkPolicyTag.end()}<<payload;
    if(s.size()>b.maximum_script_bytes)return {};
    return s;
}
enum class NativeWorkPolicyReadVerdict {Absent,Confirmed,Invalid,BudgetExceeded};
struct NativeWorkPolicyRead {NativeWorkPolicyReadVerdict verdict;std::optional<NativeWorkDefinition> policy;};
inline NativeWorkPolicyRead ReadPinnedNativeWorkPolicy(const CBlock& genesis,const uint256& expected,RangePolicyReadBudget b){
    if(!b.maximum_outputs||!b.maximum_script_bytes||b.maximum_payload_bytes<65)return {NativeWorkPolicyReadVerdict::BudgetExceeded,{}};
    if(genesis.vtx.size()!=1||!genesis.vtx.front()||!genesis.vtx.front()->IsCoinBase())return {NativeWorkPolicyReadVerdict::Invalid,{}};
    const auto& outputs=genesis.vtx.front()->vout;if(outputs.size()>b.maximum_outputs)return {NativeWorkPolicyReadVerdict::BudgetExceeded,{}};
    const Bytes wanted{NativeWorkPolicyTag.begin(),NativeWorkPolicyTag.end()};std::optional<Bytes> found;
    uint64_t remaining=b.maximum_script_bytes;
    for(const auto& out:outputs){
        const auto& s=out.scriptPubKey;if(s.size()>remaining)return {NativeWorkPolicyReadVerdict::BudgetExceeded,{}};remaining-=s.size();
        auto p=s.begin();opcodetype op;Bytes tag,payload;
        if(!s.GetOp(p,op)||op!=OP_RETURN||!s.GetOp(p,op,tag)||tag!=wanted)continue;
        if(found||out.nValue!=0||!s.GetOp(p,op,payload)||p!=s.end()||payload.size()!=65||payload[0]!=1||
           s!=(CScript{}<<OP_RETURN<<tag<<payload))return {NativeWorkPolicyReadVerdict::Invalid,{}};
        found=payload;
    }
    if(!found)return {NativeWorkPolicyReadVerdict::Absent,{}};
    bool mutation{};
    if(expected.IsNull()||genesis.GetHash()!=expected||BlockMerkleRoot(genesis,&mutation)!=genesis.hashMerkleRoot||mutation)
        return {NativeWorkPolicyReadVerdict::Invalid,{}};
    for(const auto& out:outputs)if(out.nValue!=0)return {NativeWorkPolicyReadVerdict::Invalid,{}};
    uint256 scientific,bootstrap;std::copy(found->begin()+1,found->begin()+33,scientific.begin());std::copy(found->begin()+33,found->end(),bootstrap.begin());
    const NativeWorkDefinition d{UintToArith256(scientific),UintToArith256(bootstrap)};
    if(!NativeWorkDefinitionValid(d))return {NativeWorkPolicyReadVerdict::Invalid,{}};
    return {NativeWorkPolicyReadVerdict::Confirmed,d};
}

// Absent policy is only compatibility for EXPLICIT PRIVATE legacy fixtures.
// Actual startup must require the committed definition; malformed/over-budget
// work metadata NEVER falls back to the old fixed unit.
inline std::optional<NativeWorkDefinition> ResolvePinnedNativeWork(
    const CBlock& genesis,const uint256& expected,RangePolicyReadBudget budget,const arith_uint256& private_legacy_unit){
    if(private_legacy_unit==0)return {};
    const auto read=ReadPinnedNativeWorkPolicy(genesis,expected,budget);
    if(read.verdict==NativeWorkPolicyReadVerdict::Absent)return NativeWorkDefinition{private_legacy_unit,private_legacy_unit};
    return read.policy;
}
}

// MIT. Candidate genesis codec/historical query. Not a deployed retarget rule.
#pragma once
#include <pgn/adaptive_merit_math.h>
#include <pgn/chain_work_policy.h>
#include <array>
#include <algorithm>
namespace pgn::core {
inline constexpr std::string_view NativeRetargetPolicyTag{"PGNTARGET-POLICY-CANDIDATE-1"};
inline Bytes NativeRetargetPayload(const MeritControllerDefinition& d){
    Bytes p{1};for(const auto v:std::array<uint32_t,5>{d.initial_q16,d.minimum_q16,d.maximum_q16,d.spacing_seconds,d.half_life_seconds})
        for(unsigned i=0;i<4;++i)p.push_back(static_cast<unsigned char>(v>>(8*i)));return p;
}
inline std::optional<CScript> BuildCandidateNativeRetargetEnvelope(const MeritControllerDefinition& d,RangePolicyReadBudget b){
    if(!MeritControllerValid(d)||!b.maximum_outputs||b.maximum_payload_bytes<21)return {};
    const auto p=NativeRetargetPayload(d);CScript s=CScript{}<<OP_RETURN<<Bytes{NativeRetargetPolicyTag.begin(),NativeRetargetPolicyTag.end()}<<p;
    if(s.size()>b.maximum_script_bytes)return {};return s;
}
inline std::optional<MeritControllerDefinition> DecodeNativeRetargetPayload(const Bytes& p){
    if(p.size()!=21||p[0]!=1)return {};
    std::array<uint32_t,5> f{};for(unsigned n=0;n<5;++n)for(unsigned i=0;i<4;++i)f[n]|=uint32_t(p[1+n*4+i])<<(8*i);
    const MeritControllerDefinition d{f[0],f[1],f[2],f[3],f[4]};
    return MeritControllerValid(d)&&NativeRetargetPayload(d)==p?std::optional{d}:std::nullopt;
}
struct NativeRetargetRead {NativeWorkPolicyReadVerdict verdict;std::optional<MeritControllerDefinition> policy;};
inline NativeRetargetRead ReadPinnedNativeRetarget(const CBlock& genesis,const uint256& expected,RangePolicyReadBudget b){
    if(!b.maximum_outputs||!b.maximum_script_bytes||b.maximum_payload_bytes<21)return {NativeWorkPolicyReadVerdict::BudgetExceeded,{}};
    if(genesis.vtx.size()!=1||!genesis.vtx.front()||!genesis.vtx.front()->IsCoinBase())return {NativeWorkPolicyReadVerdict::Invalid,{}};
    const auto& outputs=genesis.vtx.front()->vout;if(outputs.size()>b.maximum_outputs)return {NativeWorkPolicyReadVerdict::BudgetExceeded,{}};
    const Bytes wanted{NativeRetargetPolicyTag.begin(),NativeRetargetPolicyTag.end()};std::optional<Bytes> found;uint64_t remaining=b.maximum_script_bytes;
    for(const auto& out:outputs){
        const auto& s=out.scriptPubKey;if(s.size()>remaining)return {NativeWorkPolicyReadVerdict::BudgetExceeded,{}};remaining-=s.size();
        auto p=s.begin();opcodetype op;Bytes tag,data;
        if(!s.GetOp(p,op)||op!=OP_RETURN||!s.GetOp(p,op,tag)||tag!=wanted)continue;
        if(found||out.nValue!=0||!s.GetOp(p,op,data)||p!=s.end()||data.size()!=21||data[0]!=1||
           s!=(CScript{}<<OP_RETURN<<tag<<data))return {NativeWorkPolicyReadVerdict::Invalid,{}};
        found=data;
    }
    if(!found)return {NativeWorkPolicyReadVerdict::Absent,{}};
    bool mutation{};
    if(expected.IsNull()||genesis.GetHash()!=expected||BlockMerkleRoot(genesis,&mutation)!=genesis.hashMerkleRoot||mutation)
        return {NativeWorkPolicyReadVerdict::Invalid,{}};
    for(const auto& out:outputs)if(out.nValue!=0)return {NativeWorkPolicyReadVerdict::Invalid,{}};
    std::array<uint32_t,5> fields{};for(unsigned n=0;n<5;++n)for(unsigned i=0;i<4;++i)fields[n]|=uint32_t((*found)[1+n*4+i])<<(8*i);
    const MeritControllerDefinition d{fields[0],fields[1],fields[2],fields[3],fields[4]};
    if(!MeritControllerValid(d)||NativeRetargetPayload(d)!=*found)return {NativeWorkPolicyReadVerdict::Invalid,{}};
    return {NativeWorkPolicyReadVerdict::Confirmed,d};
}
enum class AdaptiveHeaderKind {Bootstrap,Scientific,Invalid};
inline AdaptiveHeaderKind ClassifyAdaptiveHeader(const CBlockIndex& index,const MeritControllerDefinition& d){
    if(index.nNonce==0)return index.nBits>=d.minimum_q16&&index.nBits<=d.maximum_q16?AdaptiveHeaderKind::Scientific:AdaptiveHeaderKind::Invalid;
    return (index.nNonce&65535)==2&&(index.nNonce>>80)==0?AdaptiveHeaderKind::Bootstrap:AdaptiveHeaderKind::Invalid;
}
struct HistoricalMeritQuote {uint32_t target_q16,scientific_count,adjusted_count,samples;};
// PRECONDITION: actual branch already validated the monotone Boot^K SCI^n
// phase and canonical header types. This query DOES NOT prove that premise or
// grant caller-supplied indices native authority. Native consumers must supply
// their own accepted/scoped-body-verified ancestry under cs_main.
inline std::optional<HistoricalMeritQuote> DeriveHistoricalMeritTarget(const CBlockIndex& parent,const MeritControllerDefinition& d){
    if(!MeritControllerValid(d)||parent.nHeight<0||!parent.phashBlock)return {};
    const auto* genesis=parent.GetAncestor(0);if(!genesis||genesis->GetBlockTime()<0||ClassifyAdaptiveHeader(*genesis,d)!=AdaptiveHeaderKind::Bootstrap)return {};
    const auto kind=ClassifyAdaptiveHeader(parent,d);if(kind==AdaptiveHeaderKind::Invalid)return {};
    if(kind==AdaptiveHeaderKind::Bootstrap){
        const auto median=parent.GetMedianTimePast();if(median<genesis->GetBlockTime())return {};
        const auto q=QuoteAnchoredMeritTarget(d,0,median-genesis->GetBlockTime());
        return q?std::optional{HistoricalMeritQuote{*q,0,0,0}}:std::nullopt;
    }
    uint32_t low=1,high=uint32_t(parent.nHeight);
    while(low<high){
        const uint32_t mid=low+(high-low)/2;const auto* node=parent.GetAncestor(int(mid));if(!node)return {};
        const auto mode=ClassifyAdaptiveHeader(*node,d);if(mode==AdaptiveHeaderKind::Invalid)return {};
        if(mode==AdaptiveHeaderKind::Scientific)high=mid;else low=mid+1;
    }
    const auto* first=parent.GetAncestor(int(low));
    if(!first||ClassifyAdaptiveHeader(*first,d)!=AdaptiveHeaderKind::Scientific||!first->pprev||
       ClassifyAdaptiveHeader(*first->pprev,d)!=AdaptiveHeaderKind::Bootstrap)return {};
    const uint32_t count=uint32_t(parent.nHeight)-low+1,samples=std::min(11U,count),adjusted=count-(samples-1)/2;
    std::array<int64_t,11> times{};
    for(uint32_t n=0;n<samples;++n){
        const auto* node=parent.GetAncestor(parent.nHeight-int(n));
        if(!node||ClassifyAdaptiveHeader(*node,d)!=AdaptiveHeaderKind::Scientific||node->GetBlockTime()<genesis->GetBlockTime())return {};
        times[n]=node->GetBlockTime();
    }
    std::sort(times.begin(),times.begin()+samples);const auto elapsed=times[samples/2]-genesis->GetBlockTime();
    const auto q=QuoteAnchoredMeritTarget(d,adjusted,elapsed);
    return q?std::optional{HistoricalMeritQuote{*q,count,adjusted,samples}}:std::nullopt;
}

inline std::optional<arith_uint256> QuoteConfiguredNativeHeaderWork(const CBlock& genesis,const uint256& expected,
    RangePolicyReadBudget budget,const arith_uint256& legacy,const arith_uint256& nonce,uint32_t bits){
    const auto fixed=ResolvePinnedNativeWork(genesis,expected,budget,legacy);if(!fixed)return {};
    const auto rt=ReadPinnedNativeRetarget(genesis,expected,budget);
    if(rt.verdict==NativeWorkPolicyReadVerdict::Absent)return bits==0?fixed->scientific:fixed->bootstrap;
    if(!rt.policy||!ReadPinnedNativeWorkPolicy(genesis,expected,budget).policy)return {};
    if(nonce==0){
        if(bits<rt.policy->minimum_q16||bits>rt.policy->maximum_q16)return {};
        return QuoteAdaptiveScientificWork(fixed->scientific,bits-rt.policy->minimum_q16);
    }
    if((nonce&65535)!=2||(nonce>>80)!=0)return {};
    return fixed->bootstrap;
}


}

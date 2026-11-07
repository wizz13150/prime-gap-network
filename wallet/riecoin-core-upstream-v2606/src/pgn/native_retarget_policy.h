// MIT. Candidate genesis codec/historical query. Not a deployed retarget rule.
#pragma once
#include <pgn/adaptive_merit_math.h>
#include <pgn/chain_work_policy.h>
#include <array>
#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
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
// PRECONDITION: callers supply accepted or scoped-body-verified ancestry.
// Coordination records may occur between scientific blocks. Count and sample
// only scientific headers; ledger height is not a scientific-work count.
inline std::optional<HistoricalMeritQuote> DeriveHistoricalMeritTarget(const CBlockIndex& parent,const MeritControllerDefinition& d){
    if(!MeritControllerValid(d)||parent.nHeight<0||!parent.phashBlock)return {};
    const auto* genesis=parent.GetAncestor(0);
    if(!genesis||!genesis->phashBlock||genesis->GetBlockTime()<0||ClassifyAdaptiveHeader(*genesis,d)!=AdaptiveHeaderKind::Bootstrap)return {};
    struct Summary {
        uint256 chain;std::array<uint32_t,5> policy;uint32_t count{},samples{};
        std::array<int64_t,11> newest{};
    };
    // Immutable, hash-bound summaries are an acceleration only. Eviction or a
    // policy change causes ancestry reconstruction; neither invents a count.
    static std::mutex mutex;
    static std::map<uint256,Summary> cache;
    static std::deque<uint256> order;
    const std::lock_guard lock(mutex);
    const std::array<uint32_t,5> policy{d.initial_q16,d.minimum_q16,d.maximum_q16,d.spacing_seconds,d.half_life_seconds};
    Summary result{genesis->GetBlockHash(),policy},prefix{genesis->GetBlockHash(),policy};
    uint32_t added{},samples{};std::array<int64_t,11> newest{};
    const CBlockIndex* node=&parent;
    while(node&&node->nHeight>0){
        if(!node->phashBlock||!node->pprev||node->pprev->nHeight!=node->nHeight-1)return {};
        const auto kind=ClassifyAdaptiveHeader(*node,d);
        if(kind==AdaptiveHeaderKind::Invalid)return {};
        const auto known=cache.find(node->GetBlockHash());
        if(known!=cache.end()&&known->second.chain==result.chain&&known->second.policy==policy){prefix=known->second;break;}
        if(kind==AdaptiveHeaderKind::Scientific){
            if(node->GetBlockTime()<genesis->GetBlockTime())return {};
            ++added;if(samples<newest.size())newest[samples++]=node->GetBlockTime();
        }
        node=node->pprev;
    }
    if(!node||!node->phashBlock)return {};
    result.count=prefix.count+added;
    for(uint32_t i=0;i<samples;++i)result.newest[result.samples++]=newest[i];
    for(uint32_t i=0;i<prefix.samples&&result.samples<result.newest.size();++i)result.newest[result.samples++]=prefix.newest[i];
    if(result.samples!=std::min(11U,result.count))return {};
    const auto id=parent.GetBlockHash();
    if(cache.contains(id))cache[id]=result;
    else{cache.emplace(id,result);order.push_back(id);}
    while(cache.size()>4096){cache.erase(order.front());order.pop_front();}
    if(!result.count){
        const auto median=parent.GetMedianTimePast();if(median<genesis->GetBlockTime())return {};
        const auto q=QuoteAnchoredMeritTarget(d,0,median-genesis->GetBlockTime());
        return q?std::optional{HistoricalMeritQuote{*q,0,0,0}}:std::nullopt;
    }
    const uint32_t adjusted=result.count-(result.samples-1)/2;
    std::sort(result.newest.begin(),result.newest.begin()+result.samples);
    const auto elapsed=result.newest[result.samples/2]-genesis->GetBlockTime();
    const auto q=QuoteAnchoredMeritTarget(d,adjusted,elapsed);
    return q?std::optional{HistoricalMeritQuote{*q,result.count,adjusted,result.samples}}:std::nullopt;
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

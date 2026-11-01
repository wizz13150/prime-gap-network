// MIT. Chain-authenticated candidate monetary metadata, NOT adopted issuance.
#pragma once
#include <pgn/chain_range_policy.h>
#include <pgn/issued_supply_curve.h>
#include <pgn/gold_epoch_policy.h>
namespace pgn::core {
inline constexpr std::string_view MonetaryPolicyEnvelopeTag{"PGNMONEY-POLICY-CANDIDATE-1"};
struct FeeRoutingDefinition {uint32_t numerator,denominator;};
struct MonetaryPolicyDefinition {
    CAmount ordinary_cap,exceptional_cap;IssuedSupplyLinearDefinition curve;
    uint32_t denominator,winner,near_miss,audit;
    std::optional<FeeRoutingDefinition> fees{}; // absent v1 NEVER selects an inherited fee rule.
};
inline bool MonetaryPolicyDefinitionValid(const MonetaryPolicyDefinition& d){
    return d.ordinary_cap>=0&&d.ordinary_cap<=PGN_MAX_SUPPLY_ATOMIC&&d.exceptional_cap==PGN_MAX_SUPPLY_ATOMIC-d.ordinary_cap&&
        d.curve.initial_subsidy>=0&&d.curve.initial_subsidy<=PGN_MAX_SUPPLY_ATOMIC&&d.curve.minimum_tail>=0&&d.curve.minimum_tail<=d.curve.initial_subsidy&&
        d.denominator&&d.winner&&uint64_t(d.winner)+d.near_miss+d.audit==d.denominator&&(!d.fees||(d.fees->denominator&&d.fees->numerator<=d.fees->denominator));
}
inline Bytes MonetaryPolicyPayload(const MonetaryPolicyDefinition& d){
    Bytes b{static_cast<unsigned char>(d.fees?2:1)};const auto put=[&](uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(static_cast<unsigned char>(v>>(8*i)));};
    put(d.ordinary_cap,8);put(d.exceptional_cap,8);put(d.curve.initial_subsidy,8);put(d.curve.minimum_tail,8);
    put(d.denominator,4);put(d.winner,4);put(d.near_miss,4);put(d.audit,4);
    if(d.fees){put(d.fees->numerator,4);put(d.fees->denominator,4);}return b;
}
inline std::optional<CScript> BuildCandidateMonetaryPolicyEnvelope(const MonetaryPolicyDefinition& d,RangePolicyReadBudget budget){
    if(!MonetaryPolicyDefinitionValid(d)||!budget.maximum_outputs||budget.maximum_payload_bytes<49)return {};
    const auto p=MonetaryPolicyPayload(d);if(p.size()>budget.maximum_payload_bytes)return {};CScript s=CScript{}<<OP_RETURN<<Bytes{MonetaryPolicyEnvelopeTag.begin(),MonetaryPolicyEnvelopeTag.end()}<<p;
    if(s.size()>budget.maximum_script_bytes)return {};
    return s;
}
inline std::optional<MonetaryPolicyDefinition> DecodeCandidateMonetaryPolicyPayload(const Bytes& payload){
    if(!((payload.size()==49&&payload[0]==1)||(payload.size()==57&&payload[0]==2)))return {};
    size_t at=1;const auto get=[&](unsigned n){uint64_t v{};for(unsigned i=0;i<n;++i)v|=uint64_t(payload[at++])<<(8*i);return v;};
    const auto ordinary=get(8),exceptional=get(8),initial=get(8),tail=get(8);
    if(ordinary>uint64_t(PGN_MAX_SUPPLY_ATOMIC)||exceptional>uint64_t(PGN_MAX_SUPPLY_ATOMIC)||initial>uint64_t(PGN_MAX_SUPPLY_ATOMIC)||tail>uint64_t(PGN_MAX_SUPPLY_ATOMIC))return {};
    MonetaryPolicyDefinition d{CAmount(ordinary),CAmount(exceptional),{CAmount(initial),CAmount(tail)},uint32_t(get(4)),uint32_t(get(4)),uint32_t(get(4)),uint32_t(get(4))};
    if(payload[0]==2)d.fees=FeeRoutingDefinition{uint32_t(get(4)),uint32_t(get(4))};
    if(!MonetaryPolicyDefinitionValid(d)||MonetaryPolicyPayload(d)!=payload)return {};
    return d;
}
struct ChainMonetaryPolicyCheck;
class ChainMonetaryPolicy {
    uint256 m_chain,m_genesis,m_id;MonetaryPolicyDefinition m_definition;
    std::optional<GoldEpochDefinition> m_epochs;
    ChainMonetaryPolicy(uint256 c,uint256 g,uint256 id,MonetaryPolicyDefinition d,std::optional<GoldEpochDefinition> epochs={}):m_chain(c),m_genesis(g),m_id(id),m_definition(d),m_epochs(epochs){}
    friend ChainMonetaryPolicyCheck ReadChainMonetaryPolicy(const CCoinsView&,const CBlockIndex&,const NativeRequestHistoryArchive&,RequestHistoryArchiveBudget,RangePolicyReadBudget);
public:
    const uint256& Chain()const{return m_chain;}const uint256& Genesis()const{return m_genesis;}const uint256& Id()const{return m_id;}
    const MonetaryPolicyDefinition& Definition()const{return m_definition;}
    const std::optional<GoldEpochDefinition>& Epochs()const{return m_epochs;}
};
struct ChainMonetaryPolicyCheck {ChainRangePolicyVerdict verdict;std::optional<ChainMonetaryPolicy> policy;};
inline ChainMonetaryPolicyCheck ReadChainMonetaryPolicy(const CCoinsView& view,const CBlockIndex& parent,
    const NativeRequestHistoryArchive& archive,RequestHistoryArchiveBudget archive_budget,RangePolicyReadBudget budget){
    const auto p=ReadCandidateGenesisPolicyPayload(view,parent,archive,archive_budget,budget,MonetaryPolicyEnvelopeTag);
    if(!p.genesis)return {p.verdict,{}};
    const auto definition=DecodeCandidateMonetaryPolicyPayload(p.payload);
    if(!definition)return {ChainRangePolicyVerdict::InvalidConfiguration,{}};
    std::optional<GoldEpochDefinition> epochs;
    const auto rules=ReadCandidateGenesisPolicyPayload(view,parent,archive,archive_budget,budget,GoldEpochPolicyTag);
    if(rules.verdict!=ChainRangePolicyVerdict::MissingConfiguration){
        if(!rules.genesis)return {rules.verdict,{}};
        epochs=DecodeGoldEpochPolicy(rules.payload);
        if(!epochs)return {ChainRangePolicyVerdict::InvalidConfiguration,{}};
        const auto lifetime=GoldEpochLifetimeOrdinaryBudget(*epochs);
        const auto first=QuoteGoldEpoch(*epochs,0);
        if(!lifetime||*lifetime>definition->ordinary_cap||!first||
           first->gold+first->silver+first->bronze+first->red+first->audit!=definition->curve.initial_subsidy)
            return {ChainRangePolicyVerdict::InvalidConfiguration,{}};
    }
    HashWriter id{TaggedHash("PGN/CANDIDATE/NATIVE-MONETARY-POLICY/1")};id<<p.genesis->Chain()<<p.genesis->Block()<<p.payload;
    return {ChainRangePolicyVerdict::Confirmed,ChainMonetaryPolicy{p.genesis->Chain(),p.genesis->Block(),id.GetHash(),*definition,epochs}};
}
struct SubsidyDistributionQuote {CAmount winner,near_miss,audit;};
inline std::optional<SubsidyDistributionQuote> QuoteSubsidyDistribution(CAmount gross,const MonetaryPolicyDefinition& d){
    if(!MonetaryPolicyDefinitionValid(d)||gross<0||gross>PGN_MAX_SUPPLY_ATOMIC)return {};
    const auto part=[&](uint32_t n){return CAmount((arith_uint256{uint64_t(gross)}*arith_uint256{n}/arith_uint256{d.denominator}).GetLow64());};
    const auto winner=part(d.winner),near_amount=part(d.near_miss);return SubsidyDistributionQuote{winner,near_amount,gross-winner-near_amount};
}
inline std::optional<CAmount> QuoteNativeWinnerFees(CAmount fees,const MonetaryPolicyDefinition& d){
    if(fees<0||fees>PGN_MAX_SUPPLY_ATOMIC||!MonetaryPolicyDefinitionValid(d))return {};
    if(!d.fees)return fees==0?std::optional<CAmount>{0}:std::nullopt;
    return CAmount((arith_uint256{uint64_t(fees)}*arith_uint256{d.fees->numerator}/arith_uint256{d.fees->denominator}).GetLow64());
}
// These quotes/metadata never authorize payments: actual recipients, fees,
// obligations, cumulative native state and atomic issuance remain REQUIRED.
}

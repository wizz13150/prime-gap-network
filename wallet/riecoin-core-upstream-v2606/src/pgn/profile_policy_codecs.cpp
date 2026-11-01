// MIT. Pure policy encoders/decoders shared by daemon/CLI/native readers.
#include <pgn/profile_policy_codec_internal.h>
namespace pgn::core {
std::optional<RangePoolDefinition> DecodeCandidateRangePoolDefinitionPayload(const Bytes& bytes,RangePolicyReadBudget budget){
    using namespace range_policy_codec;
    if(!budget.maximum_outputs||!budget.maximum_integer_bytes||!budget.maximum_script_bytes||bytes.size()>budget.maximum_payload_bytes)return {};
    try{return Decode(bytes,Limits(budget));}catch(const ResourceLimit&){return {};}catch(const std::invalid_argument&){return {};}
}
std::optional<CScript> BuildCandidateRangePolicyEnvelope(const RangePoolDefinition& p,RangePolicyReadBudget budget){
    using namespace range_policy_codec;
    if(!Valid(budget))return {};
    try{const auto limits=Limits(budget);const auto bytes=Encode(p,limits);
        CScript script=CScript{}<<OP_RETURN<<Bytes{RangePolicyEnvelopeTag.begin(),RangePolicyEnvelopeTag.end()}<<bytes;
        if(script.size()>budget.maximum_script_bytes)return {};
        return script;
    }catch(const std::invalid_argument&){return {};}
}
std::optional<ScienceVerificationDefinition> DecodeCandidateScienceVerificationDefinitionPayload(const Bytes& bytes,RangePolicyReadBudget budget){
    using namespace science_policy_codec;
    if(!budget.maximum_outputs||!budget.maximum_integer_bytes||!budget.maximum_script_bytes||bytes.size()>budget.maximum_payload_bytes)return {};
    try{return Decode(bytes,Limits(budget));}catch(const ResourceLimit&){return {};}catch(const std::invalid_argument&){return {};}
}
std::optional<CScript> BuildCandidateSciencePolicyEnvelope(const ScienceVerificationDefinition& p,RangePolicyReadBudget budget){
    using namespace science_policy_codec;
    if(!budget.maximum_outputs||!budget.maximum_integer_bytes||!budget.maximum_script_bytes||!budget.maximum_payload_bytes)return {};
    try{const auto limits=Limits(budget);const auto bytes=Encode(p,limits);
        CScript script=CScript{}<<OP_RETURN<<Bytes{SciencePolicyEnvelopeTag.begin(),SciencePolicyEnvelopeTag.end()}<<bytes;
        if(script.size()>budget.maximum_script_bytes)return {};
        return script;
    }catch(const std::invalid_argument&){return {};}
}

}

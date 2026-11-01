// MIT. Single shared source of pure canonical policy arithmetic/codecs.
#pragma once
#include <pgn/chain_range_policy.h>
#include <pgn/chain_science_policy.h>
#include <pgn/canonical_encoding.h>
#include <algorithm>
#include <limits>
namespace pgn::core::range_policy_codec {
constexpr std::string_view Domain{"PGN/RANGE-POLICY/1"};
constexpr std::string_view RollingDomain{"PGN/RANGE-POLICY/2"};
struct ResourceLimit {};
inline bool Valid(const RangePoolDefinition& p){return p.origin>=0&&p.width>0&&p.slots>0&&p.seed_delay&&p.lease_blocks&&
    p.seed_delay<=uint32_t(std::numeric_limits<int32_t>::max())&&p.lease_blocks<=uint32_t(std::numeric_limits<int32_t>::max())&&
    ((!p.selection_window&&!p.limit)||(p.selection_window&&p.limit&&
      *p.limit>p.origin+p.width*(p.slots-1)&&*p.limit<=p.origin+p.width*p.slots));}
inline bool Valid(RangePolicyReadBudget b){return b.maximum_outputs&&b.maximum_integer_bytes&&b.maximum_script_bytes&&b.maximum_payload_bytes;}
inline EncodingLimits Limits(RangePolicyReadBudget b){return {0,b.maximum_payload_bytes,b.maximum_integer_bytes,0};}
inline Bytes Encode(const RangePoolDefinition& p,const EncodingLimits& limits){
    detail::encodingRequire(Valid(p),"Invalid candidate range policy");
    detail::Writer out{{},limits};out.domain(p.selection_window?RollingDomain:Domain);out.integer(p.origin);out.integer(p.width);out.integer(p.slots);
    out.little(p.seed_delay,4);out.little(p.lease_blocks,4);
    if(p.selection_window){out.little(p.selection_window,4);out.integer(*p.limit);}
    return std::move(out.data);
}
inline Integer ReadInteger(detail::Reader& in){
    auto preflight=in;const auto count=preflight.count();
    detail::encodingRequire(count<=preflight.data.size()-preflight.position,"Truncated policy integer");
    if(count>in.limits.max_integer_bytes)throw ResourceLimit{};
    return in.integer();
}
inline RangePoolDefinition Decode(const Bytes& bytes,const EncodingLimits& limits){
    detail::Reader in{bytes,limits};bool rolling=false;
    try{auto probe=in;probe.domain(RollingDomain);in.position=probe.position;rolling=true;}
    catch(const std::invalid_argument&){in.domain(Domain);}
    RangePoolDefinition p{ReadInteger(in),ReadInteger(in),ReadInteger(in),uint32_t(in.little(4)),uint32_t(in.little(4))};
    if(rolling){p.selection_window=uint32_t(in.little(4));p.limit=ReadInteger(in);}
    detail::encodingRequire(in.position==bytes.size()&&Valid(p),"Invalid/trailing candidate range policy");
    detail::encodingRequire(Encode(p,limits)==bytes,"Noncanonical candidate range policy");return p;
}
}
namespace pgn::core::science_policy_codec {
constexpr std::string_view Domain{"PGN/SCIENCE-POLICY/1"};
struct ResourceLimit {};
inline bool Valid(const EncodingLimits& p){return p.max_records&&p.max_encoded_bytes&&p.max_integer_bytes&&p.max_segments;}
inline bool Valid(const ScienceVerificationDefinition& p){
    const auto& e=p.evidence;const auto& s=e.scientific;const auto& a=e.arithmetic;
    if(!p.segments||!Valid(e.encoding)||!Valid(p.proof)||p.segments>e.encoding.max_segments||
       s.endpoint_limits.max_integer_bits<2||!s.endpoint_limits.max_nodes||!s.endpoint_limits.max_factors_per_node||
       a.max_input_bits<2||!a.max_fractional_bits||!a.max_series_terms||!e.accuracy.fractional_bits||!e.accuracy.series_terms||
       e.accuracy.fractional_bits>a.max_fractional_bits||e.accuracy.series_terms>a.max_series_terms||
       e.target_numerator<=0||e.target_denominator<=0)return false;
    Integer x=e.target_numerator,y=e.target_denominator;
    while(y!=0){Integer next=x%y;x=std::move(y);y=std::move(next);}return x==1;
}
inline EncodingLimits Limits(RangePolicyReadBudget b){return {0,b.maximum_payload_bytes,b.maximum_integer_bytes,0};}
inline void Put(detail::Writer& out,const EncodingLimits& p){out.little(p.max_records,8);out.little(p.max_encoded_bytes,8);out.little(p.max_integer_bytes,4);out.little(p.max_segments,4);}
inline EncodingLimits Get(detail::Reader& in){return {in.little(8),in.little(8),uint32_t(in.little(4)),uint32_t(in.little(4))};}
inline Bytes Encode(const ScienceVerificationDefinition& p,const EncodingLimits& limits){
    detail::encodingRequire(Valid(p),"Invalid candidate science policy");detail::Writer out{{},limits};out.domain(Domain);out.little(p.segments,4);
    const auto& e=p.evidence;Put(out,e.encoding);Put(out,p.proof);
    const auto& s=e.scientific;out.little(s.endpoint_limits.max_integer_bits,4);out.little(s.endpoint_limits.max_nodes,4);
    out.little(s.endpoint_limits.max_factors_per_node,4);out.little(s.max_interior_positions,8);
    const auto& a=e.arithmetic;out.little(a.max_input_bits,4);out.little(a.max_fractional_bits,4);out.little(a.max_series_terms,4);
    out.little(e.accuracy.fractional_bits,4);out.little(e.accuracy.series_terms,4);out.integer(e.target_numerator);out.integer(e.target_denominator);
    return std::move(out.data);
}
inline Integer IntegerField(detail::Reader& in){auto preflight=in;const auto count=preflight.count();
    detail::encodingRequire(count<=preflight.data.size()-preflight.position,"Truncated policy integer");
    if(count>in.limits.max_integer_bytes)throw ResourceLimit{};
    return in.integer();}
inline ScienceVerificationDefinition Decode(const Bytes& bytes,const EncodingLimits& limits){
    detail::Reader in{bytes,limits};in.domain(Domain);ScienceVerificationDefinition p{};p.segments=in.little(4);
    auto& e=p.evidence;e.encoding=Get(in);p.proof=Get(in);
    auto& s=e.scientific;s.endpoint_limits={uint32_t(in.little(4)),uint32_t(in.little(4)),uint32_t(in.little(4))};s.max_interior_positions=in.little(8);
    e.arithmetic={uint32_t(in.little(4)),uint32_t(in.little(4)),uint32_t(in.little(4))};e.accuracy={uint32_t(in.little(4)),uint32_t(in.little(4))};
    e.target_numerator=IntegerField(in);e.target_denominator=IntegerField(in);
    detail::encodingRequire(in.position==bytes.size()&&Valid(p)&&Encode(p,limits)==bytes,"Invalid/noncanonical science policy");return p;
}
}

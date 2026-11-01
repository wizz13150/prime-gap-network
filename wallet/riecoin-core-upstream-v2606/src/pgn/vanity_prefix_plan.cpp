// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license.
#include <pgn/vanity_prefix_plan.h>
#include <pgn/vanity_public_search.h>
#include <base58.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <stdexcept>
namespace pgn::core {
namespace {
using Big=boost::multiprecision::cpp_int;
constexpr std::string_view B58="123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
constexpr std::string_view B32="qpzry9x8gf2tvdw0s3jn54khce6mua7l";
Big PrefixNumber(std::string_view prefix) {
    if(prefix.empty()||prefix.size()>40||prefix[0]=='1')throw std::invalid_argument("Empty/oversized/leading-zero prefix");
    Big digits=0;for(char c:prefix){const auto d=B58.find(c);if(d==B58.npos)throw std::invalid_argument("Non-Base58 symbol");digits*=58;digits+=d;}return digits;
}
std::string_view JointPrefix(std::string_view name,std::string_view query) {
    const auto common=std::min(name.size(),query.size());
    if(name.substr(0,common)!=query.substr(0,common))throw std::invalid_argument("Prefix contradicts explicit namespace");
    return name.size()>query.size()?name:query;
}
std::array<uint32_t,5> Words(Big n) {
    std::array<uint32_t,5> out{};for(size_t i=5;i-->0;){out[i]=static_cast<uint32_t>(n&0xffffffff);n>>=32;}return out;
}
Big Version(const LegacyVanityPolicy& policy) {
    if(policy.keyhash_version.empty()||policy.keyhash_version.size()>4||!policy.keyhash_version[0]||
       policy.required_namespace.empty()||policy.required_namespace.size()>10)
        throw std::invalid_argument("Explicit nonzero Base58 keyhash version and namespace required");
    (void)PrefixNumber(policy.required_namespace);
    Big value=0;for(auto c:policy.keyhash_version){value<<=8;value+=c;}return value;
}
}
LegacyVanityPlan CompileLegacyVanityPrefix(const LegacyVanityPolicy& policy,std::string_view prefix) {
    const Big base=Version(policy)<<192;
    (void)PrefixNumber(prefix);
    const auto joint=JointPrefix(policy.required_namespace,prefix);const Big digits=PrefixNumber(joint);
    // Nonzero version means no leading-zero '1' padding. Account for every
    // possible encoded length, intersect before removing checksum bits.
    if(prefix[0]=='1')throw std::invalid_argument("Prefix contradicts nonzero version");
    const Big top=base+(Big(1)<<192)-1;LegacyVanityPlan result{policy,std::string(prefix),{}};
    for(size_t length=joint.size();length<=40;++length) {
        Big factor=1;for(size_t i=joint.size();i<length;++i)factor*=58;
        const Big candidate_lo=digits*factor,candidate_hi=(digits+1)*factor-1;
        const Big lo=std::max(base,candidate_lo),hi=std::min(top,candidate_hi);
        if(lo<=hi) {
            // If the query fixes the entire encoded address at this length,
            // its checksum is no longer a free boundary. Reject contradictions
            // BEFORE any GPU search rather than searching forever for it.
            if(length==joint.size()) {
                std::vector<unsigned char> decoded;
                if(!DecodeBase58Check(std::string(joint),decoded,int(policy.keyhash_version.size()+20))||
                   decoded.size()!=policy.keyhash_version.size()+20||
                   !std::equal(policy.keyhash_version.begin(),policy.keyhash_version.end(),decoded.begin()))continue;
            }
            result.ranges.push_back({Words((lo-base)>>32),Words((hi-base)>>32)});
        }
    }
    if(result.ranges.empty())throw std::invalid_argument("Prefix is impossible for the explicit Base58 version");
    return result;
}
std::vector<LegacyVanityPlan> PlanLegacyNamespacePrefix(std::string_view name,size_t version_bytes,
    std::string_view prefix,size_t max_versions) {
    (void)PrefixNumber(name);(void)PrefixNumber(prefix);
    if(name.size()>10||!version_bytes||version_bytes>4||!max_versions||max_versions>4096)
        throw std::invalid_argument("Invalid explicit namespace/version admission");
    const auto joint=JointPrefix(name,prefix);const Big digits=PrefixNumber(joint);
    const Big first=Big(1)<<(8*(version_bytes-1)),last=(Big(1)<<(8*version_bytes))-1;
    std::vector<LegacyVanityPlan> result;
    for(size_t length=joint.size();length<=40;++length) {
        Big factor=1;for(size_t i=joint.size();i<length;++i)factor*=58;
        const Big proposed_low=(digits*factor)>>192,proposed_high=(((digits+1)*factor)-1)>>192;
        const Big low=std::max(first,proposed_low),high=std::min(last,proposed_high);
        if(low>high)continue;
        if(high-low+1>max_versions-result.size())throw std::length_error("Version-family admission exceeded; not an impossible name");
        for(Big value=low;value<=high;++value) {
            std::vector<unsigned char> bytes(version_bytes);Big v=value;
            for(size_t i=version_bytes;i-->0;){bytes[i]=static_cast<unsigned char>(v&255);v>>=8;}
            const auto duplicate=std::any_of(result.begin(),result.end(),[&](const auto& p){return p.policy.keyhash_version==bytes;});
            if(!duplicate)result.push_back(CompileLegacyVanityPrefix({bytes,std::string(name)},prefix));
        }
    }
    if(result.empty())throw std::invalid_argument("Prefix outside explicit namespace/version byte domain");
    return result;
}
std::vector<VanityHashRange> CompileWitnessVanityPrefix(const NativeAddressPolicy& policy,std::string_view prefix) {
    ValidateVanityPrefix(policy,prefix);Big value=0;size_t bits{};
    const auto fixed=policy.hrp.size()+2;
    for(size_t i=fixed;i<prefix.size()&&i<fixed+32;++i) {
        char c=prefix[i];if(c>='A'&&c<='Z')c=char(c-'A'+'a');
        value<<=5;value+=B32.find(c);bits+=5;
    }
    const size_t remainder=160-bits;
    return {{Words(value<<remainder),Words(((value+1)<<remainder)-1)}};
}
std::string EncodeLegacyVanityCandidate(const CPubKey& pub,const LegacyVanityPolicy& policy) {
    (void)Version(policy);
    if(pub.size()!=33||!pub.IsFullyValid())throw std::invalid_argument("Compressed valid public key required");
    auto bytes=policy.keyhash_version;const auto id=pub.GetID();bytes.insert(bytes.end(),id.begin(),id.end());
    const auto address=EncodeBase58Check(bytes);
    if(!address.starts_with(policy.required_namespace))throw std::invalid_argument("Hash outside explicit namespace for boundary version");
    return address;
}
bool VerifyLegacyVanityCandidate(const CPubKey& pub,const LegacyVanityPlan& plan,std::string_view address) {
    if(!address.starts_with(plan.prefix)||!address.starts_with(plan.policy.required_namespace))return false;
    try{if(address!=EncodeLegacyVanityCandidate(pub,plan.policy))return false;}catch(const std::invalid_argument&){return false;}
    std::vector<unsigned char> decoded;
    if(!DecodeBase58Check(std::string(address),decoded,int(plan.policy.keyhash_version.size()+20)))return false;
    const auto& version=plan.policy.keyhash_version;const auto id=pub.GetID();
    return decoded.size()==version.size()+20&&std::equal(version.begin(),version.end(),decoded.begin())&&
        std::equal(id.begin(),id.end(),decoded.begin()+version.size());
}
}

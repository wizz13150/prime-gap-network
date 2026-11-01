// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <pgn/vanity_public_search.h>
#include <addresstype.h>
#include <bech32.h>
#include <key_io.h>
#include <secp256k1.h>
#include <util/strencodings.h>

#include <algorithm>
#include <array>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

namespace pgn::core {
namespace {
constexpr std::string_view ALPHABET="qpzry9x8gf2tvdw0s3jn54khce6mua7l";
// SEC2's public generator, not an address/network constant or a secret scalar.
constexpr std::array<unsigned char,33> GENERATOR={0x02,0x79,0xbe,0x66,0x7e,0xf9,0xdc,0xbb,0xac,0x55,0xa0,0x62,0x95,0xce,0x87,0x0b,0x07,0x02,0x9b,0xfc,0xdb,0x2d,0xce,0x28,0xd9,0x59,0xf2,0x81,0x5b,0x16,0xf8,0x17,0x98};
std::string Present(std::string text,const NativeAddressPolicy& policy) {
    if(policy.presentation==NativeWitnessCase::Upper)
        for(char& c:text)if(c>='a'&&c<='z')c=char(c-'a'+'A');
    return text;
}
struct Plan {
    std::optional<NativeAddressPolicy> policy;
    std::string prefix;
    std::vector<uint8_t> hash_prefix;
};
Plan Compile(const NativeAddressPolicy& policy,std::string_view prefix) {
    if(ValidateNativeAddressPolicy(policy)!=NativeAddressPolicyStatus::Ready || !NativeWitnessProgramFits(policy,20))
        throw std::invalid_argument("Vanity requires an explicit usable witness-v0 keyhash policy");
    const auto fixed=Present(policy.hrp+"1q",policy);
    const auto address_size=fixed.size()+32+bech32::CHECKSUM_SIZE;
    if(prefix.empty() || prefix.size()>address_size ||
       prefix.substr(0,std::min(prefix.size(),fixed.size()))!=std::string_view(fixed).substr(0,std::min(prefix.size(),fixed.size())))
        throw std::invalid_argument("Vanity prefix contradicts fixed HRP, witness version, case or address length");
    Plan result{policy,std::string(prefix),{}};
    for(size_t i=fixed.size();i<prefix.size();++i) {
        char c=prefix[i];
        if(policy.presentation==NativeWitnessCase::Upper){if(c>='a'&&c<='z')throw std::invalid_argument("Vanity prefix case mismatch");if(c>='A'&&c<='Z')c=char(c-'A'+'a');}
        const auto digit=ALPHABET.find(c);
        if(digit==std::string_view::npos)throw std::invalid_argument("Vanity prefix has a non-Bech32 symbol");
        if(i-fixed.size()<32)result.hash_prefix.push_back(uint8_t(digit));
    }
    // Once all160 payload bits are specified, the Core fixes every checksum
    // character too. Do not launch an impossible near/full-address search.
    if(result.hash_prefix.size()==32) {
        std::vector<uint8_t> data{0};data.insert(data.end(),result.hash_prefix.begin(),result.hash_prefix.end());
        const auto full=Present(bech32::Encode(bech32::Encoding::BECH32,policy.hrp,data),policy);
        if(full.compare(0,prefix.size(),prefix)!=0)throw std::invalid_argument("Vanity prefix contradicts the Core checksum");
    }
    return result;
}
bool HashMatches(const CKeyID& hash,const std::vector<uint8_t>& prefix) {
    size_t position{};bool match=true;
    const bool converted=ConvertBits<8,5,true>([&](uint8_t digit){if(position<prefix.size()&&digit!=prefix[position])match=false;++position;},hash.begin(),hash.end());
    if(!converted||position!=32)throw std::logic_error("Core keyhash conversion invariant failed");
    return match;
}
} // namespace

void ValidateVanityPrefix(const NativeAddressPolicy& policy,std::string_view prefix) { (void)Compile(policy,prefix); }

VanityPublicResult SearchVanityPublic(const CPubKey& seed,const NativeAddressPolicy& policy,
    std::string_view prefix,uint64_t first_step,uint64_t slots,
    const std::function<bool()>& cancelled,const std::function<bool(const VanityPublicMatch&)>& on_match) {
    const auto plan=Compile(policy,prefix);
    if(seed.size()!=33 || !seed.IsFullyValid())throw std::invalid_argument("Vanity requires a valid compressed public seed");
    if(!on_match)throw std::invalid_argument("Vanity match consumer is required");
    if(slots && first_step>std::numeric_limits<uint64_t>::max()-(slots-1))throw std::invalid_argument("Vanity step interval wraps");
    static std::once_flag checked;
    std::call_once(checked,[]{secp256k1_selftest();});
    VanityPublicResult result;
    if(!slots)return result;
    secp256k1_pubkey point{},generator{};
    if(!secp256k1_ec_pubkey_parse(secp256k1_context_static,&point,seed.begin(),seed.size()) ||
       !secp256k1_ec_pubkey_parse(secp256k1_context_static,&generator,GENERATOR.data(),GENERATOR.size()))
        throw std::logic_error("Core public key parse invariant failed");
    bool finite=true;
    if(first_step) {
        std::array<unsigned char,32> tweak{};
        auto remaining=first_step;for(size_t i=32;i-->24;){tweak[i]=uint8_t(remaining);remaining>>=8;}
        finite=secp256k1_ec_pubkey_tweak_add(secp256k1_context_static,&point,tweak.data())!=0;
    }
    for(uint64_t i=0;i<slots;++i) {
        if(cancelled&&cancelled()){result.cancelled=true;break;}
        ++result.visited_slots;
        if(!finite)++result.infinity_slots;
        else {
            std::array<unsigned char,33> bytes{};size_t length=bytes.size();
            if(!secp256k1_ec_pubkey_serialize(secp256k1_context_static,bytes.data(),&length,&point,SECP256K1_EC_COMPRESSED)||length!=bytes.size())
                throw std::logic_error("Core compressed key serialization failed");
            const CPubKey pubkey(bytes.begin(),bytes.end());
            const auto hash=pubkey.GetID();++result.hashed_public_keys;
            // Test the compiled payload bits before allocating/encoding text.
            // Final matches still pay the REAL Core codec and script roundtrip.
            if(HashMatches(hash,plan.hash_prefix)) {
                const CTxDestination destination=WitnessV0KeyHash(hash);
                const auto address=EncodeDestination(destination,plan.policy);++result.encoded_addresses;
                if(address.compare(0,plan.prefix.size(),plan.prefix)==0) {
                    std::string error;const auto decoded=DecodeDestination(address,plan.policy,error);
                    if(!error.empty()||GetScriptForDestination(decoded)!=GetScriptForDestination(destination))
                        throw std::logic_error("Core vanity address roundtrip failed");
                    ++result.matches;
                    if(!on_match(VanityPublicMatch{first_step+i,pubkey,address})){result.callback_stopped=true;break;}
                }
            }
        }
        if(i==slots-1)break;
        if(!finite){point=generator;finite=true;}
        else {
            const secp256k1_pubkey* inputs[]={&point,&generator};secp256k1_pubkey next{};
            finite=secp256k1_ec_pubkey_combine(secp256k1_context_static,&next,inputs,2)!=0;
            if(finite)point=next;
        }
    }
    return result;
}
} // namespace pgn::core

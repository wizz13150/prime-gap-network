// Copyright (c) 2014-present The Bitcoin Core developers
// Copyright (c) 2014-present The Riecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>

#include <base58.h>
#include <bech32.h>
#include <outputtype.h>
#include <script/interpreter.h>
#include <script/solver.h>
#include <tinyformat.h>
#include <util/overflow.h>
#include <util/strencodings.h>

#include <algorithm>
#include <cassert>
#include <cstring>

/// Maximum witness length for Bech32 addresses.
static constexpr std::size_t BECH32_WITNESS_PROG_MAX_LEN = 40;

namespace {
class DestinationEncoder
{
private:
    const pgn::core::NativeAddressPolicy& m_policy;

    std::string EncodeWitness(const std::vector<unsigned char>& data) const
    {
        const auto encoding = data[0] == 0 ? bech32::Encoding::BECH32 : bech32::Encoding::BECH32M;
        auto address = bech32::Encode(encoding, m_policy.hrp, data);
        if (address.size() > m_policy.max_characters) return {};
        return m_policy.presentation == pgn::core::NativeWitnessCase::Upper ? ToUpper(address) : address;
    }

public:
    explicit DestinationEncoder(const pgn::core::NativeAddressPolicy& policy) : m_policy(policy) {}

    std::string operator()(const WitnessV0KeyHash& id) const
    {
        if (m_policy.keyhash_alias) {
            const auto& a=*m_policy.keyhash_alias;auto data=a.version;
            data.insert(data.end(),id.begin(),id.end());
            auto alias=EncodeBase58Check(data);
            if (alias.size()<=m_policy.max_characters && alias.starts_with(a.required_namespace))return alias;
        }
        std::vector<unsigned char> data = {0};
        data.reserve(33);
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, id.begin(), id.end());
        return EncodeWitness(data);
    }

    std::string operator()(const WitnessV0ScriptHash& id) const
    {
        std::vector<unsigned char> data = {0};
        data.reserve(53);
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, id.begin(), id.end());
        return EncodeWitness(data);
    }

    std::string operator()(const WitnessV1Taproot& tap) const
    {
        std::vector<unsigned char> data = {1};
        data.reserve(53);
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, tap.begin(), tap.end());
        return EncodeWitness(data);
    }

    std::string operator()(const WitnessUnknown& id) const
    {
        const std::vector<unsigned char>& program = id.GetWitnessProgram();
        if (id.GetWitnessVersion() < 1 || id.GetWitnessVersion() > 16 || program.size() < 2 || program.size() > 40) {
            return {};
        }
        std::vector<unsigned char> data = {(unsigned char)id.GetWitnessVersion()};
        data.reserve(1 + CeilDiv(program.size() * 8, 5u));
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, program.begin(), program.end());
        return EncodeWitness(data);
    }

    std::string operator()(const CNoDestination& no) const { return {}; }
    std::string operator()(const PubKeyDestination& pk) const { return {}; }

    // No script-hex or Base58 fallback can acquire PGN identity here.
    std::string operator()(const PKHash&) const { return {}; }
    std::string operator()(const ScriptHash&) const { return {}; }
};

CTxDestination DecodeWitnessDestination(const std::string& str, const pgn::core::NativeAddressPolicy& policy, std::string& error_str, std::vector<int>* error_locations)
{
    std::vector<unsigned char> data;
    error_str = "";
    if (error_locations) error_locations->clear();
    if (str.size() > policy.max_characters) {
        error_str = "Witness address exceeds its explicit character budget";
        return CNoDestination();
    }

    data.clear();
    const auto dec = bech32::Decode(str);
    if (dec.encoding != bech32::Encoding::INVALID) {
        if (dec.data.empty()) {
            error_str = "Empty Bech32 data section";
            return CNoDestination();
        }
        // Bech32 decoding
        if (dec.hrp != policy.hrp) {
            error_str = strprintf("Invalid witness address prefix (expected %s, got %s).", policy.hrp, dec.hrp);
            return CNoDestination();
        }
        int version = dec.data[0]; // The first 5 bit symbol is the witness version (0-16)
        if ((version == 0 && dec.encoding != bech32::Encoding::BECH32) ||
            (version != 0 && dec.encoding != bech32::Encoding::BECH32M)) {
            error_str = "Witness version/checksum variant mismatch";
            return CNoDestination();
        }
        // The rest of the symbols are converted witness program bytes.
        data.reserve(((dec.data.size() - 1) * 5) / 8);
        if (ConvertBits<5, 8, false>([&](unsigned char c) { data.push_back(c); }, dec.data.begin() + 1, dec.data.end())) {

            std::string_view byte_str{data.size() == 1 ? "byte" : "bytes"};

            if (version == 0) {
                {
                    WitnessV0KeyHash keyid;
                    if (data.size() == keyid.size()) {
                        std::copy(data.begin(), data.end(), keyid.begin());
                        return keyid;
                    }
                }
                {
                    WitnessV0ScriptHash scriptid;
                    if (data.size() == scriptid.size()) {
                        std::copy(data.begin(), data.end(), scriptid.begin());
                        return scriptid;
                    }
                }

                error_str = strprintf("Invalid Bech32 v0 address program size (%d %s), per BIP141", data.size(), byte_str);
                return CNoDestination();
            }

            if (version == 1 && data.size() == WITNESS_V1_TAPROOT_SIZE) {
                static_assert(WITNESS_V1_TAPROOT_SIZE == WitnessV1Taproot::size());
                WitnessV1Taproot tap;
                std::copy(data.begin(), data.end(), tap.begin());
                return tap;
            }

            if (CScript::IsPayToAnchor(version, data)) {
                return PayToAnchor();
            }

            if (version > 16) {
                error_str = "Invalid Bech32 address witness version";
                return CNoDestination();
            }

            if (data.size() < 2 || data.size() > BECH32_WITNESS_PROG_MAX_LEN) {
                error_str = strprintf("Invalid Bech32 address program size (%d %s)", data.size(), byte_str);
                return CNoDestination();
            }

            return WitnessUnknown{version, data};
        } else {
            error_str = strprintf("Invalid padding in Bech32 data section");
            return CNoDestination();
        }
    }

    // Perform Bech32 error location
    auto res = bech32::LocateErrors(str);
    error_str = res.first;
    if (error_locations) *error_locations = std::move(res.second);
    return CNoDestination();
}
} // namespace

CKey DecodeSecret(const std::string& str)
{
    CKey key;
    std::vector<unsigned char> data;
    if (str.size() == 67) {
        if (str.substr(0, 3) == "prv") { // Compressed: prv . 64 hex
            data = ParseHex(str.substr(3, 64));
            if (data.size() == 32)
                key.Set(data.begin(), data.begin() + 32, true);
        }
    }
    if (!data.empty()) {
        memory_cleanse(data.data(), data.size());
    }
    return key;
}

std::string EncodeSecret(const CKey& key)
{
    assert(key.IsValid());
    std::vector<unsigned char> data;
    data.insert(data.end(), UCharCast(key.begin()), UCharCast(key.end()));
    std::string ret("prv" + HexStr(data));
    memory_cleanse(data.data(), data.size());
    return ret;
}

CExtPubKey DecodeExtPubKey(const std::string& str)
{
    CExtPubKey key;
    std::vector<unsigned char> data;
    if (DecodeBase58Check(str, data, 78)) {
        const std::vector<unsigned char>& prefix = Params().Base58Prefix(CChainParams::EXT_PUBLIC_KEY);
        if (data.size() == BIP32_EXTKEY_SIZE + prefix.size() && std::equal(prefix.begin(), prefix.end(), data.begin())) {
            key.Decode(data.data() + prefix.size());
        }
    }
    return key;
}

std::string EncodeExtPubKey(const CExtPubKey& key)
{
    std::vector<unsigned char> data = Params().Base58Prefix(CChainParams::EXT_PUBLIC_KEY);
    size_t size = data.size();
    data.resize(size + BIP32_EXTKEY_SIZE);
    key.Encode(data.data() + size);
    std::string ret = EncodeBase58Check(data);
    return ret;
}

CExtKey DecodeExtKey(const std::string& str)
{
    CExtKey key;
    std::vector<unsigned char> data;
    if (DecodeBase58Check(str, data, 78)) {
        const std::vector<unsigned char>& prefix = Params().Base58Prefix(CChainParams::EXT_SECRET_KEY);
        if (data.size() == BIP32_EXTKEY_SIZE + prefix.size() && std::equal(prefix.begin(), prefix.end(), data.begin())) {
            key.Decode(data.data() + prefix.size());
        }
    }
    if (!data.empty()) {
        memory_cleanse(data.data(), data.size());
    }
    return key;
}

std::string EncodeExtKey(const CExtKey& key)
{
    std::vector<unsigned char> data = Params().Base58Prefix(CChainParams::EXT_SECRET_KEY);
    size_t size = data.size();
    data.resize(size + BIP32_EXTKEY_SIZE);
    key.Encode(data.data() + size);
    std::string ret = EncodeBase58Check(data);
    memory_cleanse(data.data(), data.size());
    return ret;
}

std::string EncodeDestination(const CTxDestination& dest)
{
    return EncodeDestination(dest, SelectedPgnNativeAddressPolicy());
}

std::string EncodeDestination(const CTxDestination& dest, const std::optional<pgn::core::NativeAddressPolicy>& policy)
{
    if (pgn::core::ValidateNativeAddressPolicy(policy) != pgn::core::NativeAddressPolicyStatus::Ready)
        throw pgn::core::NativeAddressPolicyError{};
    return std::visit(DestinationEncoder(*policy), dest);
}

CTxDestination DecodeDestination(const std::string& str, const std::optional<pgn::core::NativeAddressPolicy>& policy,
                                std::string& error_msg, std::vector<int>* error_locations)
{
    if (pgn::core::ValidateNativeAddressPolicy(policy) != pgn::core::NativeAddressPolicyStatus::Ready)
        throw pgn::core::NativeAddressPolicyError{};
    if (policy->keyhash_alias && str.starts_with(policy->keyhash_alias->required_namespace)) {
        if (error_locations)error_locations->clear();
        const auto& a=*policy->keyhash_alias;std::vector<unsigned char> bytes;
        if (str.size()>policy->max_characters || !DecodeBase58Check(str,bytes,a.version.size()+20) ||
            bytes.size()!=a.version.size()+20 || !std::equal(a.version.begin(),a.version.end(),bytes.begin()) ||
            EncodeBase58Check(bytes)!=str) {
            error_msg="Invalid native Base58Check keyhash alias";return CNoDestination{};
        }
        WitnessV0KeyHash keyhash;std::copy(bytes.begin()+a.version.size(),bytes.end(),keyhash.begin());
        error_msg.clear();return keyhash;
    }
    return DecodeWitnessDestination(str, *policy, error_msg, error_locations);
}

pgn::core::NativeAddressPolicyStatus CheckNativeAddressWalletOutput(
    const std::optional<pgn::core::NativeAddressPolicy>& policy, OutputType type)
{
    using S = pgn::core::NativeAddressPolicyStatus;
    const auto status = pgn::core::ValidateNativeAddressPolicy(policy);
    if (status != S::Ready) return status;
    if (type != OutputType::BECH32 && type != OutputType::BECH32M) return S::UnsupportedWalletType;
    // Covers both v0 key/script hashes and v1 Taproot, not only wpkh20.
    return pgn::core::NativeWitnessProgramFits(*policy, 32) ? S::Ready : S::InsufficientBudget;
}

CTxDestination DecodeDestination(const std::string& str, std::string& error_msg, std::vector<int>* error_locations)
{
    return DecodeDestination(str, SelectedPgnNativeAddressPolicy(), error_msg, error_locations);
}

CTxDestination DecodeDestination(const std::string& str)
{
    std::string error_msg;
    return DecodeDestination(str, error_msg);
}

bool IsValidDestinationString(const std::string& str, const CChainParams& params)
{
    std::string error_msg;
    return IsValidDestination(DecodeDestination(str, params.PgnNativeAddressPolicy(), error_msg, nullptr));
}

bool IsValidDestinationString(const std::string& str)
{
    std::string error_msg;
    return IsValidDestination(DecodeDestination(str, SelectedPgnNativeAddressPolicy(), error_msg, nullptr));
}

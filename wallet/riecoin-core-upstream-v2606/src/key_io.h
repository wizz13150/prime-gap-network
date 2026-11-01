// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KEY_IO_H
#define BITCOIN_KEY_IO_H

#include <addresstype.h>
#include <chainparams.h>
#include <key.h>
#include <pgn/nativeaddresspolicy.h>
#include <pubkey.h>

#include <string>

enum class OutputType;
/** Native witness-only codec, same bodies as selected-policy wrappers below.
 * Missing/invalid policy is a LOCAL configuration exception, not invalid input.
 * Script equality proves no ownership, receivability or network activation. */
std::string EncodeDestination(const CTxDestination& dest, const std::optional<pgn::core::NativeAddressPolicy>& policy);
CTxDestination DecodeDestination(const std::string& str, const std::optional<pgn::core::NativeAddressPolicy>& policy,
                                std::string& error_msg, std::vector<int>* error_locations = nullptr);
/** Preflight BEFORE wallet key derivation/index/database mutation. Both active
 * output families need capacity for a32byte program, not just metadata presence. */
pgn::core::NativeAddressPolicyStatus CheckNativeAddressWalletOutput(
    const std::optional<pgn::core::NativeAddressPolicy>& policy, OutputType type);

CKey DecodeSecret(const std::string& str);
std::string EncodeSecret(const CKey& key);

CExtKey DecodeExtKey(const std::string& str);
std::string EncodeExtKey(const CExtKey& extkey);
CExtPubKey DecodeExtPubKey(const std::string& str);
std::string EncodeExtPubKey(const CExtPubKey& extpubkey);

std::string EncodeDestination(const CTxDestination& dest);
CTxDestination DecodeDestination(const std::string& str);
CTxDestination DecodeDestination(const std::string& str, std::string& error_msg, std::vector<int>* error_locations = nullptr);
bool IsValidDestinationString(const std::string& str);
bool IsValidDestinationString(const std::string& str, const CChainParams& params);

#endif // BITCOIN_KEY_IO_H

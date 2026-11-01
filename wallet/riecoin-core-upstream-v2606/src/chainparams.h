// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAINPARAMS_H
#define BITCOIN_CHAINPARAMS_H

#include <kernel/chainparams.h> // IWYU pragma: export

#include <memory>

class ArgsManager;

/**
 * Creates and returns a std::unique_ptr<CChainParams> of the chosen chain.
 */
std::unique_ptr<const CChainParams> CreateChainParams(const ArgsManager& args, ChainType chain);

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests.
 */
const CChainParams &Params();

/** Copy the selected coherent PGN monetary metadata, or absent before selection
 * or on consensus/real-storage incompatibility.
 * Never assert/select parameters, initialize a node, or infer RIC defaults.
 */
std::optional<pgn::core::MonetaryContext> SelectedPgnMonetaryContext();
std::optional<pgn::core::AtomicFeeRatePolicy> SelectedPgnAtomicFeeRatePolicy();
std::optional<pgn::core::NativeAddressPolicy> SelectedPgnNativeAddressPolicy();
/** No RIC fallback; returns the immutable, real-storage-compatible consensus
 * domain only when one has actually been configured. Not an approval token. */
std::optional<Consensus::AmountRules> SelectedPgnConsensusAmountRules();

/** Existing chain's on-disk database tag, absent before parameter selection.
 * This is file identity, not PGN monetary metadata, chain approval or authority
 * to open another wallet. Never assert, select parameters or invent a tag. */
std::optional<uint32_t> SelectedChainDatabaseApplicationId();

/**
 * Sets the params returned by Params() to those for the given chain type.
 */
void SelectParams(ChainType chain);

#endif // BITCOIN_CHAINPARAMS_H

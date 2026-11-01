// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <pgn/monetary_context.h>

namespace pgn::core {
/** Capability supplied by a qualified storage schema, NEVER wallet settings,
 * network text or a single successful roundtrip. No default capability. */
struct AtomicStorageDomain {
    CAmount max_contiguous_atomic;
};
/** Capability of the actual inherited amount codec, supplied by compressor.cpp.
 * This is a representation boundary, NOT selected monetary policy or approval.
 * A future wide schema must change this producer together with its real codec. */
AtomicStorageDomain CurrentAmountStorageDomain();
enum class MonetaryBindingError {
    None, MissingConsensusDomain, MissingWalletContext, MissingStorageDomain,
    InvalidWalletContext, InvalidStorageDomain, TransferExceedsConsensus,
    ConsensusExceedsStorage, DisplayBelowConsensus
};
/** Storage safety is independent of wallet metadata. Missing domains remain
 * unavailable; a present consensus domain may never exceed the actual codec. */
inline MonetaryBindingError ValidateConsensusStorageBinding(
    const std::optional<Consensus::AmountRules>& consensus,
    const std::optional<AtomicStorageDomain>& storage)
{
    if (!consensus) return MonetaryBindingError::MissingConsensusDomain;
    if (!storage) return MonetaryBindingError::MissingStorageDomain;
    if (storage->max_contiguous_atomic <= 0)
        return MonetaryBindingError::InvalidStorageDomain;
    if (consensus->MaxAtomic() > storage->max_contiguous_atomic)
        return MonetaryBindingError::ConsensusExceedsStorage;
    return MonetaryBindingError::None;
}
/** Necessary consistency checks, NOT approval of a chain/economy/supply or
 * whole profile. Display may intentionally cover signed aggregates larger
 * than an individual transfer; storage must cover EVERY accepted output. */
inline MonetaryBindingError ValidateMonetaryBinding(
    const std::optional<Consensus::AmountRules>& consensus,
    const std::optional<MonetaryContext>& wallet,
    const std::optional<AtomicStorageDomain>& storage)
{
    if (!consensus) return MonetaryBindingError::MissingConsensusDomain;
    if (!wallet) return MonetaryBindingError::MissingWalletContext;
    if (!storage) return MonetaryBindingError::MissingStorageDomain;
    if (ValidateMonetaryContext(*wallet) != ExactAmountError::None)
        return MonetaryBindingError::InvalidWalletContext;
    const auto storage_error = ValidateConsensusStorageBinding(consensus, storage);
    if (storage_error != MonetaryBindingError::None) return storage_error;
    if (wallet->transfer.max_atomic > consensus->MaxAtomic())
        return MonetaryBindingError::TransferExceedsConsensus;
    if (wallet->display.max_atomic < consensus->MaxAtomic())
        return MonetaryBindingError::DisplayBelowConsensus;
    return MonetaryBindingError::None;
}
} // namespace pgn::core

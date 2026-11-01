// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <pgn/monetary_context.h>
#include <pgn/currency_constants.h>
#include <limits>

namespace pgn::core {
// An individual output/transaction cannot exceed total supply. This bound
// alone does NOT enforce lifetime issuance: subsidy and supply accounting must
// still be specified and checked by the eventual PGN consensus implementation.
inline constexpr Consensus::AmountRules ApprovedPgnAmountRules()
{
    return Consensus::AmountRules::ApprovedPgn();
}

// The caller must choose its own local text-work budget and validate the result.
// Display covers representable signed aggregates, not just a single transfer.
// This never selects a network or fills missing chain/provider metadata. In
// particular, fixed PGN COIN/MAX_MONEY do NOT supply missing provider/profile
// metadata. Historical RIC quantities have separate explicit reference names.
inline MonetaryContext ApprovedPgnMonetaryContext(ExactAmountTextBudget budget)
{
    return {{PGN_DECIMAL_PLACES, PGN_MAX_SUPPLY_ATOMIC},
            {PGN_DECIMAL_PLACES, std::numeric_limits<CAmount>::max()}, budget};
}
} // namespace pgn::core

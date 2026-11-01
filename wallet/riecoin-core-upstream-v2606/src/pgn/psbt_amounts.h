// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <consensus/amount.h>
#include <cstddef>
#include <optional>
class PartiallySignedTransaction;

namespace pgn::core {
enum class PsbtAmountStatus {
    Unavailable = 0, ArithmeticReadyUnverified, MissingDomain, MissingInputAmounts,
    InvalidStructure, InvalidInputReference, ConflictingInputMetadata,
    DuplicateInput, NegativeAmount, DomainExceeded, AmountOverflow, OutputsExceedInputs
};
struct PsbtAmounts { CAmount inputs; CAmount outputs; CAmount fee; };
struct PsbtAmountAdmission {
    PsbtAmountStatus status{PsbtAmountStatus::Unavailable};
    // All-or-none arithmetic tuple, not authentication of supplied UTXOs.
    std::optional<PsbtAmounts> amounts;
};
PsbtAmountAdmission AdmitPsbtAmounts(const PartiallySignedTransaction&,
    std::optional<Consensus::AmountRules>);
/** Public-data arithmetic only; CAmount capacity is NOT a consensus or supply
 * domain. This inspector may serve presentation, never network acceptance. */
PsbtAmountAdmission InspectPsbtAmounts(const PartiallySignedTransaction&);

/** A naked CFeeRate permits any nonnegative int32 GetFee size. Admit only a
 * ratio whose rounded-up query fits CAmount for ALL those sizes, including
 * GetFeePerK. This is representability, not relay/fee/economic policy. */
bool CanExposeFeeRatio(CAmount fee, std::size_t virtual_bytes);
/** One exact rounded-down nonnegative query, independently representable even
 * when the naked ratio cannot be safely queried at every int32 size. */
std::optional<CAmount> QueryFeeRatioDown(CAmount fee, std::size_t virtual_bytes,
                                       int32_t query_bytes);
} // namespace pgn::core

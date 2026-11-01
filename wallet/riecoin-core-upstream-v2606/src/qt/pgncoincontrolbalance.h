// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_COINCONTROLBALANCE_H
#define PGN_QT_COINCONTROLBALANCE_H

#include <interfaces/wallet.h>
#include <wallet/coincontrol.h>
#include <QObject>
#include <functional>
#include <optional>
#include <vector>

namespace pgn::qt {
enum class CoinControlBalanceStatus {
    Unavailable = 0, CandidateSnapshot, AmountStaged, WrongThread,
    OwnerUnavailable, MissingContext, ContextChanged, ProviderUnavailable,
    ProviderFailure, InvalidCandidates, MissingOtherAmount, InvalidOtherAmount,
    OtherAmountOverflow, InsufficientCandidates, Unrepresentable
};
struct CoinControlBalanceResult {
    CoinControlBalanceStatus status{CoinControlBalanceStatus::Unavailable};
    std::optional<interfaces::CoinControlBalanceSnapshot> snapshot;
    explicit operator bool() const { return status == CoinControlBalanceStatus::CandidateSnapshot && snapshot.has_value(); }
};
struct CoinControlAmountResult {
    CoinControlBalanceStatus status{CoinControlBalanceStatus::Unavailable};
    std::optional<CAmount> amount;
    explicit operator bool() const { return status == CoinControlBalanceStatus::AmountStaged && amount.has_value(); }
};
using CoinControlSnapshotQuery = std::function<std::optional<interfaces::CoinControlBalanceSnapshot>(const wallet::CCoinControl&)>;
using CoinControlContextQuery = std::function<std::optional<core::MonetaryContext>()>;
/** One synchronous copied query. Callbacks are local trusted code, never config.
 * Checks Qt owner/thread/request and the copied denomination after the query.
 * No event pump, cache, wallet construction, authentication or spend approval.
 * A native exception is a typed provider failure, never a candidate balance0.
 */
CoinControlBalanceResult QueryCoinControlBalance(
    QObject& owner, const std::optional<core::MonetaryContext>& expected,
    const wallet::CCoinControl&, const CoinControlSnapshotQuery&,
    const CoinControlContextQuery&, const std::function<bool()>& request_current);
// Amount cap is per transfer, NOT a multi-transaction candidate-balance cap.
CoinControlAmountResult SumTransferAmounts(const std::optional<core::MonetaryContext>&,
    const std::vector<std::optional<CAmount>>&);
CoinControlAmountResult StageCoinControlRemainder(const CoinControlBalanceResult&,
    const std::optional<core::MonetaryContext>& expected,
    const std::vector<std::optional<CAmount>>& other_amounts);
} // namespace pgn::qt
#endif

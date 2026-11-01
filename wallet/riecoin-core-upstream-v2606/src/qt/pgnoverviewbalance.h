// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_OVERVIEWBALANCE_H
#define PGN_QT_OVERVIEWBALANCE_H

#include <interfaces/wallet.h>
#include <qt/pgntransactionamount.h>

#include <QDateTime>
#include <QString>

#include <array>
#include <optional>

namespace pgn::qt {
enum class OverviewObservationState { Observed, RefreshRequired };
struct OverviewBalanceObservation {
    interfaces::WalletBalances balances;
    std::optional<core::MonetaryContext> context;
    QDateTime observed_at;
    OverviewObservationState state{OverviewObservationState::Observed};
};
enum class OverviewObservationUpdate { InvalidTime, First, Updated };
/** Frontend observation cache, NOT backend/network readiness or approval.
 * Production acceptSuccess is called ONLY after tryGetBalances returns true.
 * Public fixture injection qualifies this protocol, never a live wallet.
 */
class OverviewBalanceObservations final {
public:
    OverviewObservationUpdate acceptSuccess(const interfaces::WalletBalances&,
        std::optional<core::MonetaryContext>, const QDateTime& observed_at);
    bool requireRefresh(); // Retains last data/context/time; never invents0.
    std::optional<OverviewBalanceObservation> snapshot() const { return m_last; }
private:
    std::optional<OverviewBalanceObservation> m_last;
};

enum class OverviewBalanceStatus { Unavailable = 0, Observed, LastObserved, Hidden,
    MissingContext, InvalidContext, MissingObservation, ContextMismatch,
    InvalidObservation, InvalidBalances, TotalOverflow, Unrepresentable };
struct OverviewBalancePresentation {
    OverviewBalanceStatus status{OverviewBalanceStatus::Unavailable};
    // Available, pending, immature, total: all-or-none, never partial values.
    std::optional<std::array<QString, 4>> amounts;
    std::optional<QString> scale;
    std::optional<QDateTime> observed_at;
    std::optional<bool> immature_nonzero;
    explicit operator bool() const { return (status == OverviewBalanceStatus::Observed ||
        status == OverviewBalanceStatus::LastObserved) && amounts && scale && observed_at && immature_nonzero; }
};
OverviewBalancePresentation PresentOverviewBalances(
    const std::optional<OverviewBalanceObservation>&,
    const std::optional<core::MonetaryContext>& current_context, bool privacy);
} // namespace pgn::qt
#endif

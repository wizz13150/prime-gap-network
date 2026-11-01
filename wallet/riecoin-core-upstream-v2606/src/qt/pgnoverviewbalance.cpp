// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgnoverviewbalance.h>

#include <util/overflow.h>

#include <utility>

namespace pgn::qt {
OverviewObservationUpdate OverviewBalanceObservations::acceptSuccess(
    const interfaces::WalletBalances& balances, std::optional<core::MonetaryContext> context,
    const QDateTime& observed_at)
{
    if (!observed_at.isValid() || observed_at.timeSpec() != Qt::UTC) return OverviewObservationUpdate::InvalidTime;
    const bool first = !m_last;
    m_last = OverviewBalanceObservation{balances, std::move(context), observed_at, OverviewObservationState::Observed};
    return first ? OverviewObservationUpdate::First : OverviewObservationUpdate::Updated;
}

bool OverviewBalanceObservations::requireRefresh()
{
    if (!m_last || m_last->state == OverviewObservationState::RefreshRequired) return false;
    m_last->state = OverviewObservationState::RefreshRequired;
    return true;
}

OverviewBalancePresentation PresentOverviewBalances(const std::optional<OverviewBalanceObservation>& observation,
    const std::optional<core::MonetaryContext>& context, bool privacy)
{
    OverviewBalancePresentation result;
    // No amount, availability, scale, sign, timestamp or immature-layout leak.
    if (privacy) { result.status = OverviewBalanceStatus::Hidden; return result; }
    if (!context) { result.status = OverviewBalanceStatus::MissingContext; return result; }
    if (core::ValidateMonetaryContext(*context) != core::ExactAmountError::None) {
        result.status = OverviewBalanceStatus::InvalidContext; return result;
    }
    if (!observation) { result.status = OverviewBalanceStatus::MissingObservation; return result; }
    if (!SameTransactionMonetaryContext(observation->context, context)) {
        result.status = OverviewBalanceStatus::ContextMismatch; return result;
    }
    if (!observation->observed_at.isValid() || observation->observed_at.timeSpec() != Qt::UTC ||
        (observation->state != OverviewObservationState::Observed && observation->state != OverviewObservationState::RefreshRequired)) {
        result.status = OverviewBalanceStatus::InvalidObservation; return result;
    }
    const auto& data = observation->balances;
    if (data.balance < 0 || data.unconfirmed_balance < 0 || data.immature_balance < 0) {
        result.status = OverviewBalanceStatus::InvalidBalances; return result;
    }
    const auto pending = CheckedAdd(data.balance, data.unconfirmed_balance);
    const auto total = pending ? CheckedAdd(*pending, data.immature_balance) : std::nullopt;
    if (!total) { result.status = OverviewBalanceStatus::TotalOverflow; return result; }
    const std::array<CAmount, 4> atoms{data.balance, data.unconfirmed_balance, data.immature_balance, *total};
    std::array<QString, 4> amounts;
    for (size_t i = 0; i != atoms.size(); ++i) {
        const auto amount = PresentTransactionAtoms(QVariant::fromValue<qint64>(atoms[i]), context, true, false);
        if (!amount) { result.status = OverviewBalanceStatus::Unrepresentable; return result; }
        amounts[i] = *amount.display;
    }
    result.status = observation->state == OverviewObservationState::Observed ? OverviewBalanceStatus::Observed : OverviewBalanceStatus::LastObserved;
    result.amounts = std::move(amounts);
    result.scale = TransactionScaleText(context);
    result.observed_at = observation->observed_at;
    result.immature_nonzero = data.immature_balance != 0;
    return result;
}
} // namespace pgn::qt

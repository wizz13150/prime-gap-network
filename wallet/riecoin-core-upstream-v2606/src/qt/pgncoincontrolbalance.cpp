// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgncoincontrolbalance.h>
#include <qt/pgntransactionamount.h>
#include <QPointer>
#include <QThread>
#include <exception>
#include <limits>

namespace pgn::qt {
namespace {
bool UsableContext(const std::optional<core::MonetaryContext>& context)
{
    return context && core::ValidateMonetaryContext(*context) == core::ExactAmountError::None &&
        context->text_budget.max_characters <= static_cast<uint64_t>(std::numeric_limits<int>::max());
}
}
CoinControlBalanceResult QueryCoinControlBalance(
    QObject& owner, const std::optional<core::MonetaryContext>& expected,
    const wallet::CCoinControl& control, const CoinControlSnapshotQuery& query,
    const CoinControlContextQuery& current_context, const std::function<bool()>& request_current)
{
    using S = CoinControlBalanceStatus;
    const QPointer<QObject> alive(&owner);
    const auto captured = expected;
    if (QThread::currentThread() != owner.thread()) return {S::WrongThread, {}};
    if (!UsableContext(captured)) return {S::MissingContext, {}};
    if (!query || !current_context || !request_current) return {S::ProviderUnavailable, {}};
    try {
        if (!request_current() || !alive) return {S::OwnerUnavailable, {}};
        if (QThread::currentThread() != alive->thread()) return {S::WrongThread, {}};
        const wallet::CCoinControl copied_control{control};
        const auto result = query(copied_control);
        if (!alive || !request_current() || !alive) return {S::OwnerUnavailable, {}};
        if (QThread::currentThread() != alive->thread()) return {S::WrongThread, {}};
        const auto current = current_context();
        if (!alive || !request_current() || !alive) return {S::OwnerUnavailable, {}};
        if (QThread::currentThread() != alive->thread()) return {S::WrongThread, {}};
        if (!UsableContext(current) || !SameTransactionMonetaryContext(captured, current)) return {S::ContextChanged, {}};
        if (!result) return {S::ProviderUnavailable, {}};
        if (!UsableContext(result->context) || !SameTransactionMonetaryContext(captured, result->context)) return {S::ContextChanged, {}};
        if (result->amount < 0) return {S::InvalidCandidates, {}};
        return {S::CandidateSnapshot, result};
    } catch (const std::exception&) {
        return {S::ProviderFailure, {}};
    }
}
CoinControlAmountResult SumTransferAmounts(const std::optional<core::MonetaryContext>& context,
    const std::vector<std::optional<CAmount>>& amounts)
{
    using S = CoinControlBalanceStatus;
    if (!UsableContext(context)) return {S::MissingContext, {}};
    CAmount sum{0};
    for (const auto& value : amounts) {
        if (!value) return {S::MissingOtherAmount, {}};
        if (*value < 0 || *value > context->transfer.max_atomic) return {S::InvalidOtherAmount, {}};
        if (*value > std::numeric_limits<CAmount>::max() - sum) return {S::OtherAmountOverflow, {}};
        sum += *value;
    }
    return {S::AmountStaged, sum};
}
CoinControlAmountResult StageCoinControlRemainder(const CoinControlBalanceResult& candidate,
    const std::optional<core::MonetaryContext>& expected,
    const std::vector<std::optional<CAmount>>& other_amounts)
{
    using S = CoinControlBalanceStatus;
    if (!candidate) return {candidate.status == S::CandidateSnapshot ? S::Unavailable : candidate.status, {}};
    if (!UsableContext(expected) || !SameTransactionMonetaryContext(expected, candidate.snapshot->context)) return {S::ContextChanged, {}};
    if (candidate.snapshot->amount < 0) return {S::InvalidCandidates, {}};
    const auto total = SumTransferAmounts(expected, other_amounts);
    if (!total) return total;
    if (candidate.snapshot->amount <= *total.amount) return {S::InsufficientCandidates, {}};
    const CAmount remainder = candidate.snapshot->amount - *total.amount;
    const auto formatted = core::formatExactAmount(remainder, core::ExactAmountFormat::Transfer,
        expected->transfer, expected->text_budget);
    if (!formatted.text || formatted.error != core::ExactAmountError::None) return {S::Unrepresentable, {}};
    return {S::AmountStaged, remainder};
}
} // namespace pgn::qt

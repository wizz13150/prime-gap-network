// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgncurrencyfeeratefield.h>
#include <QThread>

PgnCurrencyFeeRateField::PgnCurrencyFeeRateField(QWidget* parent)
    : PgnExactAmountField(pgn::qt::ExactAmountTextBudget{0},parent)
{
    setAlignment(Qt::AlignRight);
    setPlaceholderText(tr("Unavailable"));
    connect(this,&PgnExactAmountField::amountStateChanged,this,&PgnCurrencyFeeRateField::feeRateStateChanged);
}
bool PgnCurrencyFeeRateField::clearRateContext()
{
    if (QThread::currentThread() != thread()) return false;
    ++m_rate_revision;
    m_core_owner.clear(); m_context.reset(); m_rate_policy.reset();
    setPlaceholderText(tr("Unavailable"));
    return clearAmountPolicy();
}
bool PgnCurrencyFeeRateField::bindRateContext(QObject& owner,
    std::optional<pgn::core::MonetaryContext> context,
    std::optional<pgn::core::AtomicFeeRatePolicy> rate)
{
    if (QThread::currentThread() != thread()) return false;
    if (m_core_owner == &owner && configured() &&
        pgn::qt::SameCurrencyFeeRateMetadata(m_context,m_rate_policy,context,rate)) return true;
    const QPointer<PgnCurrencyFeeRateField> alive(this);
    const QPointer<QObject> candidate(&owner);
    const auto cleared_revision = m_rate_revision + 1;
    clearRateContext(); // Metadata and text invalid before any new publication.
    if (!alive || m_rate_revision != cleared_revision || !candidate || owner.thread() != thread()) return false;
    const auto preview = pgn::qt::PreviewCurrencyFeeRatePolicy(context,rate);
    if (!preview) return false;
    m_core_owner = candidate; m_context = std::move(context); m_rate_policy = std::move(rate);
    if (!setNumericPolicyFromCoreOwner(owner,preview->numeric,preview->budget) || !alive ||
        m_rate_revision != cleared_revision || !m_core_owner || m_core_owner != candidate || !configured()) return false;
    setPlaceholderText(tr("Exact currency/kvB"));
    return true;
}
bool PgnCurrencyFeeRateField::configured() const
{
    return QThread::currentThread() == thread() && m_core_owner && m_core_owner->thread() == thread() &&
        amountResult().state != pgn::qt::ExactAmountFieldState::Unavailable &&
        pgn::qt::PreviewCurrencyFeeRatePolicy(m_context,m_rate_policy).has_value();
}
pgn::qt::CurrencyFeeRateResult PgnCurrencyFeeRateField::feeRateResult(QObject& expected_owner,
    const std::optional<pgn::core::MonetaryContext>& context,
    const std::optional<pgn::core::AtomicFeeRatePolicy>& rate) const
{
    using S = pgn::qt::CurrencyFeeRateStatus;
    if (QThread::currentThread() != thread()) return {S::WrongThread,{},{}};
    if (!configured() || m_core_owner != &expected_owner) return {S::OwnerUnavailable,{},{}};
    if (!pgn::qt::SameCurrencyFeeRateMetadata(m_context,m_rate_policy,context,rate)) return {S::ContextChanged,{},{}};
    const QPointer<QObject> owner(m_core_owner);
    const auto revision = m_rate_revision;
    const auto monetary = m_context;
    const auto policy = m_rate_policy;
    const auto result = pgn::qt::AdmitCurrencyFeeRate(text(),monetary,policy);
    if (!owner || owner != m_core_owner || revision != m_rate_revision) return {S::ContextChanged,{},{}};
    return result;
}
QString PgnCurrencyFeeRateField::unitText() const
{
    return configured() ? tr("units/kvB (10^%1 atoms/unit)").arg(m_context->transfer.decimals) : tr("Unavailable: fee-rate policy/context");
}

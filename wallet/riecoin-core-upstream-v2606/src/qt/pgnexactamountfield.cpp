// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.

#include <qt/pgnexactamountfield.h>

#include <QSignalBlocker>
#include <QThread>
#include <QValidator>

#include <limits>
#include <utility>

namespace pgn::qt {

class PgnExactAmountField::InputValidator final : public QValidator {
public:
    explicit InputValidator(PgnExactAmountField& field) : QValidator(&field), m_field(field) {}

    State validate(QString& input, int& position) const override
    {
        // No rewrites, locale normalization, double conversion or rounding.
        if (!m_field.onOwnerThread() || !m_field.m_owner || m_field.m_owner->thread() != m_field.thread() || m_field.m_codec->policyStatus() != ExactAmountError::None) return Invalid;
        if (input.size() > m_field.m_budget.max_characters) return Invalid;
        if (m_field.m_codec->parse(input)) return Acceptable;
        if (input.isEmpty()) return Intermediate;
        // A trailing decimal point is editable, but is NOT an accepted amount.
        if (m_field.m_policy->decimals != 0 && input.endsWith(QLatin1Char('.')) &&
            m_field.m_codec->parse(input.first(input.size() - 1))) return Intermediate;
        return Invalid;
    }

private:
    PgnExactAmountField& m_field;
};

PgnExactAmountField::PgnExactAmountField(ExactAmountTextBudget budget, QWidget* parent)
    : QLineEdit(parent), m_budget(budget), m_codec(std::make_unique<ExactAmountCodec>(std::nullopt, budget))
{
    // Let the exact validator reject budget overflow rather than silently
    // truncating text at a smaller QLineEdit default. Qt's int-sized text API
    // remains a UI storage boundary, never a monetary precision rule.
    setMaxLength(std::numeric_limits<int>::max());
    setValidator(new InputValidator(*this));
    setReadOnly(true);
    setEnabled(false);
    connect(this, &QLineEdit::textChanged, this, [this] { Q_EMIT amountStateChanged(); });
}

bool PgnExactAmountField::onOwnerThread() const
{
    return QThread::currentThread() == thread();
}

bool PgnExactAmountField::setPolicyFromCoreOwner(QObject& owner, std::optional<ExactAmountPolicy> policy)
{
    if (!onOwnerThread() || owner.thread() != thread()) return false;
    return applyPolicy(&owner, std::move(policy), m_budget, ExactAmountUnitMode::RequiredSymbol);
}

bool PgnExactAmountField::setNumericPolicyFromCoreOwner(QObject& owner, std::optional<core::ExactAmountPolicy> policy, core::ExactAmountTextBudget budget)
{
    if (!onOwnerThread()) return false;
    if (owner.thread() != thread()) { clearAmountPolicy(); return false; }
    const ExactAmountTextBudget qt_budget{budget.max_characters <= static_cast<uint64_t>(std::numeric_limits<int>::max()) ? static_cast<qsizetype>(budget.max_characters) : 0};
    std::optional<ExactAmountPolicy> numeric;
    if (policy) numeric = ExactAmountPolicy{policy->decimals, policy->max_atomic, QString{}};
    return applyPolicy(&owner, std::move(numeric), qt_budget, ExactAmountUnitMode::NumericOnly);
}

bool PgnExactAmountField::clearAmountPolicy()
{
    if (!onOwnerThread()) return false;
    return applyPolicy(nullptr, std::nullopt, m_budget, m_unit_mode);
}

bool PgnExactAmountField::applyPolicy(QObject* owner, std::optional<ExactAmountPolicy> policy, ExactAmountTextBudget budget, ExactAmountUnitMode mode)
{
    // QWidget setters can synchronously dispatch an event filter which deletes
    // either owner or this field. Capture both lifetimes BEFORE any callback;
    // never reuse the borrowed owner pointer after such a boundary.
    const QPointer<PgnExactAmountField> self(this);
    const QPointer<QObject> candidate(owner);
    const bool owner_requested = owner != nullptr;
    auto codec = std::make_unique<ExactAmountCodec>(policy, budget, mode);
    const quint64 revision = ++m_revision;
    disconnect(m_owner_destroyed);
    if (!self || m_revision != revision) return false;
    // Block transient textChanged observers: old text must never be parsed
    // under a new scale. Do not leave a raw-QObject QSignalBlocker spanning
    // callbacks: its destructor would touch a field deleted by an event filter.
    const bool was_blocked = blockSignals(true);
    // clear() is subject to the validator and can retain text after owner loss;
    // setText invalidates it regardless of editing validity.
    QLineEdit::setText(QString{});
    if (!self) return false;
    blockSignals(was_blocked);
    if (m_revision != revision) return false;
    m_budget = budget;
    m_unit_mode = mode;
    m_owner = candidate && candidate->thread() == thread() ? candidate : QPointer<QObject>{};
    m_policy = std::move(policy);
    m_codec = std::move(codec);
    if (m_owner) {
        const auto connection = connect(m_owner.data(), &QObject::destroyed, self.data(), [self] {
            if (self) self->clearAmountPolicy();
        });
        if (!self || m_revision != revision) {
            QObject::disconnect(connection);
            return false;
        }
        m_owner_destroyed = connection;
    }
    const bool available = m_owner && m_codec->policyStatus() == ExactAmountError::None;
    setReadOnly(!available);
    if (!self || m_revision != revision) return false;
    setEnabled(available);
    if (!self || m_revision != revision) return false;
    if (owner_requested && (!candidate || m_owner != candidate || candidate->thread() != thread())) {
        clearAmountPolicy();
        return false;
    }
    // Signals may legitimately revoke policy or destroy this widget. Never
    // publish the older state again after such a callback.
    Q_EMIT policyInvalidated(revision);
    if (self && m_revision == revision) Q_EMIT amountStateChanged();
    return true; // Applied, not a claim that policy is valid/approved.
}

ExactAmountFieldResult PgnExactAmountField::amountResult() const
{
    if (!onOwnerThread()) return {ExactAmountFieldState::WrongThread, ExactAmountError::None, std::nullopt};
    if (!m_owner || m_owner->thread() != thread()) return {ExactAmountFieldState::Unavailable, ExactAmountError::MissingPolicy, std::nullopt};
    if (m_codec->policyStatus() != ExactAmountError::None) return {ExactAmountFieldState::Unavailable, m_codec->policyStatus(), std::nullopt};
    const auto parsed = m_codec->parse(text());
    return {parsed ? ExactAmountFieldState::Valid : ExactAmountFieldState::InvalidText, parsed.error, parsed.amount};
}

std::optional<QString> PgnExactAmountField::unitSymbol() const
{
    if (!onOwnerThread() || !m_owner || m_owner->thread() != thread() || m_codec->policyStatus() != ExactAmountError::None) return std::nullopt;
    if (m_unit_mode == ExactAmountUnitMode::NumericOnly) return std::nullopt;
    return m_policy->symbol;
}

bool PgnExactAmountField::setAtomicAmount(CAmount amount)
{
    if (!onOwnerThread()) return false;
    const auto formatted = m_codec->format(amount, ExactAmountFormat::Transfer, false);
    const bool accepted = m_owner && m_owner->thread() == thread() && static_cast<bool>(formatted);
    {
        const QSignalBlocker blocked(this);
        QLineEdit::setText(accepted ? *formatted.text : QString{});
    }
    Q_EMIT amountStateChanged();
    return accepted;
}

} // namespace pgn::qt

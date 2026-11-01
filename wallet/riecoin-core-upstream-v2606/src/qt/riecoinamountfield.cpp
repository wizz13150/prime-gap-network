// Copyright (c) The Riecoin developers
// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/riecoinamountfield.h>
#include <qt/guiconstants.h>
#include <qt/pgntransactionamount.h>
#include <QSignalBlocker>
#include <QThread>

RiecoinAmountField::RiecoinAmountField(QWidget* parent)
    : PgnExactAmountField(pgn::qt::ExactAmountTextBudget{0}, parent)
{
    setMinimumWidth(144);
    setAlignment(Qt::AlignRight);
    setPlaceholderText(tr("Unavailable"));
    connect(this, &PgnExactAmountField::amountStateChanged, this, [this] {
        QPointer<RiecoinAmountField> self(this);
        validate();
        if (self) Q_EMIT valueChanged();
    });
}

bool RiecoinAmountField::setTransferContext(QObject& owner, std::optional<pgn::core::MonetaryContext> context)
{
    if (QThread::currentThread() != thread()) return false;
    if (m_purpose != Purpose::UnsupportedFeeRate) m_purpose = Purpose::Unconfigured;
    m_context.reset();
    m_core_owner.clear();
    m_minimum.reset();
    if (m_purpose == Purpose::UnsupportedFeeRate || owner.thread() != thread() || !context ||
        pgn::core::ValidateMonetaryContext(*context) != pgn::core::ExactAmountError::None) {
        clearAmountPolicy();
        return false;
    }
    m_context = context;
    m_core_owner = &owner;
    m_purpose = Purpose::Transfer;
    return setNumericPolicyFromCoreOwner(owner, context->transfer, context->text_budget);
}

void RiecoinAmountField::clearTransferContext()
{
    if (QThread::currentThread() != thread()) return;
    m_context.reset();
    m_core_owner.clear();
    m_minimum.reset();
    if (m_purpose != Purpose::UnsupportedFeeRate) m_purpose = Purpose::Unconfigured;
    clearAmountPolicy();
}

void RiecoinAmountField::setUnsupportedFeeRate()
{
    QPointer<RiecoinAmountField> self(this);
    m_purpose = Purpose::UnsupportedFeeRate;
    clearTransferContext();
    if (self) setToolTip(tr("Unavailable: fee-rate input requires a separate atomic-units/kvB policy."));
}

std::optional<pgn::core::MonetaryContext> RiecoinAmountField::transferContext() const
{
    if (QThread::currentThread() != thread() || m_purpose != Purpose::Transfer || !m_core_owner || m_core_owner->thread() != thread()) return std::nullopt;
    const auto state = amountResult().state;
    if (state == pgn::qt::ExactAmountFieldState::Unavailable || state == pgn::qt::ExactAmountFieldState::WrongThread) return std::nullopt;
    return m_context;
}

QString RiecoinAmountField::scaleText() const
{
    const auto context = transferContext();
    return context ? QStringLiteral("10^%1 atoms/unit").arg(context->transfer.decimals) : tr("Unavailable");
}

std::optional<CAmount> RiecoinAmountField::value() const
{
    if (!transferContext()) return std::nullopt;
    const auto result = amountResult();
    if (!result || (m_minimum && (*m_minimum < 0 || *result.amount < *m_minimum))) return std::nullopt;
    return result.amount;
}

bool RiecoinAmountField::validate()
{
    if (QThread::currentThread() != thread()) return false;
    const bool valid = transferContext() && ((m_optional && text().isEmpty()) || value().has_value());
    QPointer<RiecoinAmountField> self(this);
    setPlaceholderText(transferContext() ? tr("Exact amount") : tr("Unavailable"));
    if (self) setStyleSheet(valid ? QString{} : STYLE_INVALID);
    return valid;
}

bool RiecoinAmountField::setValue(CAmount value)
{
    if (!transferContext()) { clearTransferContext(); return false; }
    return setAtomicAmount(value);
}

bool RiecoinAmountField::trySetValuePreservingInput(CAmount amount, const pgn::core::MonetaryContext& expected)
{
    if (QThread::currentThread() != thread() ||
        !pgn::qt::SameTransactionMonetaryContext(transferContext(), expected) ||
        (m_minimum && (*m_minimum < 0 || amount < *m_minimum))) return false;
    const auto formatted = pgn::core::formatExactAmount(amount, pgn::core::ExactAmountFormat::Transfer,
        expected.transfer, expected.text_budget);
    if (!formatted.text || formatted.error != pgn::core::ExactAmountError::None) return false;
    const QString previous = text();
    {
        // No callbacks during prevalidated text replacement or rollback.
        const QSignalBlocker blocked(this);
        if (!setValue(amount) || value() != std::optional<CAmount>{amount}) {
            QLineEdit::setText(previous);
            return false;
        }
    }
    Q_EMIT amountStateChanged();
    return true; // Notification follows a coherent commit, never a partial edit.
}

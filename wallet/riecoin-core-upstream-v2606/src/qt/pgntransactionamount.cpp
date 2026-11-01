// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgntransactionamount.h>

#include <util/overflow.h>
#include <QThread>
#include <QScopedValueRollback>

#include <utility>

namespace pgn::qt {
bool SameTransactionMonetaryContext(const std::optional<core::MonetaryContext>& a,
                                   const std::optional<core::MonetaryContext>& b)
{
    if (a.has_value() != b.has_value()) return false;
    if (!a) return true;
    return a->transfer.decimals == b->transfer.decimals && a->transfer.max_atomic == b->transfer.max_atomic &&
        a->display.decimals == b->display.decimals && a->display.max_atomic == b->display.max_atomic &&
        a->text_budget.max_characters == b->text_budget.max_characters;
}

std::optional<core::MonetaryContext> TransactionMonetaryContext::snapshot() const
{
    if (thread() != QThread::currentThread()) return std::nullopt;
    return m_snapshot;
}

TransactionContextUpdate TransactionMonetaryContext::replace(std::optional<core::MonetaryContext> next)
{
    if (thread() != QThread::currentThread()) return TransactionContextUpdate::WrongThread;
    if (m_replacing) return TransactionContextUpdate::Reentrant;
    if (SameTransactionMonetaryContext(m_snapshot, next)) return TransactionContextUpdate::Unchanged;
    const QScopedValueRollback<bool> updating(m_replacing, true);
    Q_EMIT aboutToChange(); // snapshot() is STILL old here.
    m_snapshot = std::move(next);
    Q_EMIT changed(); // snapshot() is now new, including absent/invalid.
    return TransactionContextUpdate::Changed;
}

namespace {
std::optional<CAmount> ExactAtoms(const QVariant& value)
{
    if (!value.isValid() || value.isNull() || value.metaType().id() != QMetaType::LongLong) return std::nullopt;
    return value.value<qint64>();
}
TransactionAmountStatus ContextStatus(const std::optional<core::MonetaryContext>& context)
{
    if (!context) return TransactionAmountStatus::MissingContext;
    if (core::ValidateMonetaryContext(*context) != core::ExactAmountError::None) return TransactionAmountStatus::InvalidContext;
    return TransactionAmountStatus::Ready;
}
} // namespace

QVariant TransactionNetAtoms(CAmount credit, CAmount debit)
{
    const auto atoms = CheckedAdd(credit, debit);
    return atoms ? QVariant::fromValue<qint64>(*atoms) : QVariant{};
}

TransactionAmountPresentation PresentTransactionAtoms(const QVariant& value,
    const std::optional<core::MonetaryContext>& context, bool grouped, bool bracketed)
{
    TransactionAmountPresentation result;
    result.atoms = ExactAtoms(value);
    if (!result.atoms) {
        result.status = value.isValid() && !value.isNull() ? TransactionAmountStatus::InvalidAtoms : TransactionAmountStatus::MissingAtoms;
        return result;
    }
    result.status = ContextStatus(context);
    if (result.status != TransactionAmountStatus::Ready) return result;
    const auto formatted = core::formatExactAmount(*result.atoms, core::ExactAmountFormat::Delta,
        context->display, context->text_budget);
    if (formatted.error != core::ExactAmountError::None || !formatted.text) {
        result.status = TransactionAmountStatus::Unrepresentable;
        result.codec_error = formatted.error;
        return result;
    }
    const QString canonical = QString::fromLatin1(formatted.text->data(), static_cast<qsizetype>(formatted.text->size()));
    QString display = canonical;
    if (grouped) {
        const qsizetype start = display.startsWith(QLatin1Char('-')) ? 1 : 0;
        const qsizetype dot = display.indexOf(QLatin1Char('.'));
        const qsizetype end = dot == -1 ? display.size() : dot;
        for (qsizetype position = end - 3; position > start; position -= 3) display.insert(position, QChar(0x2009));
    }
    if (bracketed) display = QLatin1Char('[') + display + QLatin1Char(']');
    if (static_cast<uint64_t>(display.size()) > context->text_budget.max_characters) {
        result.status = TransactionAmountStatus::Unrepresentable;
        result.codec_error = core::ExactAmountError::BudgetExceeded;
        return result;
    }
    result.canonical = canonical;
    result.display = display;
    return result;
}

TransactionAmountPresentation PresentTransactionNet(CAmount credit, CAmount debit,
    const std::optional<core::MonetaryContext>& context, bool grouped, bool bracketed)
{
    const auto atoms = TransactionNetAtoms(credit, debit);
    if (atoms.isValid()) return PresentTransactionAtoms(atoms, context, grouped, bracketed);
    TransactionAmountPresentation result;
    result.status = TransactionAmountStatus::NetOverflow;
    return result;
}

std::optional<QString> TransactionScaleText(const std::optional<core::MonetaryContext>& context)
{
    if (ContextStatus(context) != TransactionAmountStatus::Ready) return std::nullopt;
    return QStringLiteral("10^%1 atoms/unit").arg(context->display.decimals);
}

bool TransactionAmountMeetsMinimum(const QVariant& value, const std::optional<CAmount>& minimum)
{
    const auto atoms = ExactAtoms(value);
    if (!atoms || !minimum || *minimum < 0) return true;
    const uint64_t magnitude = *atoms < 0 ? static_cast<uint64_t>(-(*atoms + 1)) + 1 : static_cast<uint64_t>(*atoms);
    return magnitude >= static_cast<uint64_t>(*minimum);
}

TransactionAmountStatus CopyTransactionMonetaryText(const QVariant& atoms,
    const std::optional<core::MonetaryContext>& context, TransactionCopyContent content,
    const std::optional<QString>& details, const std::function<void(const QString&)>& sink)
{
    const auto amount = PresentTransactionAtoms(atoms, context, false, false);
    if (!amount) return amount.status;
    if (!sink) return TransactionAmountStatus::Unavailable;
    switch (content) {
    case TransactionCopyContent::Amount:
        sink(*amount.canonical);
        return TransactionAmountStatus::Ready;
    case TransactionCopyContent::Details:
        if (!details || details->isEmpty() || !details->isValidUtf16() || details->contains(QChar(u'\0'))) return TransactionAmountStatus::InvalidText;
        sink(*details);
        return TransactionAmountStatus::Ready;
    }
    return TransactionAmountStatus::InvalidText;
}

TransactionMinimumAmount::TransactionMinimumAmount(std::optional<core::MonetaryContext> context)
    : m_context(std::move(context)) {}

MinimumAmountResult TransactionMinimumAmount::apply(const QString& text)
{
    if (ContextStatus(m_context) != TransactionAmountStatus::Ready) {
        return {MinimumAmountStatus::Unavailable, m_minimum,
            m_context ? core::ExactAmountError::InvalidPolicy : core::ExactAmountError::MissingPolicy};
    }
    if (text.isEmpty()) {
        m_minimum.reset();
        return {MinimumAmountStatus::Disabled, m_minimum, std::nullopt};
    }
    if (static_cast<uint64_t>(text.size()) > m_context->text_budget.max_characters) {
        return {MinimumAmountStatus::Invalid, m_minimum, core::ExactAmountError::BudgetExceeded};
    }
    for (QChar ch : text) if (ch.unicode() > 127) return {MinimumAmountStatus::Invalid, m_minimum, core::ExactAmountError::InvalidText};
    const auto parsed = core::parseExactAmount(text.toStdString(), m_context->display, m_context->text_budget);
    if (parsed.error != core::ExactAmountError::None || !parsed.amount) return {MinimumAmountStatus::Invalid, m_minimum, parsed.error};
    m_minimum = parsed.amount;
    return {MinimumAmountStatus::Applied, m_minimum, std::nullopt};
}
} // namespace pgn::qt

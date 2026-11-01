// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgntransactiondescamount.h>

#include <util/overflow.h>
#include <util/strencodings.h>

#include <limits>

namespace pgn::qt {
void TransactionDescAmounts::requireContext() const
{
    if (!m_context) throw TransactionDescAmountFailure{TransactionDescAmountError::MissingContext};
    if (core::ValidateMonetaryContext(*m_context) != core::ExactAmountError::None)
        throw TransactionDescAmountFailure{TransactionDescAmountError::InvalidContext};
}

void TransactionDescAmounts::requireSnapshot(const interfaces::WalletTx& data) const
{
    if (!data.tx) throw TransactionDescAmountFailure{TransactionDescAmountError::MissingTransaction};
    if (data.txin_is_mine.size() != data.tx->vin.size() || data.txout_is_mine.size() != data.tx->vout.size())
        throw TransactionDescAmountFailure{TransactionDescAmountError::InvalidShape};
    nonnegative(data.credit);
    nonnegative(data.debit); // WalletTx debit is a positive magnitude, not TransactionRecord debit.
    nonnegative(data.change);
    // Validate the observed magnitudes too, not merely a small net obtained by
    // cancellation. A description never conceals an out-of-policy component.
    number(data.credit, core::ExactAmountFormat::Transfer);
    number(data.debit, core::ExactAmountFormat::Transfer);
    number(data.change, core::ExactAmountFormat::Transfer);
    CAmount total{};
    for (const auto& out : data.tx->vout) {
        number(nonnegative(out.nValue), core::ExactAmountFormat::Transfer);
        total = sumNonnegative(total, out.nValue);
    }
    number(total, core::ExactAmountFormat::Transfer);
    orderedDifference(data.credit, data.change);
}

CAmount TransactionDescAmounts::nonnegative(CAmount value)
{
    if (value < 0) throw TransactionDescAmountFailure{TransactionDescAmountError::InvalidMagnitude};
    return value;
}

CAmount TransactionDescAmounts::add(CAmount a, CAmount b)
{
    const auto value = CheckedAdd(a, b);
    if (!value) throw TransactionDescAmountFailure{TransactionDescAmountError::ArithmeticOverflow};
    return *value;
}

CAmount TransactionDescAmounts::subtract(CAmount a, CAmount b)
{
    // Do not implement as add(a,-b): b may be INT64_MIN.
    if ((b > 0 && a < std::numeric_limits<CAmount>::min() + b) ||
        (b < 0 && a > std::numeric_limits<CAmount>::max() + b))
        throw TransactionDescAmountFailure{TransactionDescAmountError::ArithmeticOverflow};
    return a - b;
}

CAmount TransactionDescAmounts::negate(CAmount value)
{
    if (value == std::numeric_limits<CAmount>::min())
        throw TransactionDescAmountFailure{TransactionDescAmountError::ArithmeticOverflow};
    return -value;
}

CAmount TransactionDescAmounts::sumNonnegative(CAmount a, CAmount b)
{
    return add(nonnegative(a), nonnegative(b));
}

CAmount TransactionDescAmounts::orderedDifference(CAmount a, CAmount b)
{
    nonnegative(a);
    nonnegative(b);
    if (a < b) throw TransactionDescAmountFailure{TransactionDescAmountError::IncoherentAmounts};
    return subtract(a, b);
}

QString TransactionDescAmounts::number(CAmount value, core::ExactAmountFormat mode) const
{
    requireContext();
    const auto formatted = core::formatExactAmount(value, mode, m_context->display, m_context->text_budget);
    if (formatted.error != core::ExactAmountError::None || !formatted.text)
        throw TransactionDescAmountFailure{TransactionDescAmountError::Unrepresentable};
    // Canonical ASCII from Core; no implicit symbol, grouping, RIC8 or double.
    return QString::fromLatin1(formatted.text->data(), static_cast<qsizetype>(formatted.text->size()));
}

QString TransactionDescAmounts::html(CAmount value, core::ExactAmountFormat mode) const
{
    return QStringLiteral("<span style='white-space: nowrap;'>%1</span>").arg(number(value, mode).toHtmlEscaped());
}

QString TransactionDescAmounts::scaleHtml() const
{
    requireContext();
    const auto text = QStringLiteral("10^%1 atoms/unit").arg(m_context->display.decimals);
    return QStringLiteral("<span style='white-space: nowrap;'>%1</span>").arg(text.toHtmlEscaped());
}

QString TransactionDescUnavailableHtml(const QString& label, const QString& reason)
{
    return QStringLiteral("<html><font face='verdana, arial, helvetica, sans-serif'><b>%1:</b> %2</font></html>")
        .arg(label.toHtmlEscaped(), reason.toHtmlEscaped());
}

QString TransactionDescAmounts::debugHtml(const CTransaction& tx) const
{
    // Same essential fields and disclosure limits as CTransaction::ToString:
    // ten hash characters, original input/witness strings, thirty script hex
    // characters. Do not invoke CTxOut::ToString's inherited COIN/fixed8 path.
    QString text = QStringLiteral("CTransaction(hash=%1, ver=%2, vin.size=%3, vout.size=%4, nLockTime=%5)\n")
        .arg(QString::fromStdString(tx.GetHash().ToString().substr(0, 10)))
        .arg(tx.version).arg(tx.vin.size()).arg(tx.vout.size()).arg(tx.nLockTime);
    for (const auto& in : tx.vin) text += QStringLiteral("    ") + QString::fromStdString(in.ToString()) + QLatin1Char('\n');
    for (const auto& in : tx.vin) text += QStringLiteral("    ") + QString::fromStdString(in.scriptWitness.ToString()) + QLatin1Char('\n');
    for (const auto& out : tx.vout) {
        text += QStringLiteral("    CTxOut(nValue=%1, scriptPubKey=%2)\n")
            .arg(number(nonnegative(out.nValue), core::ExactAmountFormat::Transfer),
                 QString::fromStdString(HexStr(out.scriptPubKey).substr(0, 30)));
    }
    return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}
} // namespace pgn::qt

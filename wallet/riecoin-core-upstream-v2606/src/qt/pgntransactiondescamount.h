// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_TRANSACTIONDESCAMOUNT_H
#define PGN_QT_TRANSACTIONDESCAMOUNT_H

#include <interfaces/wallet.h>
#include <pgn/monetary_context.h>

#include <QString>

#include <exception>
#include <optional>
#include <utility>

namespace pgn::qt {
enum class TransactionDescAmountError {
    MissingContext, InvalidContext, MissingTransaction, InvalidShape,
    InvalidMagnitude, ArithmeticOverflow, IncoherentAmounts, Unrepresentable,
};
class TransactionDescAmountFailure final : public std::exception {
public:
    explicit TransactionDescAmountFailure(TransactionDescAmountError error) : m_error(error) {}
    TransactionDescAmountError error() const { return m_error; }
    const char* what() const noexcept override { return "Transaction monetary description unavailable"; }
private:
    TransactionDescAmountError m_error;
};
/** Immutable description-local native metadata, NOT network approval.
 * Checked arithmetic never yields a substitute zero. Failure is caught at the
 * whole-document boundary in the real TransactionDesc consumer, not as an
 * inline valid-looking amount. No wallet/node is created by this helper.
 */
class TransactionDescAmounts final {
public:
    explicit TransactionDescAmounts(std::optional<core::MonetaryContext> context) : m_context(std::move(context)) {}
    void requireContext() const;
    void requireSnapshot(const interfaces::WalletTx&) const;
    static CAmount nonnegative(CAmount);
    static CAmount add(CAmount, CAmount);
    static CAmount subtract(CAmount, CAmount);
    static CAmount negate(CAmount);
    static CAmount sumNonnegative(CAmount, CAmount);
    static CAmount orderedDifference(CAmount, CAmount);
    QString html(CAmount, core::ExactAmountFormat mode = core::ExactAmountFormat::Delta) const;
    QString scaleHtml() const;
    QString debugHtml(const CTransaction&) const;
private:
    QString number(CAmount, core::ExactAmountFormat) const;
    const std::optional<core::MonetaryContext> m_context;
};
QString TransactionDescUnavailableHtml(const QString& label, const QString& reason);
} // namespace pgn::qt
#endif

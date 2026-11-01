// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_SENDAMOUNTDISPLAY_H
#define PGN_QT_SENDAMOUNTDISPLAY_H
#include <pgn/monetary_context.h>
#include <QString>
#include <vector>
#include <utility>

namespace pgn::qt {
enum class SendAmountKind { Transfer, Aggregate, CurrencyPerKVByte };
/** Immutable render-local native metadata. No currency symbol, network policy,
 * coin value, fee admission or wallet availability is inferred here. */
class SendAmountDisplay final {
public:
    explicit SendAmountDisplay(std::optional<core::MonetaryContext> context) : m_context(std::move(context)) {}
    bool available() const;
    std::optional<QString> number(CAmount, SendAmountKind) const;
    std::optional<QString> scale() const;
private:
    const std::optional<core::MonetaryContext> m_context;
};
enum class SendConfirmationStatus { Unavailable=0, Ready, MissingContext, InvalidContext,
    MissingFee, MissingRecipients, InvalidAmount, AmountOverflow, Unrepresentable };
struct SendConfirmationTexts {
    std::vector<QString> recipients;
    QString fee, total, scale;
    CAmount total_atoms;
};
struct SendConfirmationResult {
    SendConfirmationStatus status{SendConfirmationStatus::Unavailable};
    std::optional<SendConfirmationTexts> texts;
    explicit operator bool() const { return status == SendConfirmationStatus::Ready && texts.has_value(); }
};
// Input fee absence is not fee0. Ready is arithmetic/presentation ONLY.
SendConfirmationResult PresentSendConfirmation(const std::vector<CAmount>& recipients,
    std::optional<CAmount> fee, const std::optional<core::MonetaryContext>&);
std::optional<QString> EscapeSendMetadata(const QString&);
QString SendAmountHtml(const QString& canonical, const QString& scale);
} // namespace pgn::qt
#endif

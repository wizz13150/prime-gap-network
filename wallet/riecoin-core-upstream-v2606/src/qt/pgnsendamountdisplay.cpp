// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgnsendamountdisplay.h>
#include <util/overflow.h>
#include <limits>

namespace pgn::qt {
bool SendAmountDisplay::available() const
{
    return m_context && core::ValidateMonetaryContext(*m_context) == core::ExactAmountError::None &&
        m_context->text_budget.max_characters <= static_cast<uint64_t>(std::numeric_limits<int>::max());
}
std::optional<QString> SendAmountDisplay::number(CAmount value, SendAmountKind kind) const
{
    if (!available() || value < 0) return std::nullopt;
    std::optional<core::ExactAmountPolicy> policy;
    switch (kind) {
    case SendAmountKind::Transfer: policy = m_context->transfer; break;
    case SendAmountKind::Aggregate: policy = m_context->display; break;
    case SendAmountKind::CurrencyPerKVByte:
        // Informational rate: denomination precision, NOT transfer/display
        // admission ceilings. CAmountMAX is representational, not economic.
        policy = core::ExactAmountPolicy{m_context->display.decimals, std::numeric_limits<CAmount>::max()};
        break;
    }
    if (!policy) return std::nullopt;
    const auto result = core::formatExactAmount(value, core::ExactAmountFormat::Transfer, policy, m_context->text_budget);
    if (result.error != core::ExactAmountError::None || !result.text) return std::nullopt;
    return QString::fromLatin1(result.text->data(), static_cast<qsizetype>(result.text->size()));
}
std::optional<QString> SendAmountDisplay::scale() const
{
    if (!available()) return std::nullopt;
    return QStringLiteral("10^%1 atoms/unit").arg(m_context->display.decimals);
}
SendConfirmationResult PresentSendConfirmation(const std::vector<CAmount>& recipients,
    std::optional<CAmount> fee, const std::optional<core::MonetaryContext>& context)
{
    using S = SendConfirmationStatus;
    if (!context) return {S::MissingContext, {}};
    const SendAmountDisplay display(context);
    if (!display.available()) return {S::InvalidContext, {}};
    if (!fee) return {S::MissingFee, {}};
    if (recipients.empty()) return {S::MissingRecipients, {}};
    if (*fee < 0) return {S::InvalidAmount, {}};
    CAmount sum{0};
    std::vector<QString> formatted;
    for (const CAmount amount : recipients) {
        if (amount <= 0) return {S::InvalidAmount, {}};
        const auto text = display.number(amount, SendAmountKind::Transfer);
        if (!text) return {S::Unrepresentable, {}};
        const auto next = CheckedAdd(sum, amount);
        if (!next) return {S::AmountOverflow, {}};
        sum = *next;
        formatted.push_back(*text);
    }
    const auto total = CheckedAdd(sum, *fee);
    if (!total) return {S::AmountOverflow, {}};
    const auto fee_text = display.number(*fee, SendAmountKind::Aggregate);
    const auto total_text = display.number(*total, SendAmountKind::Aggregate);
    const auto scale = display.scale();
    if (!fee_text || !total_text || !scale) return {S::Unrepresentable, {}};
    return {S::Ready, SendConfirmationTexts{std::move(formatted), *fee_text, *total_text, *scale, *total}};
}
std::optional<QString> EscapeSendMetadata(const QString& text)
{
    if (!text.isValidUtf16() || text.contains(QChar(u'\0'))) return std::nullopt;
    return text.toHtmlEscaped();
}
QString SendAmountHtml(const QString& canonical, const QString& scale)
{
    return QStringLiteral("<span style='white-space: nowrap;'>%1</span> <small>(%2)</small>")
        .arg(canonical.toHtmlEscaped(), scale.toHtmlEscaped());
}
} // namespace pgn::qt

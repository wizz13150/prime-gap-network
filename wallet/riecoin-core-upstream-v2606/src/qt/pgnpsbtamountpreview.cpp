// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgnpsbtamountpreview.h>
#include <qt/pgntransactionamount.h>
#include <pgn/psbt_amounts.h>
#include <psbt.h>

#include <QCoreApplication>
#include <utility>

namespace pgn::qt {
namespace {
QString Tr(const char* text) { return QCoreApplication::translate("PSBTOperationsDialog", text); }
PsbtMonetaryPreview Failure(PsbtPreviewStatus status) { return {status, std::nullopt}; }
PsbtPreviewStatus InspectionStatus(core::PsbtAmountStatus status)
{
    // No ordinal casts and no domain inferred from presentation metadata.
    switch (status) {
    case core::PsbtAmountStatus::ArithmeticReadyUnverified: return PsbtPreviewStatus::ArithmeticReadyUnverified;
    case core::PsbtAmountStatus::MissingInputAmounts: return PsbtPreviewStatus::MissingInputAmount;
    case core::PsbtAmountStatus::InvalidStructure: case core::PsbtAmountStatus::DuplicateInput: return PsbtPreviewStatus::InvalidStructure;
    case core::PsbtAmountStatus::InvalidInputReference: return PsbtPreviewStatus::InvalidInputReference;
    case core::PsbtAmountStatus::ConflictingInputMetadata: return PsbtPreviewStatus::ConflictingInputMetadata;
    case core::PsbtAmountStatus::NegativeAmount: return PsbtPreviewStatus::NegativeAmount;
    case core::PsbtAmountStatus::AmountOverflow: return PsbtPreviewStatus::SumOverflow;
    case core::PsbtAmountStatus::OutputsExceedInputs: return PsbtPreviewStatus::OutputsExceedInputs;
    // Inspector must never need an actual consensus domain. Unexpected domain
    // results are unavailable, never repaired with a guessed display ceiling.
    case core::PsbtAmountStatus::Unavailable: case core::PsbtAmountStatus::MissingDomain:
    case core::PsbtAmountStatus::DomainExceeded: return PsbtPreviewStatus::Unavailable;
    }
    return PsbtPreviewStatus::Unavailable;
}
std::optional<QString> Number(CAmount value, const std::optional<core::MonetaryContext>& context)
{
    if (value < 0) return std::nullopt;
    const auto result = PresentTransactionAtoms(QVariant::fromValue<qint64>(value), context, false, false);
    return result ? result.canonical : std::nullopt;
}
} // namespace

PsbtMonetaryPreview BuildPsbtMonetaryPreview(const PartiallySignedTransaction& psbt,
    std::optional<core::MonetaryContext> context)
{
    if (!context) return Failure(PsbtPreviewStatus::MissingContext);
    if (core::ValidateMonetaryContext(*context) != core::ExactAmountError::None)
        return Failure(PsbtPreviewStatus::InvalidContext);
    const auto inspected = core::InspectPsbtAmounts(psbt);
    const auto status = InspectionStatus(inspected.status);
    if (status != PsbtPreviewStatus::ArithmeticReadyUnverified) return Failure(status);
    if (!inspected.amounts) return Failure(PsbtPreviewStatus::Unavailable);
    const auto& amounts = *inspected.amounts;
    // Structure, reference consistency and all arithmetic belong to Core's one
    // inspector. Qt only formats this supplied-data result under its snapshot.
    QStringList numbers;
    for (const auto& output : psbt.outputs) {
        const auto number = Number(output.amount, context);
        if (!number) return Failure(PsbtPreviewStatus::Unrepresentable);
        numbers.push_back(*number);
    }
    const auto total_number = Number(amounts.outputs, context);
    const auto fee_number = Number(amounts.fee, context);
    const auto scale = TransactionScaleText(context);
    if (!total_number || !fee_number || !scale) return Failure(PsbtPreviewStatus::Unrepresentable);
    return {PsbtPreviewStatus::ArithmeticReadyUnverified,
        PsbtMonetaryData{amounts.inputs, amounts.outputs, amounts.fee, std::move(numbers), *total_number, *fee_number, *scale}};
}

QString PsbtPreviewUnavailableText(PsbtPreviewStatus status)
{
    if (status == PsbtPreviewStatus::MissingInputAmount)
        return Tr("Unavailable: complete input amounts are required for the monetary preview; unknown fees are not zero.");
    return Tr("Unavailable: the monetary preview requires a coherent native context and representable, consistent PSBT amounts.");
}

QString RenderPsbtMonetaryHtml(const PsbtMonetaryPreview& preview, const QStringList& destinations,
    const std::vector<bool>& own_addresses)
{
    const auto unavailable = [&] { return PsbtPreviewUnavailableText(preview.status).toHtmlEscaped(); };
    if (!preview.readyForDisplay()) return unavailable();
    const auto& data = *preview.data;
    if (destinations.size() != data.outputs.size() || own_addresses.size() != static_cast<size_t>(data.outputs.size()))
        return unavailable();
    for (const auto& label : destinations) {
        if (!label.isValidUtf16() || label.contains(QChar(u'\0'))) return unavailable();
    }
    QString html = Tr("Amounts use %1.").arg(data.scale).toHtmlEscaped() + QStringLiteral("<br>");
    for (qsizetype i = 0; i < data.outputs.size(); ++i) {
        html += QStringLiteral(" * ") + Tr("Sends %1 to %2").arg(data.outputs[i], destinations[i]).toHtmlEscaped();
        if (own_addresses[static_cast<size_t>(i)]) html += QStringLiteral(" (") + Tr("own address").toHtmlEscaped() + QStringLiteral(")");
        html += QStringLiteral("<br>");
    }
    html += QStringLiteral(" * ") + Tr("Pays transaction fee: %1").arg(data.fee_number).toHtmlEscaped();
    html += QStringLiteral("<hr /><b>") + Tr("Total output amount").toHtmlEscaped() + QStringLiteral("</b>: <b>") + data.total_number.toHtmlEscaped() + QStringLiteral("</b>");
    html += QStringLiteral("<br>") + Tr("Arithmetic preview of supplied PSBT data only; UTXOs, fees, ownership and spendability are not authenticated by this preview.").toHtmlEscaped();
    return html;
}
} // namespace pgn::qt

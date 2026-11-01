#pragma once
#include <QJsonObject>
#include <QString>

namespace pgn::ui {
struct HistoricalProofPresentation {
    QString rail, footer, tooltip;
    bool available{};
};

// Presentation of evidence admitted by the qualified journal reader only.
// Neither a historical exact gap nor a retained receipt grants network credit
// or says anything about the currently running source's exhaustive coverage.
inline HistoricalProofPresentation historicalProofPresentation(
    const QJsonObject& receipt, const QString& error) {
    HistoricalProofPresentation result;
    if (receipt.isEmpty()) {
        if (!error.isEmpty()) {
            result.available = true;
            result.rail = QStringLiteral("ARCHIVE READ ERROR · NO PROOF PROMOTED");
            result.footer = QStringLiteral("ARCHIVE READ ERROR · LIVE PROOF PENDING");
            result.tooltip = error;
        }
        return result;
    }
    result.available = true;
    if (!error.isEmpty()) {
        result.rail = QStringLiteral("LAST ARCHIVE RECEIPT RETAINED · READ ERROR · NO NETWORK CREDIT");
        result.footer = QStringLiteral("ARCHIVE RETAINED / READ ERROR · LIVE PROOF PENDING");
    } else if (receipt.value("verdict").toInt(-1) == 0) {
        result.rail = QStringLiteral("ARCHIVE EXACT GAP · Δ %1 · SOURCE %2 · NO NETWORK CREDIT")
            .arg(receipt.value("gap_size").toString(), receipt.value("source_id").toString());
        result.footer = QStringLiteral("ARCHIVE EXACT · LIVE PROOF PENDING · NO NETWORK CREDIT");
    } else {
        result.rail = QStringLiteral("ARCHIVE CHECK NOT VERIFIED · VERDICT %1 · NO NETWORK CREDIT")
            .arg(receipt.value("verdict").toInt(-1));
        result.footer = QStringLiteral("ARCHIVE NOT VERIFIED · LIVE PROOF PENDING");
    }
    result.tooltip = QStringLiteral("Historical receipt %1, source %2, original check %3. "
        "This is not a current-run proof, exhaustive range coverage, network acceptance or reward.")
        .arg(receipt.value("receipt_id").toString(), receipt.value("source_id").toString(),
             receipt.value("original_checked_utc").toString());
    if (!error.isEmpty()) result.tooltip += QStringLiteral(" Read error: ") + error;
    return result;
}
}

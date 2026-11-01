// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_PSBTAMOUNTPREVIEW_H
#define PGN_QT_PSBTAMOUNTPREVIEW_H

#include <pgn/monetary_context.h>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

class PartiallySignedTransaction;

namespace pgn::qt {
enum class PsbtPreviewStatus {
    Unavailable = 0, ArithmeticReadyUnverified, MissingContext, InvalidContext,
    InvalidStructure, MissingInputAmount, InvalidInputReference,
    ConflictingInputMetadata, NegativeAmount, SumOverflow,
    OutputsExceedInputs, Unrepresentable
};
struct PsbtMonetaryData {
    CAmount input_total;
    CAmount output_total;
    CAmount fee;
    QStringList outputs;
    QString total_number;
    QString fee_number;
    QString scale;
};
struct PsbtMonetaryPreview {
    PsbtPreviewStatus status{PsbtPreviewStatus::Unavailable};
    // Published only after the complete tuple has passed arithmetic/formatting.
    std::optional<PsbtMonetaryData> data;
    bool readyForDisplay() const {
        return status == PsbtPreviewStatus::ArithmeticReadyUnverified && data.has_value();
    }
};
/** Pure public-data preview. The context is copied once, never selected here.
 * Core InspectPsbtAmounts validates supplied non-witness hash/index, NOT chain membership,
 * ownership, signatures or the truth of a witness UTXO. Ready is arithmetic on
 * those supplied values only. This is NOT consensus or payment admission.
 * No partial monetary tuple is returned, including for missing input amounts.
 */
PsbtMonetaryPreview BuildPsbtMonetaryPreview(const PartiallySignedTransaction&,
    std::optional<core::MonetaryContext>);

// Destination labels are presentation metadata, not ownership evidence. All
// dynamic and translated text is escaped; invalid text/counts yields one notice.
QString RenderPsbtMonetaryHtml(const PsbtMonetaryPreview&, const QStringList& destinations,
    const std::vector<bool>& own_addresses);
QString PsbtPreviewUnavailableText(PsbtPreviewStatus);
} // namespace pgn::qt
#endif

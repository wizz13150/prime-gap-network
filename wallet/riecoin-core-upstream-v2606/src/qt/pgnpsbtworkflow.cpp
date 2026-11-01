// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgnpsbtworkflow.h>
#include <node/psbt.h>

namespace pgn::qt {
PsbtWorkflowAvailability ClassifyPsbtWorkflowAvailability(
    const PsbtMonetaryPreview& preview, const node::PSBTAnalysis& analysis)
{
    // Configuration is local failure, not evidence against supplied PSBT data.
    if (analysis.local_unavailable) return PsbtWorkflowAvailability::LocalConfigurationUnavailable;
    if (!analysis.error.empty()) return PsbtWorkflowAvailability::InvalidSuppliedData;
    switch (analysis.next) {
    case PSBTRole::UPDATER: case PSBTRole::SIGNER:
    case PSBTRole::FINALIZER: case PSBTRole::EXTRACTOR: break;
    default: return PsbtWorkflowAvailability::Unavailable;
    }
    if (preview.readyForDisplay()) {
        if (!analysis.fee) return PsbtWorkflowAvailability::Unavailable;
        if (*analysis.fee != preview.data->fee) return PsbtWorkflowAvailability::InvalidSuppliedData;
        return PsbtWorkflowAvailability::CompletePreviewOnly;
    }
    switch (preview.status) {
    case PsbtPreviewStatus::MissingInputAmount:
        if (preview.data || analysis.fee) return PsbtWorkflowAvailability::Unavailable;
        return PsbtWorkflowAvailability::PreserveExistingPartialSigning;
    case PsbtPreviewStatus::MissingContext: case PsbtPreviewStatus::InvalidContext:
    case PsbtPreviewStatus::Unrepresentable:
        return PsbtWorkflowAvailability::PresentationUnavailable;
    case PsbtPreviewStatus::InvalidStructure: case PsbtPreviewStatus::InvalidInputReference:
    case PsbtPreviewStatus::ConflictingInputMetadata: case PsbtPreviewStatus::NegativeAmount:
    case PsbtPreviewStatus::SumOverflow: case PsbtPreviewStatus::OutputsExceedInputs:
        return PsbtWorkflowAvailability::InvalidSuppliedData;
    case PsbtPreviewStatus::Unavailable: case PsbtPreviewStatus::ArithmeticReadyUnverified:
        return PsbtWorkflowAvailability::Unavailable;
    }
    return PsbtWorkflowAvailability::Unavailable;
}
bool MustDisablePsbtSigning(PsbtWorkflowAvailability state)
{
    return state != PsbtWorkflowAvailability::PreserveExistingPartialSigning &&
           state != PsbtWorkflowAvailability::CompletePreviewOnly;
}
bool MustDisablePsbtBroadcast(PsbtWorkflowAvailability state)
{
    return state != PsbtWorkflowAvailability::CompletePreviewOnly;
}
} // namespace pgn::qt

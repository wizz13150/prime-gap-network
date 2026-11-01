// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_PSBTWORKFLOW_H
#define PGN_QT_PSBTWORKFLOW_H
#include <qt/pgnpsbtamountpreview.h>
namespace node { struct PSBTAnalysis; }
namespace pgn::qt {
enum class PsbtWorkflowAvailability {
    Unavailable = 0, LocalConfigurationUnavailable, PresentationUnavailable,
    InvalidSuppliedData, PreserveExistingPartialSigning, CompletePreviewOnly
};
/** A UI availability classification, NOT an authorization or consensus token.
 * Never enables an action. Partial preserves existing Core signing state only;
 * it must disable broadcast. Complete still needs the existing Core workflow's
 * completion/signature checks. Call before querying wallet signability.
 */
PsbtWorkflowAvailability ClassifyPsbtWorkflowAvailability(
    const PsbtMonetaryPreview&, const node::PSBTAnalysis&);
bool MustDisablePsbtSigning(PsbtWorkflowAvailability);
bool MustDisablePsbtBroadcast(PsbtWorkflowAvailability);
} // namespace pgn::qt
#endif

// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_COINCONTROLLABELS_H
#define PGN_QT_COINCONTROLLABELS_H
#include <qt/pgncoincontrolpreview.h>
class QDialog;
class QLabel;
namespace pgn::qt {
void InvalidateCoinControlLabels(QDialog&);
bool PublishCoinControlLabels(QDialog&,const CoinControlPreviewResult&,
    const std::optional<core::MonetaryContext>&);
// Amount clipboard only: exact typed atoms+copied context, never label parsing.
// Refusal invokes no sink, preserving existing clipboard contents.
bool CopyCoinControlLabelAmount(const QLabel&,const std::optional<core::MonetaryContext>&,
    const std::function<void(const QString&)>&);
} // namespace pgn::qt
#endif

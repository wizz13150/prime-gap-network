// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_COINCONTROLPREVIEW_H
#define PGN_QT_COINCONTROLPREVIEW_H
#include <pgn/monetary_context.h>
#include <QString>
#include <QVariant>
#include <functional>
#include <vector>

namespace pgn::qt {
struct CoinControlPreviewInput {
    CAmount amount;
    uint32_t estimated_input_vbytes;
    bool witness;
};
enum class CoinControlPreviewStatus { Unavailable=0, Preliminary, InvalidContext,
    MissingInputs, InvalidAmount, Overflow, SizeUnavailable, FeeUnavailable, Unrepresentable };
struct CoinControlPreviewTexts {
    CAmount amount_atoms, fee_atoms, after_fee_atoms;
    std::optional<CAmount> change_atoms;
    QString amount, fee, after_fee;
    std::optional<QString> change; // No payment amount => not applicable, NOT0.
    QString scale;
    uint32_t estimated_vbytes;
    size_t quantity;
    bool insufficient;
};
struct CoinControlPreviewResult {
    CoinControlPreviewStatus status{CoinControlPreviewStatus::Unavailable};
    std::optional<CoinControlPreviewTexts> texts;
    explicit operator bool() const { return status==CoinControlPreviewStatus::Preliminary && texts.has_value(); }
};
/** Preliminary arithmetic/presentation ONLY, before native change/dust/final
 * transaction decisions. Trusted caller supplies its actual Core quote, tests
 * supply explicit public fixtures. No fee policy, Params, wallet or rate inferred.
 * Missing/throwing quote never means0 and invalid inputs invoke no provider. */
CoinControlPreviewResult PresentCoinControlPreview(const std::vector<CoinControlPreviewInput>&,
    const std::vector<CAmount>& payments, bool subtract_fee,
    const std::optional<core::MonetaryContext>&,
    const std::function<std::optional<CAmount>(uint32_t)>& quote);
std::optional<CAmount> CoinControlRawAtoms(const QVariant&);
std::optional<QString> CoinControlAmountText(const QVariant&,
    const std::optional<core::MonetaryContext>&, bool individual_output);
// Exact ordering: absence/type failure is a separate bucket, never numerical0.
bool CoinControlAtomsLess(const QVariant&,const QVariant&);
} // namespace pgn::qt
#endif

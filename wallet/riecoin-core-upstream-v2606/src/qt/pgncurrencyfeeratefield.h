// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_CURRENCYFEERATEFIELD_H
#define PGN_QT_CURRENCYFEERATEFIELD_H
#include <qt/pgnexactamountfield.h>
#include <qt/pgncurrencyfeerate.h>

/** Real currency/kvB field, deliberately NOT RiecoinAmountField/transfer.
 * Owner/thread identity is Qt lifetime binding, never network approval. No
 * policy or value defaults; context/policy replacement clears BEFORE publish.
 * Manual text and numeric results stay untouched by refused consumption. */
class PgnCurrencyFeeRateField final : public pgn::qt::PgnExactAmountField {
    Q_OBJECT
public:
    explicit PgnCurrencyFeeRateField(QWidget* parent=nullptr);
    bool bindRateContext(QObject&, std::optional<pgn::core::MonetaryContext>,
        std::optional<pgn::core::AtomicFeeRatePolicy>);
    bool clearRateContext();
    bool configured() const;
    pgn::qt::CurrencyFeeRateResult feeRateResult(QObject& expected_owner,
        const std::optional<pgn::core::MonetaryContext>&,
        const std::optional<pgn::core::AtomicFeeRatePolicy>&) const;
    QString unitText() const;
Q_SIGNALS:
    void feeRateStateChanged();
private:
    QPointer<QObject> m_core_owner;
    std::optional<pgn::core::MonetaryContext> m_context;
    std::optional<pgn::core::AtomicFeeRatePolicy> m_rate_policy;
    quint64 m_rate_revision{0};
};
#endif

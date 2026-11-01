// Copyright (c) The Riecoin developers
// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef RIECOIN_QT_RIECOINAMOUNTFIELD_H
#define RIECOIN_QT_RIECOINAMOUNTFIELD_H
#include <qt/pgnexactamountfield.h>
#include <pgn/monetary_context.h>

/** Historical UI class name, exact native TRANSFER field. No policy, scale,
 * currency, cap or balance default. value() is optional, never failed->zero.
 * Fee rate requires a separate physical-unit contract and is not configured.
 */
class RiecoinAmountField : public pgn::qt::PgnExactAmountField {
    Q_OBJECT
public:
    enum class Purpose { Unconfigured, Transfer, UnsupportedFeeRate };
    explicit RiecoinAmountField(QWidget* parent = nullptr);
    bool setTransferContext(QObject&, std::optional<pgn::core::MonetaryContext>);
    void clearTransferContext();
    void setUnsupportedFeeRate();
    std::optional<pgn::core::MonetaryContext> transferContext() const;
    QString scaleText() const;
    void SetOptional(bool optional) { m_optional = optional; validate(); }
    void SetMinValue(CAmount value) { m_minimum = value; validate(); }
    bool validate();
    std::optional<CAmount> value() const;
    bool setValue(CAmount);
    // Programmatic action: failure preserves the existing text exactly.
    bool trySetValuePreservingInput(CAmount, const pgn::core::MonetaryContext& expected);
    Purpose purpose() const { return m_purpose; }
Q_SIGNALS:
    void valueChanged();
private:
    QPointer<QObject> m_core_owner;
    std::optional<pgn::core::MonetaryContext> m_context;
    std::optional<CAmount> m_minimum;
    bool m_optional{false};
    Purpose m_purpose{Purpose::Unconfigured};
};
#endif

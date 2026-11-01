// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.

#ifndef PGN_QT_EXACTAMOUNTFIELD_H
#define PGN_QT_EXACTAMOUNTFIELD_H

#include <qt/pgnexactamount.h>
#include <pgn/exact_amount.h>

#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>

#include <memory>

namespace pgn::qt {

enum class ExactAmountFieldState { Unavailable, InvalidText, Valid, WrongThread };

struct ExactAmountFieldResult {
    ExactAmountFieldState state;
    ExactAmountError error; //!< Codec error; None for WrongThread (codec not accessed).
    std::optional<CAmount> amount;
    explicit operator bool() const { return state == ExactAmountFieldState::Valid && amount.has_value(); }
};

/** Real native widget, initially unavailable, with NO monetary policy default.
 * A future trusted Core Qt owner must explicitly supply approved policy. The
 * QObject identity here binds Qt thread/lifetime, NOT consensus approval or
 * wallet availability. Fixtures may exercise the port but prove neither.
 * Every policy set/removal invalidates text/accepted value, even equal policy.
 * Consumers use amountStateChanged + amountResult, never failed->zero or a
 * stale accepted result across policyInvalidated. No monetary action is wired.
 */
class PgnExactAmountField : public QLineEdit {
    Q_OBJECT

public:
    explicit PgnExactAmountField(ExactAmountTextBudget budget, QWidget* parent = nullptr);
    bool setPolicyFromCoreOwner(QObject& owner, std::optional<ExactAmountPolicy> policy);
    bool setNumericPolicyFromCoreOwner(QObject& owner, std::optional<core::ExactAmountPolicy>, core::ExactAmountTextBudget);
    bool clearAmountPolicy();
    bool setAtomicAmount(CAmount amount); //!< Transfer only; failure clears old input, not zero.
    ExactAmountFieldResult amountResult() const;
    std::optional<QString> unitSymbol() const; //!< Plain text only, or absent (also on wrong thread).

Q_SIGNALS:
    void policyInvalidated(quint64 revision);
    void amountStateChanged();

private:
    class InputValidator;
    bool onOwnerThread() const;
    bool applyPolicy(QObject* owner, std::optional<ExactAmountPolicy> policy, ExactAmountTextBudget, ExactAmountUnitMode);
    ExactAmountTextBudget m_budget;
    ExactAmountUnitMode m_unit_mode{ExactAmountUnitMode::RequiredSymbol};
    std::optional<ExactAmountPolicy> m_policy;
    std::unique_ptr<ExactAmountCodec> m_codec;
    QPointer<QObject> m_owner;
    QMetaObject::Connection m_owner_destroyed;
    quint64 m_revision{0};
};

} // namespace pgn::qt

#endif // PGN_QT_EXACTAMOUNTFIELD_H

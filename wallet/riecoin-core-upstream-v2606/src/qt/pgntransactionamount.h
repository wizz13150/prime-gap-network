// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_TRANSACTIONAMOUNT_H
#define PGN_QT_TRANSACTIONAMOUNT_H

#include <pgn/monetary_context.h>

#include <QString>
#include <QVariant>
#include <QObject>

#include <optional>
#include <functional>

namespace pgn::qt {
bool SameTransactionMonetaryContext(const std::optional<core::MonetaryContext>&,
                                   const std::optional<core::MonetaryContext>&);
enum class TransactionContextUpdate { Unchanged, Changed, WrongThread, Reentrant };
/** Qt-owned metadata snapshot, NOT an approval token. Only the real model calls
 * replace with copied selected Core metadata. Tests explicitly inject fixtures.
 * Same-thread aboutToChange is emitted before publishing any changed snapshot,
 * including missing/invalid context, so consumers invalidate accepted state.
 */
class TransactionMonetaryContext final : public QObject {
    Q_OBJECT
public:
    explicit TransactionMonetaryContext(QObject* parent = nullptr) : QObject(parent) {}
    std::optional<core::MonetaryContext> snapshot() const;
    TransactionContextUpdate replace(std::optional<core::MonetaryContext>);
Q_SIGNALS:
    void aboutToChange();
    void changed();
private:
    std::optional<core::MonetaryContext> m_snapshot;
    bool m_replacing{false};
};
// Ready is deliberately NOT zero: absent/unknown status must not become success.
enum class TransactionAmountStatus { Unavailable = 0, Ready, MissingContext, InvalidContext, MissingAtoms, InvalidAtoms, NetOverflow, Unrepresentable, InvalidText };
struct TransactionAmountPresentation {
    TransactionAmountStatus status{TransactionAmountStatus::Unavailable};
    std::optional<CAmount> atoms;
    std::optional<QString> canonical; //!< Native signed ASCII; no grouping/brackets/unit.
    std::optional<QString> display; //!< Presentation only; NEVER parsed or copied as money.
    std::optional<core::ExactAmountError> codec_error;
    explicit operator bool() const { return status == TransactionAmountStatus::Ready && canonical && display && atoms; }
};

QVariant TransactionNetAtoms(CAmount credit, CAmount debit);
TransactionAmountPresentation PresentTransactionAtoms(const QVariant&,
    const std::optional<core::MonetaryContext>&, bool grouped, bool bracketed);
TransactionAmountPresentation PresentTransactionNet(CAmount credit, CAmount debit,
    const std::optional<core::MonetaryContext>&, bool grouped, bool bracketed);
std::optional<QString> TransactionScaleText(const std::optional<core::MonetaryContext>&);
// Invalid/absent atoms remain visible under this predicate, so no malformed row
// silently disappears before a strict export can reject it. Other filters apply.
bool TransactionAmountMeetsMinimum(const QVariant&, const std::optional<CAmount>& minimum);
enum class TransactionCopyContent { Amount, Details };
// The real caller supplies GUIUtil::setClipboard; fixtures supply a local sink.
// Missing/invalid data invokes NO sink and cannot erase prior clipboard text.
TransactionAmountStatus CopyTransactionMonetaryText(const QVariant& atoms,
    const std::optional<core::MonetaryContext>&, TransactionCopyContent,
    const std::optional<QString>& details, const std::function<void(const QString&)>& sink);

enum class MinimumAmountStatus { Unavailable, Disabled, Applied, Invalid };
struct MinimumAmountResult {
    MinimumAmountStatus status;
    std::optional<CAmount> minimum;
    std::optional<core::ExactAmountError> error;
    explicit operator bool() const { return status == MinimumAmountStatus::Disabled || status == MinimumAmountStatus::Applied; }
};
/** Explicit copied context; invalid input NEVER mutates the accepted predicate.
 * Empty valid input removes the predicate (nullopt), distinct from explicit0.
 * Replacing policy means creating a new instance and clearing the UI text; never
 * reinterpret old text. No symbol/Qt ticker policy is invented for this field.
 */
class TransactionMinimumAmount final {
public:
    explicit TransactionMinimumAmount(std::optional<core::MonetaryContext>);
    MinimumAmountResult apply(const QString&);
    std::optional<CAmount> minimum() const { return m_minimum; }
    const std::optional<core::MonetaryContext>& context() const { return m_context; }
private:
    const std::optional<core::MonetaryContext> m_context;
    std::optional<CAmount> m_minimum;
};
} // namespace pgn::qt
#endif

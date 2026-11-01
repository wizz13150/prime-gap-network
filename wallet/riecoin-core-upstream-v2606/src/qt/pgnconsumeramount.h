// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_CONSUMERAMOUNT_H
#define PGN_QT_CONSUMERAMOUNT_H
#include <qt/pgntransactionamount.h>
#include <QPointer>
#include <QMetaType>
class SendCoinsRecipient;

namespace pgn::qt {
/** Transport of the real publisher/owner/source identities and copied metadata.
 * Not a monetary/profile approval token. A queued consumer must check identities,
 * same-thread lifetime and current metadata again before using the payload. */
struct ConsumerMonetarySnapshot {
    QPointer<QObject> publisher, owner, source;
    std::optional<core::MonetaryContext> context;
};
bool ConsumerOwnersLive(const ConsumerMonetarySnapshot&);
bool ConsumerSnapshotMatches(const ConsumerMonetarySnapshot&,
    const ConsumerMonetarySnapshot& current);

enum class ConsumerAmountStatus { Unavailable=0, Ready, MissingContext,
    InvalidContext, MissingAmount, InvalidAmount, InvalidPair, Unrepresentable };
struct BumpFeeAmountTexts {
    QString old_fee, increase, new_fee, scale;
    CAmount increase_atoms;
};
struct BumpFeeAmountResult {
    ConsumerAmountStatus status{ConsumerAmountStatus::Unavailable};
    std::optional<BumpFeeAmountTexts> texts;
    explicit operator bool() const { return status==ConsumerAmountStatus::Ready && texts.has_value(); }
};
// Fee amounts are aggregates, NOT transfer amounts or a rate policy. Presence,
// nonnegativity and ordering are checked BEFORE subtraction. Native bump logic
// remains the sole authority for fee adequacy and transaction validity.
BumpFeeAmountResult PresentBumpFeeAmounts(std::optional<CAmount> old_fee,
    std::optional<CAmount> new_fee, const std::optional<core::MonetaryContext>&);

/** Historical Recipient/RequestEntry v1 has no amount presence, unit or context.
 * It cannot construct this observation just by consulting current parameters.
 * A future qualified provider may supply an actual observation, including0.
 * This DTO is presentation data, not wallet/address/provenance authentication. */
struct RequestAmountObservation {
    std::optional<CAmount> atoms;
    std::optional<core::MonetaryContext> context;
};
struct RequestAmountText {
    ConsumerAmountStatus status{ConsumerAmountStatus::Unavailable};
    std::optional<QString> canonical, scale;
    explicit operator bool() const { return status==ConsumerAmountStatus::Ready && canonical && scale; }
};
RequestAmountText PresentRequestAmount(const std::optional<RequestAmountObservation>&);
// Opaque legacy metadata is never upgraded by inspecting its amount/default0.
std::optional<RequestAmountObservation> ObserveLegacyRequestAmount(const SendCoinsRecipient&);
TransactionAmountPresentation PresentConsumerTransactionAtoms(const QVariant&,
    const std::optional<core::MonetaryContext>&,bool grouped,bool bracketed);

struct TransactionAmountNotification {
    ConsumerMonetarySnapshot monetary;
    QVariant atoms; // Exact signed LongLong role, never converted through strings/double.
    QString date, type, address, label, wallet_name;
};
TransactionAmountPresentation PresentNotificationAmount(const TransactionAmountNotification&,
    const ConsumerMonetarySnapshot& current);
} // namespace pgn::qt
Q_DECLARE_METATYPE(pgn::qt::TransactionAmountNotification)
#endif

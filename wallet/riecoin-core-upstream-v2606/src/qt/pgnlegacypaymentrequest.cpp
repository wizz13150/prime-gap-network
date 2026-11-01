// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/pgnlegacypaymentrequest.h>
#include <QCoreApplication>

namespace pgn::qt {
QString LegacyPaymentRequestMetadataHtml(std::string_view payload)
{
    // Deliberately no ASN.1/protobuf/CN/UTF8 heuristic. Metadata presence is not
    // certificate validity, authentication, ownership, or payment admission.
    // Even the length need not be disclosed. The view makes no payload copy.
    (void)payload;
    const QString label = QCoreApplication::translate("TransactionDesc", "Legacy PaymentRequest metadata");
    const QString unverified_notice = QCoreApplication::translate("TransactionDesc", "Unverified legacy metadata. No merchant identity extracted or authenticated.");
    return QStringLiteral("<b>%1:</b> %2<br>")
        .arg(label.toHtmlEscaped(), unverified_notice.toHtmlEscaped());
}
} // namespace pgn::qt

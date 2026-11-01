// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_LEGACYPAYMENTREQUEST_H
#define PGN_QT_LEGACYPAYMENTREQUEST_H

#include <QString>
#include <string_view>

namespace pgn::qt {
/** Opaque historical metadata, NOT PaymentRequest/X509 parsing or admission.
 * No payload byte is read, decoded, executed,
 * normalized, copied into the result, or offered as a merchant identity.
 * The caller retains its original history; this function cannot modify it.
 * The only message is a fixed local UI translation, also HTML escaped.
 */
QString LegacyPaymentRequestMetadataHtml(std::string_view payload);
} // namespace pgn::qt
#endif

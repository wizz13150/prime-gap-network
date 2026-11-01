// Copyright (c) 2026 The PGN developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef PGN_QT_MONEYFORMAT_H
#define PGN_QT_MONEYFORMAT_H
#include <consensus/amount.h>
#include <QString>

// Preserved v2606 thin-space/HTML presentation definitions. The numeric
// implementation below needs QtCore only, not guiutil.h's Widgets/backend.
#define REAL_THIN_SP_CP 0x2009
#define REAL_THIN_SP_UTF8 "\xE2\x80\x89"
#define HTML_HACK_SP "<span style='white-space: nowrap; font-size: 6pt'> </span>"
#define THIN_SP_CP REAL_THIN_SP_CP
#define THIN_SP_UTF8 REAL_THIN_SP_UTF8
#define THIN_SP_HTML HTML_HACK_SP

namespace GUIUtil {
enum class SeparatorStyle { NEVER, STANDARD, ALWAYS };
/** Total signed CAmount presentation, including MIN and aggregates above the
 * transfer cap. Native FormatMoneyFixed is the only numeric conversion.
 * These pure functions receive actual atoms; they never fill absent provider,
 * wallet, profile, policy or monetary observations with a default0. */
QString formatAmount(const CAmount&,bool plussign=false,SeparatorStyle separators=SeparatorStyle::STANDARD,bool justify=false);
QString formatAmountWithUnit(const CAmount&,bool plussign=false,SeparatorStyle separators=SeparatorStyle::STANDARD);
QString formatAmountHtmlWithUnit(const CAmount&,bool plussign=false,SeparatorStyle separators=SeparatorStyle::STANDARD);
//! Preserved nonnegative precondition, fixed-length value-independent mask.
QString formatAmountWithPrivacy(const CAmount&,SeparatorStyle,bool privacy);
/** Transfer syntax only: nonnegative <= approved PGN cap, ASCII decimal or
 * exact THIN_SP integer groups. Outer ASCII whitespace is allowed; signs,
 * inner ASCII spaces/Unicode alternatives are not. Failure never writes out.
 * Native nonambiguous "1."/".1" conveniences are preserved; dot-only fails
 * in the native parser. Output formatting remains canonical fixed10.
 * Dedicated edit validators retain their own intermediate-input contract.
 * Signed display/plus/aggregate format is deliberately not transfer admission.
 * Native ParseMoney is the sole numeric parser; no URI/network approval. */
bool parse(const QString&,CAmount* val_out);
} // namespace GUIUtil
#endif

#pragma once
#include <QString>

// Horizon's primary coin-navigation skin. The zero minimum overrides the
// workspace form-button minimum: a 43px bar has 33px for its native buttons.
// PGN supplies its own labels, page bindings and status-event projection.
inline QString HorizonPrimaryNavigationStyle()
{
    return QStringLiteral(
        "QFrame#horizonCoinNavigation{background:#01070b;border-bottom:1px solid #123740;}"
        "QPushButton{min-height:0px;min-width:0px;background:#03151c;color:#78b9c6;"
        "border:1px solid #123e49;border-radius:9px;padding:2px 14px;text-align:left;"
        "font-family:monospace;font-size:8px;font-weight:700;}"
        "QPushButton:hover{background:#08252d;color:#e5fffb;border-color:#31808a;}"
        "QPushButton:checked{background:#0a3535;color:#69efc5;"
        "border:1px solid #69efc5;border-left:5px solid #ffad4a;}");
}

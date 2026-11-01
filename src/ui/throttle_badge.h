#pragma once
#include <QString>
#include <QStringList>
namespace pgn::ui {
// Observed driver limitation only. A mining enable toggle or an unqualified
// controller must never appear here as evidence of active thermal regulation.
inline QString throttleBadgeLabel(const QString& observedClass){
 const QStringList known{QStringLiteral("NONE"),QStringLiteral("THERMAL"),
  QStringLiteral("POWER/CURRENT"),QStringLiteral("HARDWARE"),
  QStringLiteral("CLOCK/POLICY"),QStringLiteral("UNKNOWN")};
 return QStringLiteral("THROTTLE %1").arg(known.contains(observedClass)?observedClass:QStringLiteral("UNKNOWN"));
}
}

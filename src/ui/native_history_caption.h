#pragma once
#include <QString>
namespace pgn::ui {
inline QString nativeHistoryCaption(qint64 current,qint64 total,qint64 dated,qint64 imported,bool /*timeAxis*/){
  if(current<-1||total<0||dated<0||dated>total||imported<0)return QStringLiteral("NATIVE HISTORY · INDEX COUNTS UNAVAILABLE");
 // One retained-history count, independent of optional source-time metadata.
 // The chart's axis eligibility is explained separately, never another counter.
  return QStringLiteral("CURRENT %1 · NATIVE ALL %2 · IMPORTED %3")
      .arg(current<0?QStringLiteral("LOADING"):QString::number(current)).arg(total).arg(imported);
}
inline QString nativeHistoryExplanation(qint64 total,qint64 dated,bool timeAxis){
 if(total<0||dated<0||dated>total)return QStringLiteral("Native index counts are not available; source records remain preserved.");
 const auto archive=QStringLiteral("Native archive: %1 retained gaps. ").arg(total);
 if(timeAxis && dated<total)
  return archive+QStringLiteral("TIME uses original source times. Records without a source time remain available in LENGTH. No timestamp is invented.");
 return archive+(timeAxis?QStringLiteral("TIME uses original source times. The visible viewport and filters do not delete retained history."):
                          QStringLiteral("All retained gaps are available in LENGTH. The visible viewport and filters do not delete retained history."));
}
}

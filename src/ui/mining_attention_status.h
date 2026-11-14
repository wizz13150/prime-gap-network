#pragma once
#include <QString>
namespace pgn::ui {
inline QString miningAttentionStatus(const QString& reason)
{
 const auto text=reason.simplified();
 if(text.contains("Active parent changed",Qt::CaseInsensitive))
  return "Network changed during audit submission · open Diagnostics";
 if(text.contains("WAL budget",Qt::CaseInsensitive))
  return "Local results storage unavailable · open Diagnostics";
 if(text.contains("assignment changed",Qt::CaseInsensitive))
  return "Audit assignment changed · open Diagnostics";
 if(text.contains("Invalid audit actor signature",Qt::CaseInsensitive))
  return "Audit submission rejected · identity verification failed";
 return text.isEmpty()?QString("Mining needs attention · open Diagnostics for the reason"):
                       QString("Mining needs attention · ")+text.left(160);
}
}

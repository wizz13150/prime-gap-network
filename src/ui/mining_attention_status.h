#pragma once
#include <QString>
namespace pgn::ui {
inline QString miningAttentionStatus(const QString& reason)
{
 const auto text=reason.simplified();
 if(text.contains("Active parent changed",Qt::CaseInsensitive))
  return "Audit submission delayed · retrying on the updated network";
 if(text.contains("WAL budget",Qt::CaseInsensitive))
  return "Results saved · temporary storage access blocked";
 if(text.contains("assignment changed",Qt::CaseInsensitive))
  return "Audit reassigned · computed results retained";
 if(text.contains("Invalid audit actor signature",Qt::CaseInsensitive))
  return "Audit submission rejected · identity verification failed";
 return text.isEmpty()?QString("Mining needs attention · open Diagnostics for the reason"):
                       QString("Mining needs attention · ")+text.left(160);
}
}

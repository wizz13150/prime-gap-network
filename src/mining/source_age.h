#pragma once
#include <QJsonObject>
#include <chrono>
#include <optional>
#include <limits>
namespace pgn::mining {
inline qint64 receiptMonotonicNs(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
// Two machines' UTC clocks cannot determine transport age. Preserve source
// UTC unchanged; use the producer's own clock lag plus local monotonic elapsed.
// Network transit is unmeasured, so this is explicitly a LOWER BOUND, not an
// assertion that a cross-machine timestamp is instantaneous or synchronized.
inline void stampSourceAge(QJsonObject& raw,quint64 producerNs,quint64 counterNs,qint64 receiptNs){
 if(producerNs<counterNs||producerNs-counterNs>=86400ULL*1000000000ULL||receiptNs<0){
  raw.remove("pgn_age_at_receipt_ns");raw.remove("pgn_receipt_monotonic_ns");return;
 }
 raw.insert("pgn_age_at_receipt_ns",QString::number(producerNs-counterNs));
 raw.insert("pgn_receipt_monotonic_ns",QString::number(receiptNs));
}
inline std::optional<qint64> sourceAgeLowerBoundNs(const QJsonObject& raw,qint64 nowNs=receiptMonotonicNs()){
 bool ageOk=false,receiptOk=false;
 const auto age=raw.value("pgn_age_at_receipt_ns").toString().toLongLong(&ageOk);
 const auto receipt=raw.value("pgn_receipt_monotonic_ns").toString().toLongLong(&receiptOk);
 if(!ageOk||!receiptOk||age<0||receipt<0||nowNs<receipt||nowNs-receipt>std::numeric_limits<qint64>::max()-age)return {};
 return age+(nowNs-receipt);
}
}

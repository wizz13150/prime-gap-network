#pragma once
#include "ui/render/metric_caption.h"
namespace pgn::ui {
// Card-specific policy: fixed two decimals, no approximation glyph. Source
// bits, exact integer-ratio rounding and outside-card formatters are unchanged.
inline QString cardMetricText(QString text){return text.remove(QChar(0x2248)).remove('~').simplified();}
inline QString compactCardMetricCaption(const std::optional<double>& value){
 if(!value||!std::isfinite(*value)||*value<0)return QStringLiteral("—");
 if(*value==0)return QStringLiteral("0.00");
 auto ratio=metric_detail::exact(*value);
 if(ratio.numerator>=ratio.denominator*metric_detail::power10(27)){
  const metric_detail::Integer whole=ratio.numerator/ratio.denominator;
  int exponent=int(whole.convert_to<std::string>().size())-1;
  ratio.denominator*=metric_detail::power10(exponent);
  if(ratio.numerator*200>=ratio.denominator*1999){ratio.denominator*=10;++exponent;}
  return cardMetricText(metric_detail::fixed(ratio,2,false))+"e"+QString::number(exponent);
 }
 static const std::array<QString,9> units{QString{},"K","M","G","T","P","E","Z","Y"};
 std::size_t unit=0;
 // Promote only if rounding at two decimals would reach1000.00.
 while(ratio.numerator*200>=ratio.denominator*199999&&unit+1<units.size()){ratio.denominator*=1000;++unit;}
 return cardMetricText(metric_detail::fixed(ratio,2,false))+units[unit];
}
inline QString fixedCardMetricCaption(const std::optional<double>& value){
 if(!value||!std::isfinite(*value)||*value<0)return QStringLiteral("—");
 const auto text=cardMetricText(metric_detail::fixed(metric_detail::exact(*value),2,false));
 return text.size()>8?compactCardMetricCaption(value):text;
}
} // namespace pgn::ui

#pragma once
#include <QString>
#include <array>
#include <cmath>
#include <optional>
#include <bit>
#include <cstdint>
#include <boost/multiprecision/cpp_int.hpp>

namespace pgn::ui {
// A future producer UTC timestamp cannot establish a negative data age.
// Preserve the raw measurement; presentation reports it as unavailable.
inline QString telemetryAgeCaption(const std::optional<qint64>& milliseconds,
                                   bool lowerBound = false)
{
    if (!milliseconds || *milliseconds < 0) return QStringLiteral("—");
    return (lowerBound ? QStringLiteral("≥") : QString{}) +
        QString::number(*milliseconds) + QStringLiteral("ms");
}

namespace metric_detail {
using Integer=boost::multiprecision::cpp_int;
struct Ratio{Integer numerator,denominator;};
inline Ratio exact(double raw){
    const auto bits=std::bit_cast<std::uint64_t>(raw);
    const unsigned e=unsigned((bits>>52)&0x7ffU);
    const std::uint64_t mantissa=(bits&((std::uint64_t{1}<<52)-1))|(e?(std::uint64_t{1}<<52):0);
    Ratio out{mantissa,1};const int power=e?int(e)-1023-52:-1074;
    if(power>=0)out.numerator<<=power;else out.denominator<<=-power;
    return out;
}
inline Integer power10(int e){Integer out=1;for(int i=0;i<e;++i)out*=10;return out;}
inline QString fixed(const Ratio& value,int places,bool trim){
    const Integer scaled=value.numerator*power10(places);
    const Integer remainder=scaled%value.denominator;
    const Integer rounded=scaled/value.denominator+(2*remainder>=value.denominator?1:0);
    QString digits=QString::fromStdString(rounded.convert_to<std::string>());
    if(places){digits=digits.rightJustified(places+1,'0');digits.insert(digits.size()-places,'.');}
    if(trim&&places){while(digits.endsWith('0'))digits.chop(1);if(digits.endsWith('.'))digits.chop(1);}
    return (remainder!=0?QStringLiteral("≈"):QString{})+digits;
}
}

// SI selection, half-up rounding and exactness all use the original binary64
// rational. No floating-point division or decimal-to-double equality test.
// Source measurements and counters are never modified by presentation.
inline QString compactMetricCaption(const std::optional<double>& value)
{
    if(!value||!std::isfinite(*value)||*value<0)return QStringLiteral("—");
    if(*value==0)return QStringLiteral("0");
    auto ratio=metric_detail::exact(*value);
    if(ratio.numerator*100<ratio.denominator)return QStringLiteral("<0.01");
    if(ratio.numerator>=ratio.denominator*metric_detail::power10(27)){
        const metric_detail::Integer integer=ratio.numerator/ratio.denominator;
        int exponent=int(integer.convert_to<std::string>().size())-1;
        const metric_detail::Integer divisor=ratio.denominator*metric_detail::power10(exponent);
        metric_detail::Integer mantissa=ratio.numerator/divisor+(2*(ratio.numerator%divisor)>=divisor?1:0);
        if(mantissa==10){mantissa=1;++exponent;}
        const bool rounded=ratio.numerator!=mantissa*ratio.denominator*metric_detail::power10(exponent);
        return (rounded?QStringLiteral("≈"):QString{})+QString::fromStdString(mantissa.convert_to<std::string>())+"e"+QString::number(exponent);
    }
    static const std::array<QString,9> units{QString{},"K","M","G","T","P","E","Z","Y"};
    std::size_t unit=0;
    while(ratio.numerator*2>=ratio.denominator*1999&&unit+1<units.size()){ratio.denominator*=1000;++unit;}
    const int places=ratio.numerator>=ratio.denominator*100?0:ratio.numerator>=ratio.denominator*10?1:2;
    return metric_detail::fixed(ratio,places,true)+units[unit];
}

inline QString fixedMetricCaption(const std::optional<double>& value,int places)
{
    if(!value||!std::isfinite(*value)||*value<0||places<0||places>6)return QStringLiteral("—");
    const auto text=metric_detail::fixed(metric_detail::exact(*value),places,false);
    return text.size()>8?compactMetricCaption(value):text;
}
} // namespace pgn::ui

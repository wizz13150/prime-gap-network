#pragma once
#include <QString>
#include <QRegularExpression>
#include <algorithm>
#include <stdexcept>
#include <boost/multiprecision/cpp_int.hpp>

namespace pgn::ui {
namespace rational_detail {
using Integer=boost::multiprecision::cpp_int;
inline Integer decimal(const QString& text){
    if(!QRegularExpression("^(0|[1-9][0-9]{0,39})$").match(text).hasMatch())
        throw std::invalid_argument("Bounded canonical rational required");
    return Integer(text.toStdString());
}
inline Integer power(int exponent){Integer out=1;for(int i=0;i<exponent;++i)out*=10;return out;}
inline QString fixed(const Integer& numerator,const Integer& denominator,int places,bool trim){
    if(numerator<0||denominator<=0||places<0||places>12)throw std::invalid_argument("Rational display bounds");
    const Integer scaled=numerator*power(places),remainder=scaled%denominator;
    const Integer rounded=scaled/denominator+(remainder*2>=denominator?1:0);
    QString digits=QString::fromStdString(rounded.convert_to<std::string>());
    if(places){digits=digits.rightJustified(places+1,'0');digits.insert(digits.size()-places,'.');}
    if(trim&&places){while(digits.endsWith('0'))digits.chop(1);if(digits.endsWith('.'))digits.chop(1);}
    return (remainder!=0?QString("≈ "):QString{})+digits;
}
}
// The approximation marker describes rounding, never changes the source ratio.
inline QString meritTargetText(qint64 q16){
    if(q16<=0||q16>64*65536)return "—";
    return rational_detail::fixed(q16,65536,q16*10000<65536?8:4,false);
}
inline QString reportedRateText(const QString& positions,qint64 seconds){
    try{
        using namespace rational_detail;
        if(seconds<=0||seconds>86400)return "—";
        const Integer numerator=decimal(positions);Integer denominator=seconds;
        if(numerator==0)return "0 pos/s";
        static const char* units[]{"","k","M","G","T","P","E"};int unit=0;
        while(numerator>=denominator*1000&&unit<6){denominator*=1000;++unit;}
        if(unit==6&&numerator>=denominator*1000){
            const int exponent=int((numerator/seconds).convert_to<std::string>().size())-1;
            return fixed(numerator,Integer(seconds)*power(exponent),2,true)+" × 10^"+QString::number(exponent)+" pos/s";
        }
        // Three significant decimal digits; include leading fractional zeros.
        Integer whole=numerator/denominator;
        int places=whole==0?3:std::max(0,3-int(whole.convert_to<std::string>().size()));
        if(whole==0){Integer shifted=numerator;while(shifted*10<denominator&&places<12){shifted*=10;++places;}}
        return fixed(numerator,denominator,places,true)+" "+units[unit]+"pos/s";
    }catch(const std::exception&){return "—";}
}
}

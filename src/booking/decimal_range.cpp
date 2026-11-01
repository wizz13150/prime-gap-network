#include "booking/decimal_range.h"
#include <QStringList>
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <stdexcept>

namespace pgn::booking {
namespace {
using Integer=boost::multiprecision::cpp_int;
Integer integer(const QString& value){return Integer(canonicalUnsigned(value).toStdString());}
QString decimal(const Integer& value){return QString::fromStdString(value.str());}
}
QString canonicalUnsigned(const QString& text){
    if(text.isEmpty()||text.size()>160)throw std::invalid_argument("Unsigned decimal length");
    for(const auto c:text)if(c<'0'||c>'9')throw std::invalid_argument("Unsigned decimal characters");
    qsizetype first=0;while(first+1<text.size()&&text[first]=='0')++first;
    return text.mid(first);
}
int compareUnsigned(const QString& a,const QString& b){
    const auto left=canonicalUnsigned(a),right=canonicalUnsigned(b);
    if(left.size()!=right.size())return left.size()<right.size()?-1:1;
    return left.compare(right);
}
QString scaleDecimal(const QString& text,int places){
    if(places<0||places>80)throw std::invalid_argument("Decimal scale");
    const auto parts=text.split('.');
    if(parts.size()>2||parts[0].isEmpty()||(parts.size()==2&&(parts[1].isEmpty()||parts[1].size()>places)))
        throw std::invalid_argument("Exact decimal scale required");
    canonicalUnsigned(parts[0]);const auto fraction=parts.size()==2?parts[1]:QString{};
    if(!fraction.isEmpty())canonicalUnsigned(fraction);
    return canonicalUnsigned(parts[0]+fraction+QString(places-fraction.size(),'0'));
}
QString subtractUnsigned(const QString& a,const QString& b){
    const Integer result=integer(a)-integer(b);if(result<0)throw std::invalid_argument("Negative range difference");return decimal(result);
}
QString addUnsigned(const QString& a,const QString& b){return decimal(integer(a)+integer(b));}
QString divideUnsignedSmall(const QString& numerator,unsigned divisor){if(!divisor)throw std::invalid_argument("Zero divisor");return decimal(integer(numerator)/divisor);}
double rangeFraction(const QString& position,const QString& start,const QString& end){
    const auto first=integer(start),last=integer(end),p=integer(position);
    if(last<=first)throw std::invalid_argument("Non-positive range");
    if(p<=first)return 0;if(p>=last)return 1;
    // Subtract exactly before converting: adjacent positions near2e20 do not
    // collapse into equal doubles, even though display pixels can overlap.
    return ((p-first).convert_to<long double>()/(last-first).convert_to<long double>());
}
int bitLength(const QString& positive){const auto value=integer(positive);return value==0?0:boost::multiprecision::msb(value)+1;}
bool overlaps(const QString& a,const QString& b,const QString& c,const QString& d){
    return compareUnsigned(a,d)<0&&compareUnsigned(c,b)<0;
}
}

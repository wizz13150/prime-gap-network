#pragma once
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <utility>
#include <algorithm>
namespace pgn::ui {
namespace position_detail {
inline QString normalized(QString value){while(value.size()>1&&value[0]=='0')value.remove(0,1);return value;}
inline int compare(const QString& a,const QString& b){return a.size()!=b.size()?(a.size()<b.size()?-1:1):QString::compare(a,b);}
inline QString increment(QString value){for(int i=value.size()-1;i>=0;--i){if(value[i]!='9'){value[i]=QChar(value[i].unicode()+1);return value;}value[i]='0';}return "1"+value;}
inline QString times(QString value,int multiplier){int carry=0;for(int i=value.size()-1;i>=0;--i){int v=value[i].digitValue()*multiplier+carry;value[i]=QChar('0'+v%10);carry=v/10;}if(carry)value.prepend(QString::number(carry));return normalized(value);}
inline QString subtract(QString a,const QString& b){int borrow=0;for(int i=a.size()-1,j=b.size()-1;i>=0;--i,--j){int v=a[i].digitValue()-(j>=0?b[j].digitValue():0)-borrow;borrow=v<0;if(borrow)v+=10;a[i]=QChar('0'+v);}return normalized(a);}
inline QString trim(QString value){while(value.contains('.')&&value.endsWith('0'))value.chop(1);if(value.endsWith('.'))value.chop(1);return value;}
// Round nonnegative decimal integers half up, before adding any notation.
// No conversion to double: adjacent 95-bit values must stay distinguishable.
inline QString roundedPrefix(const QString& exact,int count){QString digits=exact.left(count);if(exact.size()>count&&exact[count]>='5')digits=increment(digits);return digits;}
inline QString expanded(QString digits,int shift){return normalized(digits+QString(shift,'0'));}
inline const QStringList& binaryPowers(){static const QStringList values=[](){QStringList out{"1"};while(out.back().size()<=40)out.append(times(out.back(),2));return out;}();return values;}
}
struct PositionFormats {QString exact,human,decimal,binary;QString inlineText()const{return human+" · "+decimal+" · "+binary;}QString stackedText()const{return human+"\n"+decimal+"\n"+binary;}QString tooltip()const{return exact+" positions\n"+decimal+"\n"+binary;}};
inline PositionFormats positionFormats(QString exact){
    PositionFormats out;out.exact=exact;out.human=out.decimal=out.binary="—";
    if(exact.isEmpty()||exact.size()>40||!QRegularExpression("^[0-9]+$").match(exact).hasMatch())return out;
    using namespace position_detail;exact=normalized(exact);out.exact=exact;
    if(exact=="0"){out.human="0";out.decimal="0e0";out.binary="0";return out;}
    out.human=exact;for(const auto& unit:{std::pair{18,"E"},std::pair{15,"P"},std::pair{12,"T"},std::pair{9,"G"},std::pair{6,"M"},std::pair{3,"K"}})if(exact.size()>unit.first){
        const int count=exact.size()-unit.first+2;auto rounded=roundedPrefix(exact,count);
        const bool approximate=expanded(rounded,exact.size()-count)!=exact;
        rounded.insert(rounded.size()-2,'.');out.human=(approximate?QString("≈ "):QString{})+trim(rounded)+" "+unit.second;break;}
    int decimalExponent=exact.size()-1;auto digits=roundedPrefix(exact,5);
    bool approximate=exact.size()>5&&expanded(digits,exact.size()-5)!=exact;
    if(digits.size()>5){digits.chop(1);++decimalExponent;}
    digits+=QString(5-digits.size(),'0');digits.insert(1,'.');
    out.decimal=(approximate?QString("≈ "):QString{})+digits+"e"+QString::number(decimalExponent);
    const auto& powers=binaryPowers();const auto next=std::upper_bound(powers.begin(),powers.end(),exact,[](const QString& value,const QString& power){return compare(value,power)<0;});
    int exponent=int(next-powers.begin())-1;const auto& power=powers[exponent];
    if(power==exact)out.binary="2^"+QString::number(exponent);
    else{
        QString remainder=subtract(exact,power),mantissa="1";
        for(int i=0;i<4;++i){remainder=times(remainder,10);int digit=0;while(compare(remainder,power)>=0){remainder=subtract(remainder,power);++digit;}mantissa+=QChar('0'+digit);}
        const bool binaryApproximate=remainder!="0";
        if(compare(times(remainder,2),power)>=0)mantissa=increment(mantissa);
        if(mantissa=="20000"){mantissa="10000";++exponent;}
        mantissa.insert(1,'.');out.binary=(binaryApproximate?QString("≈ "):QString{})+trim(mantissa)+" × 2^"+QString::number(exponent);
    }
    // Decimal/binary notation is presentation only; exact integer never changes.
    return out;
}
struct PositionRangeFormats {PositionFormats start,end;bool valid{};};
inline PositionRangeFormats positionRangeFormats(const QString& start,const QString& end){
    PositionRangeFormats out{positionFormats(start),positionFormats(end),false};
    if(out.start.human=="—"||out.end.human=="—")return out;
    const auto& first=out.start.exact;const auto& last=out.end.exact;
    if(first.size()>last.size()||(first.size()==last.size()&&first>=last))return out;
    int shared=0;if(first.size()==last.size())while(shared<first.size()&&first[shared]==last[shared])++shared;
    const int precision=std::max(7,shared+1);
    const auto notation=[precision](const QString& exact){
        if(exact=="0")return QString("0e0");
        QString digits=exact.left(precision);const bool approximate=exact.sliced(digits.size()).contains(QRegularExpression("[1-9]"));
        while(digits.size()>1&&digits.endsWith('0'))digits.chop(1);
        if(digits.size()>1)digits.insert(1,'.');
        return (approximate?QString("≈ "):QString{})+digits+"e"+QString::number(exact.size()-1);
    };
    out.start.decimal=notation(first);out.end.decimal=notation(last);out.valid=true;return out;
}
}

#pragma once
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <cmath>
#include <utility>
#include <algorithm>
namespace pgn::ui {
struct PositionFormats {QString exact,human,decimal,binary;QString inlineText()const{return human+" · "+decimal+" · "+binary;}QString stackedText()const{return human+"\n"+decimal+"\n"+binary;}QString tooltip()const{return exact+" positions\n"+decimal+"\n"+binary;}};
inline PositionFormats positionFormats(QString exact){
    PositionFormats out;out.exact=exact;out.human=out.decimal=out.binary="—";
    if(exact.isEmpty()||exact.size()>40||!QRegularExpression("^[0-9]+$").match(exact).hasMatch())return out;
    while(exact.size()>1&&exact[0]=='0')exact.remove(0,1);out.exact=exact;
    if(exact=="0"){out.human="0";out.decimal="0e0";out.binary="0";return out;}
    const double value=exact.toDouble();const auto trim=[](QString x){while(x.contains('.')&&x.endsWith('0'))x.chop(1);if(x.endsWith('.'))x.chop(1);return x;};
    out.human=exact;for(const auto& unit:{std::pair{18,"E"},std::pair{15,"P"},std::pair{12,"T"},std::pair{9,"G"},std::pair{6,"M"},std::pair{3,"K"}})if(exact.size()>unit.first){
        const auto digits=trim(QString::number(value/std::pow(10.,unit.first),'f',2));
        auto restored=digits;const int point=restored.indexOf('.');const int fractional=point<0?0:restored.size()-point-1;restored.remove('.');restored+=QString(unit.first-fractional,'0');
        while(restored.size()>1&&restored[0]=='0')restored.remove(0,1);
        out.human=(restored==exact?QString{}:QString("≈ "))+digits+" "+unit.second;break;}
    out.decimal=QString::number(value,'e',4).replace("e+","e");out.decimal.replace(QRegularExpression("e(-?)0+([0-9]+)$"),"e\\1\\2");
    const int mark=out.decimal.indexOf('e');QString rebuilt=out.decimal.left(mark);rebuilt.remove('.');int shift=out.decimal.mid(mark+1).toInt()-4;while(shift<0&&rebuilt.endsWith('0')){rebuilt.chop(1);++shift;}if(shift>=0)rebuilt+=QString(shift,'0');while(rebuilt.size()>1&&rebuilt[0]=='0')rebuilt.remove(0,1);if(shift<0||rebuilt!=exact)out.decimal.prepend("≈ ");
    int exponent=0;double mantissa=std::frexp(value,&exponent)*2;--exponent;if(QString::number(mantissa,'f',4)=="2.0000"){mantissa=1;++exponent;}
    out.binary="≈ "+trim(QString::number(mantissa,'f',4))+" × 2^"+QString::number(exponent);
    QString power="1";for(int i=0;i<exponent;++i){int carry=0;for(int j=power.size()-1;j>=0;--j){const int d=power[j].digitValue()*2+carry;power[j]=QChar('0'+d%10);carry=d/10;}if(carry)power.prepend('1');}
    if(power==exact)out.binary="2^"+QString::number(exponent);
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

#include "booking/range_explorer.h"
#include "booking/decimal_range.h"
#include <QRegularExpression>
#include <QSet>
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <stdexcept>

namespace pgn::booking {
QString rangeInputInteger(QString text){
    text=text.trimmed();
    if(text.size()>240)throw std::invalid_argument("Number is too long.");
    text.remove(' ');text.remove(QChar(0x00a0));text.remove(QChar(0x202f));text.remove('_');
    if(text.startsWith('+'))text.remove(0,1);
    if(text.startsWith('.'))text.prepend('0');
    static const QRegularExpression number("^([0-9]+)(?:\\.([0-9]+))?(?:[eE]([+-]?[0-9]{1,3}))?$");
    const auto match=number.match(text);
    if(!match.hasMatch())throw std::invalid_argument("Use a whole number or exact scientific notation, for example1.5e20.");
    QString digits=canonicalUnsigned(match.captured(1)+match.captured(2));
    const int exponent=match.captured(3).isEmpty()?0:match.captured(3).toInt();
    const int shift=exponent-match.capturedLength(2);
    if(digits=="0")return digits;
    if(shift>=0){
        if(digits.size()+shift>160)throw std::invalid_argument("Number is too long.");
        digits+=QString(shift,'0');
    }else{
        const int remove=-shift;
        if(remove>=digits.size()||digits.right(remove)!=QString(remove,'0'))
            throw std::invalid_argument("The number must resolve to an exact whole integer.");
        digits.chop(remove);
    }
    return canonicalUnsigned(digits);
}
RangeSelection rangeSelection(const QString& first,const QString& second,RangeBound bound){
    RangeSelection selection;selection.start=rangeInputInteger(first);
    const auto value=rangeInputInteger(second);
    selection.end=bound==RangeBound::Length?addUnsigned(selection.start,value):value;
    if(compareUnsigned(selection.start,selection.end)>=0)
        throw std::invalid_argument("Choose a positive length or an end greater than the start.");
    selection.length=subtractUnsigned(selection.end,selection.start);
    selection.firstBits=bitLength(selection.start);selection.lastBits=bitLength(subtractUnsigned(selection.end,"1"));
    return selection;
}
RangeAvailability rangeAvailability(const SourceRegistry& source,const RangeSelection& selection){
    RangeAvailability result;QString cursor=selection.start;QSet<QString> states,owners;
    QVector<const SourceRange*> rows;
    for(const auto& row:source.ranges)if(overlaps(selection.start,selection.end,row.start,row.end))rows.append(&row);
    std::sort(rows.begin(),rows.end(),[](const auto* a,const auto* b){return compareUnsigned(a->start,b->start)<0;});
    bool gap=false;
    for(const auto* row:rows){
        ++result.rows;states.insert(row->status);if(!row->owner.isEmpty())owners.insert(row->owner);
        if(compareUnsigned(row->start,cursor)>0)gap=true;
        if(compareUnsigned(cursor,row->end)<0)cursor=row->end;
    }
    result.fullyPublished=!gap&&compareUnsigned(cursor,selection.end)>=0;
    result.declaredOpen=result.fullyPublished&&states.size()==1&&states.contains("Open");
    result.status=!result.fullyPublished?(rows.isEmpty()?"Unknown":"Partially published"):
                  states.size()==1?*states.constBegin():"Mixed";
    result.owners=owners.values();result.owners.sort(Qt::CaseInsensitive);return result;
}
QString rangePositionAtPixel(const QString& first,const QString& last,int pixel,int width){
    using boost::multiprecision::cpp_int;
    if(width<=0)throw std::invalid_argument("A positive viewport width is required.");
    const cpp_int start(canonicalUnsigned(first).toStdString()),end(canonicalUnsigned(last).toStdString());
    if(end<=start)throw std::invalid_argument("A nonempty viewport is required.");
    const cpp_int position=start+(end-start)*std::clamp(pixel,0,width)/width;
    return QString::fromStdString(position.str());
}
}

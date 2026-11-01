#pragma once
#include "booking/decimal_range.h"

namespace pgn::booking {
// The numeric catalogue has 10^20 positions per page, 10^17 per zone,
// and 10^15 per block. Keep page identities decimal, including beyond the
// current browsable domain; a label grants no availability or mining right.
struct CatalogCoordinates {
    QString numberRange, cells, exact;
    bool valid{};
};
inline CatalogCoordinates catalogCoordinates(const QString& first,const QString& last) {
    CatalogCoordinates result;
    try {
        const auto start=canonicalUnsigned(first),end=canonicalUnsigned(last);
        if(compareUnsigned(start,end)>=0)return result;
        const auto finalPosition=subtractUnsigned(end,"1");
        const auto page=[](const QString& n){return n.size()<=20?QString("0"):n.left(n.size()-20);};
        const auto zone=[](const QString& n){return n.rightJustified(20,'0').right(20).left(3).toInt()+1;};
        const auto block=[](const QString& n){return n.rightJustified(17,'0').right(17).left(2).toInt()+1;};
        const auto a=page(start),z=page(finalPosition);
        result.numberRange=QString("%1.00 – %2.00 ×10²⁰").arg(a,addUnsigned(z,"1"));
        const int firstZone=zone(start),lastZone=zone(finalPosition),firstBlock=block(start),lastBlock=block(finalPosition);
        if(a==z&&firstZone==lastZone)
            result.cells=QString("Zone %1 · %2").arg(firstZone).arg(firstBlock==lastBlock?QString("Block %1").arg(firstBlock):QString("Blocks %1–%2").arg(firstBlock).arg(lastBlock));
        else if(a==z)
            result.cells=QString("Zone %1 / Block %2 → Zone %3 / Block %4").arg(firstZone).arg(firstBlock).arg(lastZone).arg(lastBlock);
        else
            result.cells=QString("Range %1 / Zone %2 / Block %3 → Range %4 / Zone %5 / Block %6").arg(a).arg(firstZone).arg(firstBlock).arg(z).arg(lastZone).arg(lastBlock);
        if(start.rightJustified(15,'0').right(15)!=QString(15,'0')||end.rightJustified(15,'0').right(15)!=QString(15,'0'))result.cells+=" · partial blocks";
        result.exact="["+start+", "+end+")";result.valid=true;
    }catch(const std::invalid_argument&){}
    return result;
}
}

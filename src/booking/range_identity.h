#pragma once
#include "booking/source_registry.h"
#include "booking/decimal_range.h"
#include "booking/catalog_coordinates.h"
#include <stdexcept>

namespace pgn::booking {
inline QString rangePositionLabel(const QString& extent){
    for(const auto unit:{qMakePair(18,"E"),qMakePair(15,"P"),qMakePair(12,"T"),qMakePair(9,"G"),qMakePair(6,"M"),qMakePair(3,"k")})
        if(extent.size()>unit.first&&extent.endsWith(QString(unit.first,'0')))return extent.left(extent.size()-unit.first)+" "+unit.second;
    return extent;
}
inline QString rangeDisplayTitle(const QString& first,const QString& last,const QString& selectedPositions={}){
    const auto coordinates=catalogCoordinates(first,last);if(!coordinates.valid)return "Awaiting a valid interval";
    auto cells=coordinates.cells;const bool spansPages=cells.startsWith("Range ");
    cells.replace(" · partial blocks"," *").replace("Blocks ","B").replace("Block ","B").replace("Zone ","Z").replace("Range ","R");
    const auto page=first.size()<=20?QString("0"):first.left(first.size()-20);
    const auto extent=selectedPositions.isEmpty()?subtractUnsigned(last,first):canonicalUnsigned(selectedPositions);
    return (spansPages?QString{}:"R"+page+" · ")+cells+" · "+rangePositionLabel(extent)+" positions";
}
// Human navigation is scoped to the pinned register, never a substitute for
// canonical endpoints or a claim of exclusive ownership/certified coverage.
struct RangeIdentity {
    QString title, declaration, exact;
    int zone{-1}, firstBlock{}, lastBlock{};
    bool valid{};
    QString displayTitle;
};
inline RangeIdentity identifyRange(const SourceRegistry& source,const QString& first,const QString& last) {
    RangeIdentity result;
    result.title="Awaiting a valid interval";
    result.exact="["+first+", "+last+")";
    result.declaration="Outside register · ownership unknown";
    try {canonicalUnsigned(first);canonicalUnsigned(last);if(compareUnsigned(first,last)>=0)return result;}
    catch(const std::invalid_argument&){return result;}
    result.valid=true;
    result.displayTitle=rangeDisplayTitle(first,last);
    result.title=QString("%1 bits · %2").arg(bitLength(first)).arg(result.displayTitle);
    for(int i=0;i<source.ranges.size();++i){const auto& row=source.ranges[i];
        if(compareUnsigned(row.start,first)>0||compareUnsigned(last,row.end)>0)continue;
        result.zone=i;
        const auto length=subtractUnsigned(row.end,row.start);
        const int count=compareUnsigned(length,"100")<0?length.toInt():100;
        if(count<1)break;
        const auto step=divideUnsignedSmall(length,count);QString cursor=row.start;
        bool exactFirst=false,exactLast=false;
        for(int block=1;block<=count;++block){const auto end=block==count?row.end:addUnsigned(cursor,step);
            if(overlaps(first,last,cursor,end)){if(!result.firstBlock){result.firstBlock=block;exactFirst=first==cursor;}result.lastBlock=block;exactLast=last==end;}
            cursor=end;
        }
        const auto blocks=result.firstBlock==result.lastBlock?QString("Block %1").arg(result.firstBlock):QString("Blocks %1–%2").arg(result.firstBlock).arg(result.lastBlock);
        result.title=QString("%1 bits · Zone %2 · %3%4").arg(bitLength(first)).arg(i+1).arg(blocks,(exactFirst&&exactLast)?QString{}:QString(" · partial"));
        result.declaration=row.status=="Open"?QString("Declared open · no listed owner"):QString("%1 · %2").arg(row.status,row.owner.isEmpty()?QString("owner not listed"):row.owner);
        break;
    }
    return result;
}
}

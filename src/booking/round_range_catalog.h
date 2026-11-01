#pragma once
#include "booking/bit_length_catalog.h"
namespace pgn::booking {
// Numeric pages have1000 zones except the clipped final page at2^95.
// Visibility never implies availability or permission to execute work.
inline const quint64 roundRangeZoneCount=decimalZoneIndex(subtractUnsigned(catalogUpperExclusive(),"1"))+1;
inline const quint64 roundRangeMaximumPage=(roundRangeZoneCount-1)/BitLengthCatalog::pageSize;
inline QString roundRangeEnd(quint64 page){
 if(page>roundRangeMaximumPage)throw std::invalid_argument("Numeric catalog page outside coverage");
 const auto end=QString::number(page+1)+QString(20,'0');
 return compareUnsigned(end,catalogUpperExclusive())>0?catalogUpperExclusive():end;
}
inline BitLengthCatalog roundRangeCatalog(quint64 page,const SourceRegistry& source){
 if(page>roundRangeMaximumPage)throw std::invalid_argument("Numeric catalog page outside coverage");
 BitLengthCatalog c;c.bits=-1;c.page=page;c.firstGrid=page*BitLengthCatalog::pageSize;
 c.count=roundRangeZoneCount;c.start=canonicalUnsigned(QString::number(page)+QString(20,'0'));c.end=roundRangeEnd(page);
 const auto count=std::min(BitLengthCatalog::pageSize,roundRangeZoneCount-c.firstGrid);
 c.ranges.reserve(int(count));
 for(quint64 i=0;i<count;++i){
  const auto grid=c.firstGrid+i;const auto start=canonicalUnsigned(QString::number(grid)+QString(17,'0'));
  auto end=QString::number(grid+1)+QString(17,'0');if(compareUnsigned(end,c.end)>0)end=c.end;
  SourceRange row{qint64(i+1),start,end,"Unknown",{}};
  for(const auto& declared:source.ranges)if(compareUnsigned(declared.start,row.start)<=0&&compareUnsigned(row.end,declared.end)<=0){row.status=declared.status;row.owner=declared.owner;break;}
  c.ranges.append(row);
 }
 return c;
}
}

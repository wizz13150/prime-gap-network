#pragma once
#include "booking/source_registry.h"
#include "booking/decimal_range.h"
#include <algorithm>
#include <stdexcept>

namespace pgn::booking {
inline constexpr int CatalogMinimumBits=65; // Existing historical domains remain visible.
inline constexpr int CatalogMaximumBits=95;

// Complete bit-length domains, virtual pages, exact decimal zone identities.
// Catalog visibility is NOT an availability claim or permission to mine.
struct BitLengthCatalog {
 int bits{};quint64 page{},firstGrid{},count{};
 QString start,end;QVector<SourceRange> ranges;
 static constexpr quint64 pageSize=1000;
 quint64 pageCount()const{return(count+pageSize-1)/pageSize;}
 quint64 firstOrdinal()const{return page*pageSize+1;}
};
inline QString powerOfTwo(int exponent){QString n="1";for(int i=0;i<exponent;++i)n=addUnsigned(n,n);return n;}
inline quint64 decimalZoneIndex(const QString& exact){
 const auto value=canonicalUnsigned(exact);
 if(value.size()<=17)return 0;
 bool ok=false;const auto index=value.left(value.size()-17).toULongLong(&ok);
 if(!ok)throw std::invalid_argument("Catalog zone index exceeds64bit coordinates");
 return index;
}
inline const QString& catalogUpperExclusive(){static const auto upper=powerOfTwo(CatalogMaximumBits);return upper;}
inline BitLengthCatalog bitLengthCatalog(int bits,quint64 requestedPage,const SourceRegistry& registry){
 if(bits<CatalogMinimumBits||bits>CatalogMaximumBits)throw std::invalid_argument("Catalog supports bit lengths65..95 only");
 BitLengthCatalog c;c.bits=bits;c.start=powerOfTwo(bits-1);c.end=powerOfTwo(bits);
 c.firstGrid=decimalZoneIndex(c.start);const auto last=decimalZoneIndex(subtractUnsigned(c.end,"1"));c.count=last-c.firstGrid+1;
 c.page=std::min(requestedPage,c.pageCount()-1);const quint64 offset=c.page*BitLengthCatalog::pageSize,limit=std::min(c.count,offset+BitLengthCatalog::pageSize);
 c.ranges.reserve(int(limit-offset));
 for(quint64 i=offset;i<limit;++i){const auto grid=c.firstGrid+i;auto first=QString::number(grid)+QString(17,'0'),lastEnd=QString::number(grid+1)+QString(17,'0');
  if(compareUnsigned(first,c.start)<0)first=c.start;if(compareUnsigned(lastEnd,c.end)>0)lastEnd=c.end;
  SourceRange row{qint64(i+1),first,lastEnd,"Unknown",{}};
  for(const auto& declared:registry.ranges)if(compareUnsigned(declared.start,first)<=0&&compareUnsigned(lastEnd,declared.end)<=0){row.status=declared.status;row.owner=declared.owner;break;}
  c.ranges.append(row);
 }
 return c;
}
}

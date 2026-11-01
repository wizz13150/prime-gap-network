#include "booking/bit_length_catalog.h"
#include "booking/round_range_catalog.h"
#include <limits>
#include "ui/range_calendar.h"
#include <QCoreApplication>
#include <iostream>
int main(int argc,char** argv){QCoreApplication app(argc,argv);using namespace pgn::booking;SourceRegistry source;
 source.ranges.append({1001,"199900000000000000000","200000000000000000000","Open",{}});
 auto n68=bitLengthCatalog(68,0,source);
 if(n68.start!="147573952589676412928"||n68.end!="295147905179352825856"||n68.count!=1477||n68.pageCount()!=2||n68.ranges.size()!=1000)return 1;
 if(n68.ranges[524].start!=source.ranges[0].start||n68.ranges[524].end!=source.ranges[0].end||n68.ranges[524].status!="Open"||n68.ranges[0].status!="Unknown")return 2;
 for(int bits=65;bits<=CatalogMaximumBits;++bits){auto head=bitLengthCatalog(bits,0,source),tail=bitLengthCatalog(bits,head.pageCount()-1,source);
  if(head.ranges.first().start!=head.start||tail.ranges.last().end!=head.end||head.ranges.size()>1000||tail.ranges.size()>1000)return 3;
  for(const auto& page:{head,tail})for(int i=0;i<page.ranges.size();++i){const auto& r=page.ranges[i];
   if(compareUnsigned(r.start,r.end)>=0||bitLength(r.start)!=bits||(i&&page.ranges[i-1].end!=r.start))return 4;
   const auto cells=pgn::ui::rangeCalendarSlots(r.start,r.end);if(cells.size()!=100||cells.first().start!=r.start||cells.last().end!=r.end)return 5;
  }
 }
 auto n95=bitLengthCatalog(95,0,source);
 auto wide=bitLengthCatalog(95,2147484,source);
 auto tail=bitLengthCatalog(95,n95.pageCount()-1,source);
 if(wide.ranges.first().row<=std::numeric_limits<int>::max()||tail.ranges.last().row!=qint64(n95.count))return 6;
 if(tail.ranges.last().end!=powerOfTwo(95)||roundRangeMaximumPage!=396140812ULL)return 7;
 auto lastRound=roundRangeCatalog(roundRangeMaximumPage,source);
 if(lastRound.ranges.size()!=572||lastRound.end!=powerOfTwo(95)||lastRound.ranges.last().end!=lastRound.end)return 8;
 for(qsizetype i=1;i<lastRound.ranges.size();++i)if(lastRound.ranges[i-1].end!=lastRound.ranges[i].start)return 9;
 bool refused=false;try{roundRangeCatalog(roundRangeMaximumPage+1,source);}catch(const std::invalid_argument&){refused=true;}if(!refused)return 10;
 refused=false;try{decimalZoneIndex(QString(80,'9'));}catch(const std::invalid_argument&){refused=true;}if(!refused)return 11;
 std::cout<<"PASS complete65..95bits,1477 68 bits zones, bounded1000-page, exact100-block partitions and UNKNOWN outside pinned source\n";
}

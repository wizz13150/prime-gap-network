#pragma once
#include "booking/booking_store.h"
#include "booking/decimal_range.h"
namespace pgn::booking {
// The accepted numerical calendar has 1000 zones of 10^17 per 10^20 page.
// A legacy multi-zone intent remains one explicit range, never silently split.
inline ExactBlockSelection queueZone(const Booking& job){
 const auto first=canonicalUnsigned(job.start),last=canonicalUnsigned(job.end);
 const auto zone=first.size()>17?canonicalUnsigned(first.left(first.size()-17)+QString(17,'0')):QString("0");
 const auto end=addUnsigned(zone,"100000000000000000");
 if(compareUnsigned(last,end)>0)return {first,last,first,last};
 return {zone,end,zone,end};
}
}

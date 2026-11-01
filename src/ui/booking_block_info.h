#pragma once
#include "booking/booking_model.h"
#include "booking/range_identity.h"
#include "ui/booking_labels.h"
#include "ui/range_calendar.h"
#include <QSet>

namespace pgn::ui {
// Shared left/right projection. Local work cannot become a public lease, and
// a physical scan cannot become an independent proof through presentation.
inline QString bookingBlockInfo(const pgn::booking::BookingModel& model,const QString& first,const QString& last){
 using namespace pgn::booking;
 const auto identity=identifyRange(model.source(),first,last);
 QStringList lines{identity.title,"["+first+", "+last+")"};
 bool declared=false;
 for(const auto& row:model.source().ranges)if(overlaps(first,last,row.start,row.end)){
  lines<<"Community: "+row.status+" · "+(row.owner.isEmpty()?"no listed owner":row.owner);declared=true;
 }
 if(!declared)lines<<"Community: unknown · no confirmed reservation received";
 auto known=model.completedRanges();known+=model.jobs();QSet<QString> seen;
 for(const auto& job:known)if(overlaps(first,last,job.start,job.end)&&!seen.contains(job.id)){
  seen.insert(job.id);lines<<model.researcherName()+" · "+localWorkLabel(job.state)+" · "+job.device;
  if(job.state=="SOURCE_FINISHED")lines<<"Processed locally · independent coverage proof pending";
 }
 const auto& live=model.runtime();
 if(!live.run.isEmpty()&&overlaps(first,last,live.start,live.end)){
  lines<<"Processing: "+model.researcherName()+" · "+live.label;
  if(!live.positions.isEmpty()){
   const auto span=rangeCalendarInterval({first,last},live.start,live.end,addUnsigned(live.start,live.positions));
   if(span&&span->progressEnd)lines<<QString("This block: %1% processed · raw cursor").arg(*span->progressEnd*100,0,'f',5);
  }else lines<<"Progress unavailable · no value inferred";
  lines<<"Observed: "+live.observedUtc.toUTC().toString(Qt::ISODateWithMs);
 }
 lines<<"Local work is not a confirmed public lease. Processed is not independently certified.";
 return lines.join('\n');
}
}

#include "ui/range_atlas.h"
#include "ui/prototype_community_atlas.h"
#include "ui/booking_labels.h"
#include "booking/booking_model.h"
#include "booking/decimal_range.h"
#include "booking/range_identity.h"
#include "ui/booking_block_info.h"
#include <memory>
#include <limits>

namespace pgn::ui {
namespace {
double atlasCoordinate(const QString& exact){const auto n=pgn::booking::canonicalUnsigned(exact);if(n.isEmpty())return 0.;
 return n.size()>15?(n.left(n.size()-15)+"."+n.right(15)).toDouble():("0."+n.rightJustified(15,'0')).toDouble();}
}
QWidget* createRangeAtlas(pgn::booking::BookingModel* model,bool local,std::function<void(const QString&,const QString&)> inspect,DeviceAccent accent,QWidget* parent){
 auto* atlas=new PGNCommunityAtlas(parent);atlas->setObjectName(local?"pgn-local-range-atlas":"pgn-source-atlas");atlas->setProperty("pgn_local_atlas",local);
 atlas->resetControl()->setObjectName(local?"pgn-local-atlas-reset":"pgn-source-atlas-reset");
 atlas->resetControl()->setFixedSize(114,21);
 atlas->blockTooltip=[model](const QString& first,const QString& last){return bookingBlockInfo(*model,first,last);};
 auto* title=atlas->findChild<QLabel*>();if(title)title->setText(local?"OUR WORK · NUMBER RANGES":"COMMUNITY · RESEARCHERS & RESERVED BLOCKS");
 auto* focus=new HorizonGraphResetButton("ZOOM TO OUR WORK",114,atlas);focus->setZoomed(true);focus->setObjectName(local?"pgn-local-atlas-focus":"pgn-source-atlas-focus");
 static_cast<QHBoxLayout*>(atlas->layout())->insertWidget(1,focus);
 atlas->inspect=[model,local,inspect](const QJsonObject& row){if(row["_block"].toBool()){if(inspect)inspect(row["start"].toString(),row["end"].toString());}else if(local||row["status"].toString().startsWith("LOCAL_"))model->select(row["id"].toString());else if(inspect)inspect(row["start"].toString(),row["end"].toString());};
 // Fixed original axis: changes to registry extent must not move a saved zoom.
 const auto coordinate=atlasCoordinate;
 const auto focusRun=[atlas,model,coordinate]{if(!model->source().valid())return;const auto& runtime=model->runtime();
  if(model->viewIsRoundRange()&&!runtime.start.isEmpty())model->setRoundRange(pgn::booking::decimalZoneIndex(runtime.start)/1000);
  if(model->viewBitLength()&&!runtime.start.isEmpty()){
   const int bits=pgn::booking::bitLength(runtime.start);model->setViewBitLength(bits);
   const auto first=pgn::booking::decimalZoneIndex(pgn::booking::powerOfTwo(bits-1));
   model->setViewPage((pgn::booking::decimalZoneIndex(runtime.start)-first)/pgn::booking::BitLengthCatalog::pageSize);
  }
  QString first=runtime.start,last=runtime.end;
  for(const auto& job:model->jobs())if(job.state=="ACTIVE"||job.state=="QUEUED"||job.state=="ARMED"||job.state=="DISPATCHING"||job.state=="DISPATCHED"){
   if(first.isEmpty()||pgn::booking::compareUnsigned(job.start,first)<0)first=job.start;
   if(last.isEmpty()||pgn::booking::compareUnsigned(job.end,last)>0)last=job.end;
  }
  // Focus the enclosing zone(s), not a tiny block stretched to viewport.
  // This preserves context and exposes completed neighbours in the same100.
  if(!first.isEmpty()&&!last.isEmpty()){
   const auto& ranges=model->viewRanges();
   for(const auto& row:ranges)if(pgn::booking::compareUnsigned(row.start,first)<=0&&pgn::booking::compareUnsigned(first,row.end)<0){first=row.start;break;}
   for(const auto& row:ranges)if(pgn::booking::compareUnsigned(row.start,last)<0&&pgn::booking::compareUnsigned(last,row.end)<=0){last=row.end;break;}
   atlas->focusRange(coordinate(first),coordinate(last));
  }};
 QObject::connect(focus,&QPushButton::clicked,atlas,[atlas,focusRun]{atlas->setProperty("pgn_manual_atlas_view",false);focusRun();});
 // The original reset button is a human camera choice, not permission to
 // snap back to the active range on the next lifecycle update.
 QObject::connect(atlas->resetControl(),&QPushButton::clicked,atlas,[atlas]{atlas->setProperty("pgn_manual_atlas_view",true);});
 struct State{QString sourceKey,deviceKey,viewKey;quint64 revision=std::numeric_limits<quint64>::max();bool initialized=false;};auto state=std::make_shared<State>();
 auto refresh=[atlas,model,local,accent,coordinate,focusRun,state]{const auto& source=model->source();const auto key=source.sha256+source.error+model->viewKey();
  const auto& runtime=model->runtime();const auto deviceKey=runtime.device+runtime.label;
  if(!state->initialized||state->sourceKey!=key||state->revision!=model->jobsRevision()||state->deviceKey!=deviceKey){
   const bool viewChanged=state->initialized&&state->viewKey!=model->viewKey();state->viewKey=model->viewKey();
   state->sourceKey=key;state->deviceKey=deviceKey;state->revision=model->jobsRevision();QJsonArray rows,zones;int localCount=0;
   auto append=[&](const QString& id,const QString& start,const QString& end,const QString& owner,const QString& status,const QString& label,const QColor& color){
    QJsonObject row{{"id",id},{"start",start},{"end",end},{"owner",owner},{"status",status},{"_label",QString::number(pgn::booking::bitLength(start))+" bits · "+label},
     {"_detail",start+" → "+end},
     {"axis_start",QString::number(coordinate(start),'g',17)},{"axis_end",QString::number(coordinate(end),'g',17)}};
    row.insert("_label",pgn::booking::identifyRange(source,start,end).title+" · "+label);
    if(color.isValid())row.insert("_device_color",color.name());rows.append(row);};
   auto known=model->completedRanges();known+=model->jobs();
   if(source.valid()){
    const auto& visible=model->viewRanges();
    for(int z=0;z<visible.size();++z){const auto& row=visible[z];const auto ordinal=model->viewFirstOrdinal()+z;
     auto zoneLabel=QString(model->viewBitLength()?"Zone %1":"Zone %1").arg(ordinal);const auto original=pgn::booking::identifyRange(source,row.start,row.end);
     if(model->viewBitLength()&&original.zone>=0)zoneLabel+=QString(" · register zone %1").arg(original.zone+1);
     zones.append(QJsonObject{{"zone",int(ordinal)},{"label",zoneLabel},{"start",row.start},{"end",row.end},{"owner",row.owner},{"status","DECLARED_"+row.status.toUpper()},{"axis_start",QString::number(coordinate(row.start),'g',17)},{"axis_end",QString::number(coordinate(row.end),'g',17)}});
     if(local)continue;
     // Subtract only exact local allocations from OPEN display spans. The
     // pinned community declaration is untouched; unknown public confirmation
     // remains explicit in each Wizz tooltip. Never claim the whole zone.
     QVector<QPair<QString,QString>> spans{{row.start,row.end}};
     if(row.status=="Open")for(const auto& job:known)if(job.state=="SOURCE_FINISHED"||job.state=="ACTIVE"||job.state=="QUEUED"||job.state=="ARMED"||job.state=="DISPATCHING"||job.state=="DISPATCHED"){
      QVector<QPair<QString,QString>> next;
      for(const auto& span:spans){using namespace pgn::booking;
       if(!overlaps(span.first,span.second,job.start,job.end)){next.append(span);continue;}
       if(compareUnsigned(span.first,job.start)<0)next.append({span.first,job.start});
       if(compareUnsigned(job.end,span.second)<0)next.append({job.end,span.second});
      }spans=next;
     }
     for(const auto& span:spans)append(QString::number(row.row)+"/"+span.first,span.first,span.second,row.owner,"DECLARED_"+row.status.toUpper(),row.status.toUpper(),{});
    }
    const auto visibleJob=[&visible](const auto& job){return !visible.isEmpty()&&pgn::booking::overlaps(visible.first().start,visible.last().end,job.start,job.end);};
    for(const auto& job:model->jobs())if(visibleJob(job)){
     append(job.id,job.start,job.end,model->researcherName(),"LOCAL_"+job.state,localWorkLabel(job.state),QColor("#ffad4a"));++localCount;
    }
    int processed=0;for(const auto& job:model->completedRanges())if(visibleJob(job)){
     append(job.id,job.start,job.end,model->researcherName(),"LOCAL_SOURCE_FINISHED","PROCESSED · PROOF PENDING",QColor("#46c8ee"));++processed;
    }
    atlas->setProperty("pgn_atlas_processed_segments",processed);
   }
   // The original component reads the full axis from its first/last rows.
   // Sorting screen coordinates changes no source row, job or exact bound.
   std::vector<QJsonObject> sorted;for(const auto& row:rows)sorted.push_back(row.toObject());
   std::sort(sorted.begin(),sorted.end(),[](const auto& x,const auto& y){return x["axis_start"].toString().toDouble()<y["axis_start"].toString().toDouble();});
   rows={};for(const auto& row:sorted)rows.append(row);
   atlas->setData(QJsonObject{{"runs",rows},{"zones",zones},{"researcher",model->researcherName()},{"source",source.sha256}});
   if(viewChanged)atlas->clickReset();
   atlas->setProperty("pgn_source_atlas_rows",source.ranges.size());atlas->setProperty("pgn_atlas_segments",rows.size());
   atlas->setProperty("pgn_catalog_bits",model->viewBitLength());atlas->setProperty("pgn_catalog_total_zones",qulonglong(model->viewCount()));atlas->setProperty("pgn_catalog_page_zones",model->viewRanges().size());
   atlas->setProperty("pgn_atlas_local_segments",localCount);atlas->setProperty("pgn_atlas_combined_context",!local);
   if(local&&!atlas->property("pgn_manual_atlas_view").toBool())focusRun();state->initialized=true;
  }
  atlas->setProgress(runtime.run,runtime.fraction,runtime.observedUtc.toUTC().toString(Qt::ISODateWithMs),runtime.start,runtime.end,runtime.positions);
 };
 refresh();model->subscribe(atlas,refresh);return atlas;
}
QRectF rangeAtlasJobRect(QWidget* widget,const QString& id){if(auto* atlas=dynamic_cast<PGNCommunityAtlas*>(widget))return atlas->jobRect(id);return {};}
void bindRangeAtlasContext(QWidget* widget,BookingContextHandler handler){if(auto* atlas=dynamic_cast<PGNCommunityAtlas*>(widget))atlas->context=std::move(handler);}
void focusRangeAtlas(QWidget* widget,const QString& first,const QString& last){
 if(auto* atlas=dynamic_cast<PGNCommunityAtlas*>(widget)){
  const auto a=pgn::booking::canonicalUnsigned(first),b=pgn::booking::canonicalUnsigned(last);
  if(a.isEmpty()||b.isEmpty()||pgn::booking::compareUnsigned(a,b)>=0)return;
  atlas->setProperty("pgn_manual_atlas_view",true);
  atlas->setProperty("pgn_atlas_inspected_start",a);atlas->setProperty("pgn_atlas_inspected_end",b);
  atlas->focusRange(atlasCoordinate(a),atlasCoordinate(b));
 }
}
}

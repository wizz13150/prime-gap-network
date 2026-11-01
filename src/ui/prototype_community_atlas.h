// Renderer reused from the CLOSED first PGN prototype, not a reimplementation.
// Original archived prototype remains unchanged; this reused renderer adds
// explicit local lanes and inspection without implicit camera movement.
#include <QWidget>
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>
#include <QToolTip>
#include <QLabel>
#include <QHBoxLayout>
#include "horizon_graph_controls.h"
#include "horizon_device_visuals.h"
#include "ui/range_visuals.h"
#include "ui/range_calendar.h"
#include "ui/booking_labels.h"
#include "ui/zone_source_coverage.h"
#include "booking/decimal_range.h"
#include <functional>
#include <cmath>
#include <algorithm>
#include <optional>
#include <limits>
#include <QDateTime>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>
#pragma once
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include "ui/booking_context_request.h"
// Screen positions use the registry's small *1e15 coordinates. Exact decimal
// endpoints remain strings in drilldown/API; screen rounding is not ownership.
class PGNCommunityAtlas final:public QWidget{
 QJsonObject community;QJsonArray rows,zones;QStringList owners;QVector<QPair<QRectF,QJsonObject>> hits;double lo=100000,hi=200000,domainLo=100000,domainHi=200000;bool zoomed=false,dragging=false;QPointF pressed;double pressedLo=0,pressedHi=0;HorizonGraphResetButton* reset;QImage cached;QString activeId,selectedId;std::optional<double> activeProgress;QString progressUtc,progressStart,progressEnd,progressPositions;
 QRectF plot()const{return QRectF(145,54,std::max(1,width()-178),std::max(1,std::min(height()-118,std::max(230,int(owners.size())*68))));}
 QRectF projectedRange(double a,double b,int lane)const{
  const auto grid=plot();const auto rowh=grid.height()/std::max(1,int(owners.size()));
  const auto exactWidth=(std::min(b,hi)-std::max(a,lo))/(hi-lo)*grid.width();
  // The same projection/minimum applies to every owner. A larger local hit
  // target must never masquerade as a wider numerical zone.
  const auto displayWidth=std::max(2.,exactWidth);
  const auto x=std::clamp(grid.left()+(std::max(a,lo)-lo)/(hi-lo)*grid.width(),grid.left(),std::max(grid.left(),grid.right()-displayWidth));
  const auto height=std::clamp(rowh-12.,24.,58.);
  return {x,grid.top()+lane*rowh+(rowh-height)/2,displayWidth,height};
 }
 QSet<QString> selectedZones,sweepBase;int sweepAnchor=-1;bool sweeping=false,subtractSweep=false,ctrlPress=false;
 QString zoneKey(const QJsonObject& zone)const{return zone["start"].toString()+"/"+zone["end"].toString();}
 int zoneAt(const QPointF& point,bool requireHit=true)const{
  const QJsonObject* hit=nullptr;for(auto i=hits.crbegin();i!=hits.crend();++i)if(i->first.contains(point)){hit=&i->second;break;}
  if(requireHit&&!hit)return -1;
  if(hit&&(hit->value("_block").toBool()||hit->value("_zone_frame").toBool()||hit->value("_zone_span").toBool())){
   const auto a=hit->value("_zone_start").toString(hit->value("start").toString()),b=hit->value("_zone_end").toString(hit->value("end").toString());
   for(int i=0;i<zones.size();++i){const auto z=zones[i].toObject();if(pgn::booking::compareUnsigned(z["start"].toString(),a)<=0&&pgn::booking::compareUnsigned(b,z["end"].toString())<=0)return i;}
  }
  const double axis=lo+(point.x()-plot().left())/plot().width()*(hi-lo);
  for(int i=0;i<zones.size();++i){const auto z=zones[i].toObject();if(z["axis_start"].toString().toDouble()<=axis&&axis<z["axis_end"].toString().toDouble())return i;}
  // A minimum-size one-zone marker can be clamped at an axis edge. Resolve
  // its exact metadata rather than choosing a neighbouring rounded pixel.
  if(hit)for(int i=0;i<zones.size();++i){const auto z=zones[i].toObject();if(pgn::booking::compareUnsigned(z["start"].toString(),hit->value("start").toString())<=0&&pgn::booking::compareUnsigned(hit->value("end").toString(),z["end"].toString())<=0)return i;}
  return -1;
 }
 void sweepTo(int index){if(sweepAnchor<0||index<0)return;selectedZones=sweepBase;for(int i=std::min(sweepAnchor,index);i<=std::max(sweepAnchor,index);++i){const auto key=zoneKey(zones[i].toObject());if(subtractSweep)selectedZones.remove(key);else selectedZones.insert(key);}setProperty("pgn_atlas_selected_zones",selectedZones.size());QWidget::update();}
 void paintZoneSelection(QPainter& p){if(selectedZones.isEmpty())return;QVector<QJsonObject> selected;for(const auto& value:zones){const auto z=value.toObject();if(selectedZones.contains(zoneKey(z))&&z["axis_start"].toString().toDouble()<hi&&z["axis_end"].toString().toDouble()>lo)selected.append(z);}p.save();p.setPen(QPen(QColor("#e8fff8"),2));p.setBrush(Qt::NoBrush);
  for(const auto& hit:hits){const auto r=hit.second;if(r["_block"].toBool())continue;
   for(const auto& z:selected){if(!pgn::booking::overlaps(z["start"].toString(),z["end"].toString(),r["start"].toString(),r["end"].toString()))continue;
    const auto lane=owners.indexOf(ownerName(r));const auto box=r["_zone_frame"].toBool()||r["_zone_span"].toBool()?hit.first:projectedRange(z["axis_start"].toString().toDouble(),z["axis_end"].toString().toDouble(),lane);
    if(lane>=0||r["_zone_frame"].toBool()){if(box.width()<=3)p.fillRect(box,QColor("#e8fff8"));else p.drawRoundedRect(box.adjusted(1,1,-1,-1),4,4);}
   }}p.restore();
 }
 QTimer motion;QElapsedTimer motionClock;double targetLo=lo,targetHi=hi,zoomAnchor=.5;QImage previousMode,lastFrame;QElapsedTimer modeClock;int detailCount=0;
 QPointF hoverPosition;QPoint hoverGlobal;bool hoverValid=false;QString hoverText;
 void refreshHover(){if(!hoverValid||dragging||!underMouse())return;for(auto it=hits.crbegin();it!=hits.crend();++it)if(it->first.contains(hoverPosition)){
  const auto r=it->second;QString text;if(r["_block"].toBool()&&blockTooltip)text=blockTooltip(r["start"].toString(),r["end"].toString());
  else{const bool local=r["status"].toString().startsWith("LOCAL_");const QString detail=local?(r["id"].toString()==activeId?"Raw progress observation "+progressUtc:"Local lifecycle state; no independent coverage certificate"):"Community declaration · click inspects zone, does not select all blocks";
   text=r["_label"].toString()+"\n["+r["start"].toString()+", "+r["end"].toString()+")\n"+detail+(r["_marker"].toBool()?"\nMinimum-size marker · zoom for interval width":"");}
  if(text!=hoverText){hoverText=text;setProperty("pgn_live_hover_text",text);QToolTip::showText(hoverGlobal,text,this);}return;}
  hoverText.clear();QToolTip::hideText();}
 void stopMotion(){motion.stop();targetLo=lo;targetHi=hi;previousMode={};}
 void stepMotion(){
  const double dt=std::clamp(double(motionClock.restart()),1.,50.),alpha=-std::expm1(-dt/42.);
  const double oldSpan=hi-lo,targetSpan=targetHi-targetLo;
  const double span=std::exp(std::log(oldSpan)+alpha*(std::log(targetSpan)-std::log(oldSpan)));
  const double pivot=lo+oldSpan*zoomAnchor,goal=targetLo+targetSpan*zoomAnchor;
  lo=pivot+(goal-pivot)*alpha-span*zoomAnchor;hi=lo+span;clamp();
  if(std::abs(std::log((hi-lo)/targetSpan))<.00008&&std::abs(lo-targetLo)<targetSpan*.00008){lo=targetLo;hi=targetHi;motion.stop();}
  setProperty("pgn_atlas_motion_frames",property("pgn_atlas_motion_frames").toInt()+1);update();
 }
 QColor color(QString s)const{return s=="DECLARED_COMPLETE"?QColor("#46c8ee"):s=="DECLARED_RESERVED"?QColor("#ffad4a"):s=="DECLARED_OPEN"?QColor("#69efc5"):QColor("#657b81");}
 QString ownerName(const QJsonObject& row)const{const auto name=row["owner"].toString();return !name.isEmpty()?name:row["status"]=="DECLARED_UNKNOWN"?QString("Unknown"):QString("Declared open");}
 void clamp(){const double span=hi-lo;if(span>=domainHi-domainLo){lo=domainLo;hi=domainHi;return;}if(lo<domainLo){lo=domainLo;hi=lo+span;}if(hi>domainHi){hi=domainHi;lo=hi-span;}}
public:
 std::function<void(QJsonObject)> inspect;
 pgn::ui::BookingContextHandler context;
 explicit PGNCommunityAtlas(QWidget*p=nullptr):QWidget(p){setMinimumHeight(286);setMouseTracking(true);auto*l=new QHBoxLayout(this);l->setContentsMargins(14,6,14,0);l->setAlignment(Qt::AlignTop);auto*t=new QLabel("COMMUNITY REGISTER · RESEARCHER × NUMBER RANGE");t->setStyleSheet("font-size:10px;");l->addWidget(t,1);reset=new HorizonGraphResetButton;l->addWidget(reset);connect(reset,&QPushButton::clicked,this,[this]{stopMotion();lo=domainLo;hi=domainHi;targetLo=lo;targetHi=hi;zoomed=false;update();});motion.setTimerType(Qt::PreciseTimer);motion.setInterval(16);connect(&motion,&QTimer::timeout,this,[this]{stepMotion();});}
 void setData(QJsonObject data){const auto anchor=sweeping&&sweepAnchor>=0&&sweepAnchor<zones.size()?zoneKey(zones[sweepAnchor].toObject()):QString{};community=data;rows=data["runs"].toArray();zones=data["zones"].toArray();QSet<QString> present;int newAnchor=-1;for(int i=0;i<zones.size();++i){const auto key=zoneKey(zones[i].toObject());present.insert(key);if(key==anchor)newAnchor=i;}selectedZones.intersect(present);sweepBase.intersect(present);if(sweeping){sweepAnchor=newAnchor;if(newAnchor<0){sweeping=false;dragging=false;}}setProperty("pgn_atlas_selected_zones",selectedZones.size());owners.clear();double first=std::numeric_limits<double>::max(),last=std::numeric_limits<double>::lowest();for(auto v:rows){auto r=v.toObject();auto name=ownerName(r);if(!owners.contains(name))owners.append(name);first=std::min(first,r["axis_start"].toString().toDouble());last=std::max(last,r["axis_end"].toString().toDouble());}const auto own=data["researcher"].toString();if(owners.removeAll(own))owners.prepend(own);if(!rows.isEmpty()){domainLo=first;domainHi=last;if(!zoomed){lo=domainLo;hi=domainHi;}clamp();}update();}
 QJsonArray observations()const{return rows;}
 std::function<QString(const QString&,const QString&)> blockTooltip;
 double axisStart()const{return lo;}double axisEnd()const{return hi;}
 QPointF pointForOwner(QString owner)const{for(const auto&h:hits)if(h.second["owner"].toString()==owner&&h.second["status"].toString()=="DECLARED_RESERVED")return h.first.center();return QPointF(-1,-1);}
 void clickReset(){setProperty("pgn_manual_atlas_view",true);reset->click();}
 HorizonGraphResetButton* resetControl()const{return reset;}
 QRectF jobRect(const QString& id)const{for(const auto& h:hits)if(h.second["id"].toString()==id)return h.first;return {};}
 void focusRange(double a,double b){stopMotion();const double span=std::max(1e-7,b-a);lo=a-.2*span;hi=b+.2*span;clamp();targetLo=lo;targetHi=hi;zoomed=true;update();}
 void setProgress(QString id,std::optional<double> fraction,QString utc,QString first={},QString last={},QString positions={}){activeId=std::move(id);activeProgress=fraction;progressUtc=std::move(utc);progressStart=std::move(first);progressEnd=std::move(last);progressPositions=std::move(positions);QWidget::update();}
 void update(){cached={};QWidget::update();}
 bool zoomAnimating()const{return motion.isActive();}
 QVector<pgn::booking::ExactBlockSelection> zoneSelections()const{QVector<pgn::booking::ExactBlockSelection> result;for(const auto& value:zones){const auto z=value.toObject();if(selectedZones.contains(zoneKey(z)))result.append({z["start"].toString(),z["end"].toString(),z["start"].toString(),z["end"].toString()});}return result;}
 QPointF zonePoint(const QString& first)const{for(const auto& value:zones){const auto z=value.toObject();if(z["start"].toString()!=first)continue;for(const auto& h:hits){const auto r=h.second;if(!pgn::booking::overlaps(z["start"].toString(),z["end"].toString(),r["start"].toString(),r["end"].toString()))continue;if(r["_block"].toBool()||r["_zone_frame"].toBool()||r["_zone_span"].toBool())return h.first.center();const int lane=owners.indexOf(ownerName(r));if(lane>=0)return projectedRange(z["axis_start"].toString().toDouble(),z["axis_end"].toString().toDouble(),lane).center();}}return {-1,-1};}
protected:
 void contextMenuEvent(QContextMenuEvent* e)override{
  if(!context)return;stopMotion();const int index=zoneAt(e->pos());if(index<0)return;const auto z=zones[index].toObject();if(!selectedZones.contains(zoneKey(z))){selectedZones={zoneKey(z)};setProperty("pgn_atlas_selected_zones",1);QWidget::update();}
  pgn::ui::BookingContextRequest request{z["start"].toString(),z["end"].toString(),z["start"].toString(),z["end"].toString(),e->globalPos(),false};request.zones=zoneSelections();context(request);e->accept();
 }
 bool event(QEvent* e)override{
  if(e->type()==QEvent::Wheel||(e->type()==QEvent::MouseMove&&dragging))setProperty("pgn_manual_atlas_view",true);
  return QWidget::event(e);
 }
 void paintEvent(QPaintEvent*)override{QElapsedTimer renderClock;renderClock.start();if(cached.size()!=size()){cached=QImage(size(),QImage::Format_ARGB32_Premultiplied);cached.fill(Qt::transparent);paintStatic();const int nextDetail=property("pgn_atlas_zoom_zones").toInt();if(nextDetail!=detailCount&&!lastFrame.isNull()){previousMode=lastFrame;modeClock.start();}detailCount=nextDetail;lastFrame=cached;}QPainter p(this);p.drawImage(0,0,cached);
  if(!previousMode.isNull()){const double blend=std::clamp(modeClock.elapsed()/110.,0.,1.);if(blend<1){p.setOpacity(1-blend);p.drawImage(rect(),previousMode);p.setOpacity(1);QTimer::singleShot(16,this,[this]{QWidget::update();});}else previousMode={};}
  for(const auto& h:hits){if(h.second["_block"].toBool()){
   if(!progressStart.isEmpty()&&!progressEnd.isEmpty()&&!progressPositions.isEmpty())if(const auto span=pgn::ui::rangeCalendarInterval({h.second["start"].toString(),h.second["end"].toString()},progressStart,progressEnd,pgn::booking::addUnsigned(progressStart,progressPositions)))pgn::ui::drawRangeSpan(p,h.first,QColor("#ffad4a"),span->begin,span->end,span->progressEnd);
  }else if(!activeId.isEmpty()&&h.second["id"].toString()==activeId){const auto c=QColor(h.second["_device_color"].toString("#ffad4a"));
   if(h.second["_zone_span"].toBool()){
    if(!progressPositions.isEmpty())if(const auto span=pgn::ui::rangeCalendarInterval({h.second["_zone_start"].toString(),h.second["_zone_end"].toString()},progressStart,progressEnd,pgn::booking::addUnsigned(progressStart,progressPositions)))pgn::ui::drawRangeSpan(p,h.first,c,span->begin,span->end,span->progressEnd);
   }else pgn::ui::drawRangeTile(p,h.first,c,h.second["_label"].toString()+" · "+(activeProgress?QString::number(*activeProgress*100,'f',4)+"%":"UNKNOWN"),h.second["_detail"].toString(),activeProgress,true);}}
  paintZoneSelection(p);setProperty("pgn_atlas_current_visible",!jobRect(activeId).isEmpty());setProperty("pgn_atlas_visible_cases",hits.size());setProperty("pgn_atlas_render_ns",renderClock.nsecsElapsed());}
 bool paintZone(QPainter& p){
  setProperty("pgn_atlas_zoom_blocks",0);setProperty("pgn_atlas_zoom_zones",0);if(!zoomed)return false;
  QVector<QJsonObject> visible;
  for(const auto& value:zones){const auto zone=value.toObject();const double a=zone["axis_start"].toString().toDouble(),b=zone["axis_end"].toString().toDouble();if(a<hi&&b>lo)visible.append(zone);}
  // Semantic zoom is a calendar of EVERY intersecting zone, never a
  // special-case current zone. At overview scale retain the original axis.
  if(visible.isEmpty()||visible.size()>3)return false;
  const double cardWidth=(width()-28.-12.*(visible.size()-1))/visible.size();
  if(cardWidth<190)return false;
  int totalCells=0;for(int z=0;z<visible.size();++z){const auto& zone=visible[z];
  const auto cells=pgn::ui::rangeCalendarSlots(zone["start"].toString(),zone["end"].toString());totalCells+=cells.size();
  const QRectF container(14+z*(cardWidth+12),46,cardWidth,std::max(150,height()-85));
  auto containerHit=zone;containerHit["_zone_frame"]=true;hits.append({container,containerHit});
  pgn::ui::drawRangeTile(p,container,QColor("#477b85"),{},{});
  const QRectF area=container.adjusted(8,55,-8,-8);const double cw=area.width()/10,ch=area.height()/10;
  p.setFont(QFont("Segoe UI",10,QFont::DemiBold));p.setPen(QColor("#d9fff3"));
  const auto zoneLabel=zone["label"].toString(QString("Zone %1").arg(zone["zone"].toInt()));
  p.drawText(container.adjusted(9,5,-9,-container.height()+26),Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(QString("%1 bits · %2 · 100 blocks").arg(pgn::booking::bitLength(zone["start"].toString())).arg(zoneLabel),Qt::ElideRight,int(container.width()-18)));
  const auto declaration=zone["owner"].toString().isEmpty()?QString("Community: %1").arg(zone["status"].toString().mid(9).toLower()):QString("Community: %1 · %2").arg(zone["owner"].toString(),zone["status"].toString().mid(9).toLower());
  p.setFont(QFont("Segoe UI",8));p.setPen(QColor("#9bc6c9"));p.drawText(QRectF(container.left()+9,container.top()+29,container.width()-18,19),Qt::AlignLeft|Qt::AlignVCenter,p.fontMetrics().elidedText(declaration,Qt::ElideRight,int(container.width()-18)));
  for(int i=0;i<cells.size();++i){const auto& slot=cells[i];QJsonObject hit{{"start",slot.start},{"end",slot.end},{"status",zone["status"]},{"owner",zone["owner"]},{"_block",true},{"_label",QString("%1 · Block %2").arg(zoneLabel).arg(i+1)}};
   const QRectF box(area.left()+(i%10)*cw,area.top()+(i/10)*ch,cw-3,ch-3);
   pgn::ui::drawRangeTile(p,box,color(zone["status"].toString()),{}, {}, {},false);
   for(const auto& value:rows){const auto row=value.toObject();if(!row["status"].toString().startsWith("LOCAL_"))continue;
    if(const auto span=pgn::ui::rangeCalendarInterval(slot,row["start"].toString(),row["end"].toString())){pgn::ui::drawRangeSpan(p,box,QColor(row["_device_color"].toString("#ffad4a")),span->begin,span->end);hit["id"]=row["id"];hit["owner"]=row["owner"];hit["_detail"]=row["_label"];}}
   p.setFont(QFont("Segoe UI",ch<22?8:10,QFont::DemiBold));p.setPen(QColor("#e8fff8"));
   // The public owner is already shown in the zone header. Repeat a name
   // only for an allocation override; keep the full owner in every hit/tooltip.
   const auto blockOwner=hit["owner"].toString();
   const bool ownerOverride=!hit["id"].toString().isEmpty()||blockOwner!=zone["owner"].toString();
   const auto ownerLabel=ch>=40&&ownerOverride&&!blockOwner.isEmpty()?"\n"+p.fontMetrics().elidedText(blockOwner,Qt::ElideRight,std::max(1,int(box.width()-4))):QString{};
   p.drawText(box.adjusted(2,1,-2,-1),Qt::AlignCenter,QString::number(i+1).rightJustified(2,'0')+ownerLabel);hits.append({box,hit});
  }}
  p.setFont(QFont("Segoe UI",8));p.setPen(QColor("#9bc6c9"));p.drawText(QRectF(14,height()-33,width()-28,28),Qt::AlignLeft|Qt::AlignVCenter,"Orange = allocated · cyan = processed (proof may be pending) · click a block to select it · Reset zoom = all zones");
  setProperty("pgn_atlas_zoom_blocks",totalCells);setProperty("pgn_atlas_zoom_zones",visible.size());setProperty("pgn_atlas_zoom_zone",visible.front()["zone"].toInt());reset->setZoomed(true);return true;
 }
 void paintStatic(){QPainter p(&cached);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),HorizonDeviceVisuals::canvasBrush(rect()));hits.clear();setProperty("pgn_atlas_mixed_source_zones",0);if(paintZone(p))return;const auto grid=plot();if(rows.isEmpty()){p.setPen(QColor("#adc5c9"));p.drawText(grid,Qt::AlignCenter,"COMMUNITY SOURCE UNAVAILABLE · no FREE or COMPLETE inference");return;}const double rowh=grid.height()/std::max(1,int(owners.size()));p.setFont(HorizonDeviceVisuals::font(8,QFont::Normal,true));for(int i=0;i<owners.size();i++){p.setPen(QColor("#bce5e1"));p.drawText(QRectF(5,grid.top()+i*rowh,132,rowh),Qt::AlignRight|Qt::AlignVCenter,owners[i]);p.setPen(QPen(QColor(70,111,120,90),1));p.drawLine(QPointF(grid.left(),grid.top()+i*rowh),QPointF(grid.right(),grid.top()+i*rowh));}
  for(auto v:rows){auto r=v.toObject();if(r["status"].toString().startsWith("LOCAL_"))continue;const double a=r["axis_start"].toString().toDouble(),b=r["axis_end"].toString().toDouble();if(a>=hi||b<=lo)continue;const int lane=owners.indexOf(ownerName(r));if(lane<0)continue;const auto exactWidth=(std::min(b,hi)-std::max(a,lo))/(hi-lo)*grid.width();const QRectF rect=projectedRange(a,b,lane);
   const bool selected=!selectedId.isEmpty()&&r["id"].toString()==selectedId;
   if(exactWidth<4&&!selected)p.fillRect(rect,color(r["status"].toString()));
   else pgn::ui::drawRangeTile(p,rect,color(r["status"].toString()),r["_label"].toString(),{},std::nullopt,selected);r["source"]=community["source"];hits.append({rect,r});}
  // One enclosing zone frame for each local owner, then exact allocated /
  // processed subspans. A block is never drawn as if it were a whole zone.
  const auto own=community["researcher"].toString();const int ownLane=owners.indexOf(own);
  struct OwnRow{QJsonObject row;double start,end;bool axisValid;};QVector<OwnRow> ownRows;for(const auto& r:rows){const auto row=r.toObject();if(row["status"].toString().startsWith("LOCAL_")&&row["owner"]==own){bool startOk=false,endOk=false;const double start=row["axis_start"].toString().toDouble(&startOk),end=row["axis_end"].toString().toDouble(&endOk);ownRows.append({row,start,end,startOk&&endOk&&std::isfinite(start)&&std::isfinite(end)});}}int ownerOverlapChecks=0,exactOverlapChecks=0;
  if(ownLane>=0)for(const auto& value:zones){auto zone=value.toObject();const double a=zone["axis_start"].toString().toDouble(),b=zone["axis_end"].toString().toDouble();if(a>=hi||b<=lo)continue;
   QVector<QJsonObject> pieces;for(const auto& candidate:ownRows){++ownerOverlapChecks;
    // Broad screen-coordinate rejection only. Conservative slack retains
    // touching/rounding-ambiguous bounds for the authoritative decimal test.
    const double slack=std::max(1e-6,64*std::numeric_limits<double>::epsilon()*std::max({std::abs(a),std::abs(b),std::abs(candidate.start),std::abs(candidate.end)}));
    if(candidate.axisValid&&(candidate.start>b+slack||candidate.end<a-slack))continue;const auto& row=candidate.row;++exactOverlapChecks;
    if(pgn::booking::overlaps(zone["start"].toString(),zone["end"].toString(),row["start"].toString(),row["end"].toString()))pieces.append(row);}
   if(pieces.isEmpty())continue;const auto exactWidth=(std::min(b,hi)-std::max(a,lo))/(hi-lo)*grid.width();const QRectF box=projectedRange(a,b,ownLane);
   const bool allocated=std::any_of(pieces.cbegin(),pieces.cend(),[](const auto& row){return row["status"].toString()!="LOCAL_SOURCE_FINISHED";});
   const auto coverage=pgn::ui::zoneSourceCoverage(zone,pieces);
   const bool partialOpen=!allocated&&zone["status"].toString()=="DECLARED_OPEN"&&coverage.covered!="0"&&!coverage.complete;
   const bool compactMixed=partialOpen&&box.width()<4;
   if(partialOpen)setProperty("pgn_atlas_mixed_source_zones",property("pgn_atlas_mixed_source_zones").toInt()+1);
   if(compactMixed){
    // At a two-pixel overview width, horizontal block occupancy is invisible.
    // Keep identical zone geometry and use a vertical two-state indicator;
    // zoom retains the exact horizontal block positions. Never round a
    // nonempty Open remainder away, even if double projection reaches 1.0.
    // Reserve two display pixels for a nonempty Open remainder so fractional
    // device-pixel alignment cannot erase it. Exact counts stay in the tooltip.
    const auto processedHeight=std::clamp(box.height()*coverage.fraction,1.,box.height()-2.);
    p.fillRect(box,QColor("#69efc5"));p.fillRect(QRectF(box.left(),box.top(),box.width(),processedHeight),QColor("#46c8ee"));
   }else pgn::ui::drawRangeTile(p,box,partialOpen?QColor("#69efc5"):QColor("#78aab2"),zone["label"].toString(),{}, {},false);
   // At overview scale a partial block may be subpixel. Outline the owner
   // container without painting its unallocated blocks as reserved/processed.
   if(!compactMixed){p.setPen(QPen(QColor(allocated?"#ffad4a":partialOpen?"#69efc5":"#47c5e6"),1.8));p.setBrush(Qt::NoBrush);p.drawRoundedRect(box.adjusted(.9,.9,-.9,-.9),4,4);}
   const auto mixedLabel=partialOpen?QString(" · source processed %1%; remainder Open; independent proof pending%2").arg(coverage.fraction*100,0,'f',2).arg(compactMixed?" · compact vertical status indicator; zoom for exact block positions":""):QString{};
   auto frame=zone;frame["_zone_frame"]=true;frame["_label"]=own+" · "+zone["label"].toString()+" · outline identifies our work; only coloured block spans are allocated/processed"+mixedLabel;hits.append({box,frame});
   for(auto row:pieces)if(const auto span=pgn::ui::rangeCalendarInterval({zone["start"].toString(),zone["end"].toString()},row["start"].toString(),row["end"].toString())){if(!compactMixed)pgn::ui::drawRangeSpan(p,box,QColor(row["_device_color"].toString()),span->begin,span->end);row["_zone_span"]=true;row["_zone_start"]=zone["start"];row["_zone_end"]=zone["end"];row["_marker"]=exactWidth<12;row["_label"]=row["_label"].toString()+mixedLabel;hits.append({box,row});}
  }
  setProperty("pgn_atlas_owner_overlap_checks",ownerOverlapChecks);setProperty("pgn_atlas_owner_exact_checks",exactOverlapChecks);setProperty("pgn_atlas_owner_rows",ownRows.size());
  p.setPen(QColor("#8eb5b8"));for(int i=0;i<=5;i++){const double x=grid.left()+grid.width()*i/5;p.drawText(QRectF(x-55,grid.bottom()+7,110,20),Qt::AlignCenter,QString::number((lo+(hi-lo)*i/5)/100000.,'f',hi-lo<100?8:4));}p.drawText(QRectF(grid.left(),height()-42,grid.width(),18),Qt::AlignCenter,"NUMBER AXIS × 10²⁰ · wheel zoom / drag pan / click exact bounds");p.drawText(QRectF(7,height()-22,width()-14,18),Qt::AlignCenter,property("pgn_local_atlas").toBool()?"LOCAL JOBS ONLY · source finished is NOT independently certified coverage · no public lease":"cyan=declared complete (not proof) · amber=reserved · mint=declared open (not Core lease) · outside source=UNKNOWN");reset->setZoomed(zoomed);refreshHover();
 }
 void wheelEvent(QWheelEvent*e)override{
  if(!e->pixelDelta().y()&&!e->angleDelta().y()){e->ignore();return;}
  const auto grid=plot();zoomAnchor=std::clamp((e->position().x()-grid.left())/grid.width(),0.,1.);
  if(!motion.isActive()){targetLo=lo;targetHi=hi;}
  const double units=e->pixelDelta().y()?e->pixelDelta().y()*.8:e->angleDelta().y();
  const double old=targetHi-targetLo,span=std::clamp(old*std::exp(-units*.002972291),1e-7,domainHi-domainLo),pivot=targetLo+old*zoomAnchor;
  targetLo=pivot-span*zoomAnchor;targetHi=targetLo+span;
  if(targetLo<domainLo){targetLo=domainLo;targetHi=targetLo+span;}if(targetHi>domainHi){targetHi=domainHi;targetLo=targetHi-span;}
  zoomed=true;if(!motion.isActive()){motionClock.start();motion.start();}e->accept();
 }
 void mousePressEvent(QMouseEvent*e)override{if(e->button()!=Qt::LeftButton)return;stopMotion();pressed=e->position();pressedLo=lo;pressedHi=hi;sweepAnchor=zoneAt(pressed);sweeping=sweepAnchor>=0;dragging=sweeping||plot().contains(pressed);ctrlPress=e->modifiers().testFlag(Qt::ControlModifier);if(sweeping){sweepBase=ctrlPress?selectedZones:QSet<QString>{};subtractSweep=ctrlPress&&selectedZones.contains(zoneKey(zones[sweepAnchor].toObject()));sweepTo(sweepAnchor);}e->accept();}
 void mouseMoveEvent(QMouseEvent*e)override{if(dragging){if(sweeping){sweepTo(zoneAt(e->position(),false));return;}if((e->position()-pressed).manhattanLength()<4)return;const double delta=(e->position().x()-pressed.x())/plot().width()*(pressedHi-pressedLo);lo=pressedLo-delta;hi=pressedHi-delta;clamp();zoomed=true;update();return;}hoverPosition=e->position();hoverGlobal=e->globalPosition().toPoint();hoverValid=true;refreshHover();}
 void leaveEvent(QEvent*e)override{hoverValid=false;hoverText.clear();QToolTip::hideText();QWidget::leaveEvent(e);}
 void mouseReleaseEvent(QMouseEvent*e)override{if(e->button()!=Qt::LeftButton||!dragging)return;dragging=false;if(sweeping&&!ctrlPress&&(e->position()-pressed).manhattanLength()<4){auto inspected=zones[sweepAnchor].toObject();for(auto i=hits.crbegin();i!=hits.crend();++i)if(i->first.contains(e->position())&&i->second["_block"].toBool()){inspected=i->second;break;}if(inspect)inspect(inspected);}sweeping=false;e->accept();}
};



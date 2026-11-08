#include "ui/mining_ticket_progress.h"
#include "ui/gap_flow_motion.h"
#include "ui/position_formats.h"
#include "ui/ticket_flow_evidence.h"
#include "ui/report_visual.h"
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QImage>
#include <QHash>
#include <QTableWidget>
#include <QRegularExpression>
#include <cmath>

namespace pgn::ui {
namespace {
double ease(double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);}
QImage glow(const QColor& color){
 QImage image(40,40,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
 QPainter painter(&image);QRadialGradient gradient(QPointF(20,20),20);
 auto core=color;core.setAlpha(200);gradient.setColorAt(0,core);core.setAlpha(0);gradient.setColorAt(1,core);
 painter.fillRect(image.rect(),gradient);return image;
}
// Visual rank follows the existing merit classification; it never changes rewards.
double coinRadius(MeritTier tier){
 switch(tier){
 case MeritTier::Gold:return 4.5;
 case MeritTier::Silver:return 3.7;
 case MeritTier::Bronze:return 3.0;
 case MeritTier::NearMiss:return 2.4;
 default:return 1.6;
 }
}
void workMark(QPainter& painter,const QPointF& point,const QString& letter,const QColor& color,bool audited,double opacity=1.){
 painter.save();painter.setOpacity(opacity);painter.translate(point);
 if(audited){painter.setBrush(Qt::NoBrush);painter.setPen(QPen(QColor(149,255,202,185),1.2));painter.drawEllipse(QRectF(-12,-15,24,9));}
 painter.setBrush(QColor("#071b23"));painter.setPen(QPen(color,1.2));
 if(letter=="A")painter.drawEllipse(QPointF(),7,7);else painter.drawRoundedRect(QRectF(-7,-7,14,14),2,2);
  if(letter=="R"){painter.drawLine(QPointF(-4,-2),QPointF(4,-2));painter.drawLine(QPointF(-4,2),QPointF(3,2));painter.drawLine(QPointF(-4,5),QPointF(1,5));}
  else{QFont font("Consolas");font.setPixelSize(10);font.setBold(true);painter.setFont(font);painter.setPen(color);painter.drawText(QRectF(-7,-7,14,14),Qt::AlignCenter,letter);}painter.restore();
}
struct Port {QString id;QRectF card;QPointF point;};
QColor faded(QColor color,int alpha){color.setAlpha(std::clamp(alpha,0,255));return color;}
QColor blend(const QColor& a,const QColor& b,double amount){
 amount=std::clamp(amount,0.,1.);return QColor::fromRgbF(std::lerp(a.redF(),b.redF(),amount),std::lerp(a.greenF(),b.greenF(),amount),std::lerp(a.blueF(),b.blueF(),amount));
}
}
class GapFlowOverlay final : public QWidget {
public:
 explicit GapFlowOverlay(MiningTicketProgress* rail) : QWidget(rail->parentWidget()), rail_(rail) {
  setObjectName("pgn-card-gap-flow");setStyleSheet("background:transparent;border:0;");setAutoFillBackground(false);
  setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_NoSystemBackground);setAttribute(Qt::WA_TranslucentBackground);
  for (const auto& color : {QColor("#bde9e4"),QColor("#62cfff"),QColor("#69cf8b"),QColor("#ff6474"),QColor("#cd7f32"),QColor("#c0c0c0"),QColor("#ffd700")})
   glows_.insert(color.rgb(),glow(color));
  show();raise();
 }
 void synchronize() {
  if (!rail_ || !rail_->canvas_ || !parentWidget()) return;
  ports_.clear();int top=rail_->y();
  const QRectF viewport(rail_->canvas_->mapTo(parentWidget(),QPoint()),rail_->canvas_->size());
  for (const auto& value : rail_->canvas_->property("pgn_card_layout").toJsonArray()) {
   const auto card=value.toObject();const auto rect=card.value("rect").toArray();
   if (card.value("visible")!=true || rect.size()!=4) continue;
   QRectF area(rect[0].toDouble(),rect[1].toDouble(),rect[2].toDouble(),rect[3].toDouble());
   area.moveTopLeft(rail_->canvas_->mapTo(parentWidget(),area.topLeft().toPoint()));
   const auto visible=area.intersected(viewport);if (visible.width()<8) continue;
   ports_.append({card.value("id").toString(),area,QPointF(visible.center().x(),area.bottom()+2)});
   top=std::min(top,int(std::floor(area.bottom()))+1);
  }
  const QRect area(0,top,parentWidget()->width(),std::max(1,rail_->y()+rail_->height()-top));
  if (geometry()!=area) {setGeometry(area);raise();}
 }
protected:
 void paintEvent(QPaintEvent*) override {
  if (!rail_) return;
  QElapsedTimer cost;cost.start();QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
  const QPointF offset=rail_->mapTo(parentWidget(),QPoint(0,0))-pos();
  const auto box=rail_->bar().translated(offset);const auto center=box.center();
  QRegion safe(rect());
  for (const auto& port : ports_) safe-=QRegion(port.card.translated(-QPointF(pos())).adjusted(-2,-2,2,2).toAlignedRect());
  safe-=QRegion(QRect(0,int(offset.y()+88),width(),rail_->height()-88));painter.setClipRegion(safe);
  const auto now=rail_->clock_.elapsed();const double seconds=now/1000.;
  const auto source=[&](const QString& id,double seed) {
   for (const auto& port : ports_) if (port.id.compare(id,Qt::CaseInsensitive)==0)
    return port.point-QPointF(pos())+QPointF((seed-.5)*std::min(180.,port.card.width()*.55),0);
   return QPointF(center.x(),std::min(box.top()-18.,offset.y()+3.));
  };
  const auto particle=[&](QPointF at,QColor color,double opacity,double radius) {
   painter.save();painter.setOpacity(std::clamp(opacity,0.,1.));
   painter.drawImage(QRectF(at.x()-radius*4,at.y()-radius*4,radius*8,radius*8),glows_.value(color.rgb()));
   painter.setPen(Qt::NoPen);painter.setBrush(color);painter.drawEllipse(at,radius,radius);painter.restore();
  };
  const QColor incoming=rail_->flow_.audit()?QColor("#62cfff"):QColor("#bde9e4");
  if (rail_->flow_.moving()) {
   const int population=3+int(rail_->flow_.activity()*13);
   for (int i=0;i<population;++i) {
    const double rhythm=flowSeed(i+37),travel=seconds*(.18+rail_->flow_.activity()*.27+rhythm*.19)+rhythm*13;
    const auto cycle=std::uint32_t(std::floor(travel));const double u=travel-std::floor(travel);
    const double seed=flowSeed(std::uint32_t(i)*65537+cycle*7919+17);
    const auto from=source(rail_->flow_.producer(),seed);
    const auto at=livingFlowPoint(from,center,u,seed,seconds,30.);
    const auto tail=livingFlowPoint(from,center,std::max(0.,u-.035),seed,seconds,30.);
    painter.setPen(QPen(faded(incoming,40),.7));painter.drawLine(tail,at);
    particle(at,incoming,.16+.38*std::sin(u*3.14159265),.9+seed*.8);
    if(i==0&&rail_->flow_.audit())workMark(painter,at,"A",incoming,false,.65+.35*std::sin(u*3.14159265));
   }
  }
  QVector<const GapFlowEvent*> labels;
  for (const auto& event : rail_->flow_.events()) if (event.tier!=MeritTier::Ordinary && now-event.born>3000) labels.append(&event);
  std::sort(labels.begin(),labels.end(),[this](const auto* a,const auto* b) {
   if (rail_->flow_.record(*a)!=rail_->flow_.record(*b)) return rail_->flow_.record(*a);
   if (a->tier!=b->tier) return int(a->tier)>int(b->tier);
   return a->born>b->born;
  });
  if (labels.size()>3) labels.resize(3);
  const double reach=std::max(32.,box.width()*.34);
  const QPointF receiver(center.x()+reach,center.y());
  const QPointF blackHole(center.x()-reach,center.y()),chainPort(box.right()-25,center.y());
  rail_->setProperty("pgn_ticket_audit_markers",rail_->flow_.moving()&&rail_->flow_.audit()?1:0);
  // A whole range has a distinct packet. Its local completion, native report
  // inclusion and audited verdict are separate states; only the last gets a halo.
  QJsonArray workTokens;QHash<QString,QPointF> reportPositions;int visibleRanges=0;
  for(auto at=rail_->landings_.crbegin();at!=rail_->landings_.crend()&&visibleRanges<6;++at){
   const auto& entry=*at;if(!entry.complete&&!entry.confirmed)continue;
   if(entry.audited&&now-entry.approvedAt>3200)continue;
   const int slot=visibleRanges++;
   const QPointF holding(chainPort.x()-12-slot*17,box.bottom()-3);
   const double arrival=std::clamp((now-entry.born)/1800.,0.,1.);
   QPointF position=livingFlowPoint(center,holding,arrival,flowSeed(qHash(entry.ticket)),seconds,13.);
   double opacity=1.;
   if(entry.audited){const double entering=std::clamp((now-entry.approvedAt-700)/2300.,0.,1.);position=livingFlowPoint(holding,chainPort,entering,flowSeed(qHash(entry.ticket)),seconds,4.);if(entering>.7)opacity=(1-entering)/.3;}
   position.setY(std::min(position.y(),box.bottom()-3));
    painter.save();painter.setOpacity(opacity);drawReportVisual(painter,position,entry.request,entry.audited?QColor("#95ffca"):entry.confirmed?QColor("#69c9e4"):QColor("#b4cec8"),seconds,1.,entry.audited);painter.restore();
   reportPositions.insert(entry.ticket,position);
   workTokens.append(QJsonObject{{"ticket_id",entry.ticket},{"request_id",entry.request},{"kind","range"},{"report_included",entry.confirmed},{"audited",entry.audited},{"x",position.x()},{"y",position.y()}});
  }
  rail_->setProperty("pgn_ticket_work_tokens",workTokens);
  const bool alive=rail_->flow_.moving()||!rail_->flow_.events().isEmpty()||!rail_->flow_.collected().isEmpty()||!rail_->flow_.departing().isEmpty();const double beat=alive?seconds:0;
  // The right-hand collector is a destination, not a declaration that an audit
  // or a monetary reward has passed. The underlying Best table retains results.
  painter.setBrush(Qt::NoBrush);
  for (int i=0;i<3;++i) {
   const double angle=beat*(i%2?-.21:.16)+i*1.7;
   painter.save();painter.translate(receiver);painter.rotate(angle*180/3.14159265);
   painter.setPen(QPen(QColor(77,175,170,42),.8));painter.drawEllipse(QPointF(),29+i*7,7+i*2);painter.restore();
  }
  const auto collectionPointAt=[&](const GapFlowEvent& gap,double time) {
   const double angle=gap.seed*6.2831853+time*(.055+gap.seed*.06);
   const double radius=10+flowSeed(std::uint32_t(gap.seed*65535)+13)*std::min(100.,box.width()*.12);
   return receiver+QPointF(std::cos(angle)*radius,std::sin(angle)*(6+gap.seed*10));
  };
  const auto collectionPoint=[&](const GapFlowEvent& gap){return collectionPointAt(gap,seconds);};
  rail_->hits_.clear();
  for(const auto& node:rail_->flow_.collected()){
   if(now<node.arrives)continue;
   const auto& gap=node.gap;auto at=collectionPoint(gap);
   const bool hover=rail_->hovered_==gap.pair;
   if(hover){const auto mouse=rail_->pointer_+offset;const auto delta=mouse-at;const double length=std::hypot(delta.x(),delta.y());if(length>.1)at+=delta*(std::min(4.,length*.15)/length);}
   const QColor color=meritColor(gap.tier,QColor("#69cf8b"));
   const bool notable=gap.tier!=MeritTier::Ordinary;
   particle(at,color,notable?.9:.45,coinRadius(gap.tier));
   rail_->hits_.append({gap.pair,at-offset});
   if(rail_->flow_.record(gap)){painter.setPen(color);painter.drawText(QRectF(at.x()-5,at.y()-16,12,12),Qt::AlignCenter,"★");}
   if(hover||rail_->pulsed_==gap.pair&&now-rail_->pulsedAt_<850){
    const double pulse=hover?.35:std::clamp((now-rail_->pulsedAt_)/850.,0.,1.);
    painter.setBrush(Qt::NoBrush);painter.setPen(QPen(faded(QColor("#effffb"),int(220*(1-pulse))),1));painter.drawEllipse(at,6+pulse*10,6+pulse*10);
    QFont font("Consolas");font.setPixelSize(11);painter.setFont(font);painter.setPen(color);painter.drawText(QRectF(at.x()-38,at.y()-25,76,14),Qt::AlignCenter,QString::number(gap.gap));
   }
  }
  QJsonArray hitRows;for(const auto& hit:rail_->hits_)hitRows.append(QJsonObject{{"pair",hit.pair},{"x",hit.point.x()},{"y",hit.point.y()}});rail_->setProperty("pgn_ticket_collection_hits",hitRows);
  for(const auto& node:rail_->flow_.departing()){
   const double t=std::clamp((now-node.leaves)/double(node.toSink?1700:node.ticketTransition?2800:1700),0.,1.);
   const QPointF holding=reportPositions.value(node.gap.ticket(),QPointF(chainPort.x()-12,box.bottom()-3));
   const QPointF exit=node.toSink?blackHole+QPointF(std::cos(node.gap.seed*6.28)*24,std::sin(node.gap.seed*6.28)*10):node.ticketTransition?holding:QPointF(receiver.x()+100,center.y()-20);
   const double leaveSeconds=node.leaves/1000.;QPointF origin=collectionPointAt(node.gap,leaveSeconds);
   if(node.flight){const auto& event=*node.flight;const double age=node.flightAge;
    if(age<1.2)origin=livingFlowPoint(source(event.producer,event.seed),center,age/1.2,event.seed,leaveSeconds,28.);
    else if(age<2.2){const double phase=age-1.2,angle=phase*(6+event.seed*5)+event.seed*13;origin=center+QPointF(std::cos(angle)*(10-7*phase),std::sin(angle)*(6-4*phase));}
    else{const bool notable=event.tier!=MeritTier::Ordinary||node.flightFromBest;const QPointF destination=node.flightFromBest?collectionPointAt(node.gap,leaveSeconds):notable?receiver:blackHole+QPointF(std::cos(event.seed*6.28)*24,std::sin(event.seed*6.28)*10);origin=livingFlowPoint(center,destination,(age-2.2)/1.5,event.seed,leaveSeconds,17.);}
   }
   auto at=livingFlowPoint(origin,exit,t,node.gap.seed,seconds,12.);double opacity=node.ticketTransition?1.:std::min(1.,(1-t)*2);
   if(node.toSink&&t>=1.){const double swallowed=std::clamp((now-node.leaves-1700)/500.,0.,1.);const double angle=node.gap.seed*6.28+swallowed*9;at=blackHole+QPointF(std::cos(angle)*24,std::sin(angle)*10)*std::sqrt(1-swallowed);}
   else if(node.ticketTransition&&t>=1.){
    // The report keeps the discovery signature and the actual pending state.
    // Folding a point into that envelope does not assert blockchain inclusion.
    const double folded=std::clamp((now-node.leaves-2800)/1400.,0.,1.);
    opacity=1-ease(folded);
   }
   particle(at,meritColor(node.gap.tier,QColor("#69cf8b")),opacity,coinRadius(node.gap.tier)*(1-t*.1)*(node.ticketTransition&&!node.toSink?opacity:1.));
  }
  // A waiting packet sits outside the links. Only a current-branch Core report
  // proof permits the final ingress; this says nothing about audit acceptance.
  painter.setBrush(QColor("#06161e"));painter.setPen(QPen(QColor("#34545c"),1));
  for(int i=0;i<3;++i){const QPointF p=chainPort+QPointF((i-1)*14,0);QPainterPath link;link.moveTo(p+QPointF(0,-7));link.lineTo(p+QPointF(6,-3));link.lineTo(p+QPointF(6,4));link.lineTo(p+QPointF(0,8));link.lineTo(p+QPointF(-6,4));link.lineTo(p+QPointF(-6,-3));link.closeSubpath();painter.drawPath(link);painter.drawLine(p+QPointF(-6,-3),p+QPointF(0,1));painter.drawLine(p+QPointF(0,1),p+QPointF(6,-3));painter.drawLine(p+QPointF(0,1),p+QPointF(0,8));}
  double reaction=0;QColor resonance=alive?(rail_->flow_.audit()?QColor("#62cfff"):QColor("#69cf8b")):QColor("#426b70");
  const GapFlowEvent* measured=nullptr;
  for (const auto& event : rail_->flow_.events()) {
   const double age=(now-event.born)/1000.;if (age<0) continue;
   const auto* retained=rail_->flow_.collected(event);const auto color=meritColor(event.tier,retained?QColor("#69cf8b"):incoming);const bool notable=event.tier!=MeritTier::Ordinary||retained;
   const double radius=1.9+.4*std::sin(seconds*2.7+event.seed*17);
   if (age<1.2) {
    particle(livingFlowPoint(source(event.producer,event.seed),center,age/1.2,event.seed,seconds,28.),incoming,.9,radius);
   } else if (age<2.2) {
    const double t=age-1.2,pulse=std::sin(t*3.14159265);
    if (pulse>reaction) {reaction=pulse;resonance=blend(incoming,color,ease(t));measured=&event;}
    const double angle=t*(6+event.seed*5)+event.seed*13;
    particle(center+QPointF(std::cos(angle)*(10-7*t),std::sin(angle)*(6-4*t)),blend(incoming,color,ease(t)),1.,std::lerp(2.5,coinRadius(event.tier),ease(t)));
   } else {
    const double transit=(age-2.2)/1.5;
    const int slot=labels.indexOf(&event);
    const QPointF destination=retained?collectionPoint(retained->gap):notable?receiver:blackHole+QPointF(std::cos(event.seed*6.28)*24,std::sin(event.seed*6.28)*10);
    QPointF at=livingFlowPoint(center,destination,transit,event.seed,seconds,17.);
    double fade=1.;
    if (transit>=1. && retained) continue;
    if (transit>=1.) {
     const double settling=age-3.7;
     if (notable) {
      const double lane=slot>=0?slot:std::fmod(event.seed*19,3.);
      const double angle=event.seed*6.28+settling*(.7+event.seed*.8);
      const double orbit=(13+lane*17)*std::clamp((event.lifetime()/1000.-age)/1.7,0.,1.);
      at=receiver+QPointF(std::cos(angle)*orbit,std::sin(angle)*(4+lane*3));
      fade=std::clamp((event.lifetime()/1000.-age)/.8,0.,1.);
     } else {
      const double absorption=std::clamp(settling/.5,0.,1.);const double angle=event.seed*6.28+absorption*9.;
      at=blackHole+QPointF(std::cos(angle)*24,std::sin(angle)*10)*std::sqrt(1-absorption);fade=1.;
     }
    }
    if (transit<.65) {
     const double impulse=std::clamp(transit/.65,0.,1.);painter.setBrush(Qt::NoBrush);
     painter.setPen(QPen(faded(color,int((1-impulse)*65)),.8));painter.drawEllipse(center,38+impulse*42,8+impulse*8);
    }
    particle(at,color,fade,coinRadius(event.tier));
    if (notable && transit>=.7 && slot>=0) {
     painter.setOpacity(fade);painter.setPen(color);QFont font("Consolas");font.setPixelSize(10);painter.setFont(font);
     // Stable label lanes remain legible while the entities themselves orbit.
     const double labelX=receiver.x()+(slot-1)*76;
     painter.drawText(QRectF(labelX-36,box.bottom()-6,72,13),Qt::AlignCenter,(rail_->flow_.record(event)?QString("★ "):QString{})+QString::number(event.gap));
     painter.setOpacity(1.);
    }
   }
  }
  // The ordinary stream winds into a quiet visual sink. No data operation is
  // attached to this black hole: reports and counts remain source-owned.
  painter.save();painter.translate(blackHole);painter.rotate(-14);painter.setBrush(Qt::NoBrush);
  QConicalGradient accretion(QPointF(),beat*12);accretion.setColorAt(0,QColor(78,163,183,25));accretion.setColorAt(.35,QColor(153,219,223,190));accretion.setColorAt(.65,QColor(59,116,153,80));accretion.setColorAt(1,QColor(78,163,183,25));
  painter.setPen(QPen(QBrush(accretion),1.7));painter.drawEllipse(QPointF(),27,9);painter.setPen(QPen(QColor(99,168,181,80),.8));painter.drawEllipse(QPointF(),13,13);
  painter.setBrush(QColor("#000407"));painter.setPen(QPen(QColor(98,182,199,145),1.));painter.drawEllipse(QPointF(),8,8);painter.restore();
  // Prime anchors deform a lens as the candidate is measured. Interference is
  // decorative; exact progress, merit, record status and audits stay separate.
  const double half=52+reaction*11;
  QPainterPath membrane;membrane.moveTo(center-QPointF(half,0));
  for (int side : {-1,1}) for (int i=0;i<=40;++i) {
   const double u=side<0?double(i)/40:1.-double(i)/40;
   const double envelope=std::sin(u*3.141592653589793);
   const double ripple=(std::sin(u*15+beat*1.8)+flowNoise(.27+u,beat*.6))*(.8+reaction*2)*envelope;
   membrane.lineTo(center+QPointF(-half+2*half*u,side*(17+reaction*3)*std::pow(envelope,.65)+ripple));
  }
  membrane.closeSubpath();painter.setPen(QPen(faded(resonance,30),6));painter.setBrush(Qt::NoBrush);painter.drawPath(membrane);
  QLinearGradient skin(center-QPointF(half,0),center+QPointF(half,0));
  skin.setColorAt(0,faded(resonance,25));skin.setColorAt(.5,QColor(3,22,29,245));skin.setColorAt(1,faded(resonance,25));
  painter.setBrush(skin);painter.setPen(QPen(faded(resonance,170),1));painter.drawPath(membrane);
  painter.save();painter.setClipPath(membrane,Qt::IntersectClip);painter.setBrush(Qt::NoBrush);
  for (int i=0;i<3;++i) {const double radius=9+std::fmod(beat*8+i*15,46.);painter.setPen(QPen(faded(resonance,int(48*(1-radius/60))),.7));painter.drawEllipse(center,radius,radius*.5);}
  painter.restore();
  const double separation=24+reaction*9;painter.setPen(QPen(faded(resonance,135),1));painter.drawLine(center-QPointF(separation,0),center+QPointF(separation,0));
  for (int side : {-1,1}) {
   const auto anchor=center+QPointF(side*separation,0);painter.setBrush(QColor("#071c24"));painter.setPen(QPen(resonance,1.2));painter.drawEllipse(anchor,4,4);
   painter.setPen(QPen(faded(resonance,60),.7));painter.setBrush(Qt::NoBrush);painter.drawEllipse(anchor,7+reaction*2,7+reaction*2);
  }
  QFont font("Consolas");font.setPixelSize(10);painter.setFont(font);painter.setPen(resonance);
  painter.drawText(QRectF(center.x()-separation-8,center.y()-20,16,12),Qt::AlignCenter,"p");
  painter.drawText(QRectF(center.x()+separation-8,center.y()-20,16,12),Qt::AlignCenter,"q");
  painter.drawText(QRectF(center.x()-35,center.y()+4,70,12),Qt::AlignCenter,measured?QString("Δ %1").arg(measured->gap):rail_->flow_.audit()?QString("AUDIT"):QString("Δ / ln p"));
  const auto elapsed=cost.nsecsElapsed();rail_->setProperty("pgn_ticket_animation_paint_ns",elapsed);
  rail_->setProperty("pgn_ticket_animation_paint_max_ns",std::max(rail_->property("pgn_ticket_animation_paint_max_ns").toLongLong(),elapsed));
 }
private:
 QPointer<MiningTicketProgress> rail_;QVector<Port> ports_;QHash<QRgb,QImage> glows_;
};
MiningTicketProgress::MiningTicketProgress(QWidget* parent):QWidget(parent){
 setObjectName("pgn-ticket-progress-rail");setFixedHeight(148);setMouseTracking(true);clock_.start();timer_.setInterval(33);
 connect(&timer_,&QTimer::timeout,this,[this]{if(!isVisible())return;const bool wasMoving=flow_.moving();flow_.expire(clock_.elapsed());pollLandings();if(results_)flow_.setVisibleBest(results_->property("pgn_visible_best").toJsonObject(),clock_.elapsed());setProperty("pgn_ticket_collection_size",flow_.collected().size());setProperty("pgn_ticket_collection_departing",flow_.departing().size());
  if(wasMoving!=flow_.moving()||flow_.moving()||workMarksActive(clock_.elapsed())||!flow_.events().isEmpty()||!flow_.collected().isEmpty()||!flow_.departing().isEmpty()||frame_.value("phase")=="Starting"||frame_.value("phase")=="Stopping"){
   update();ensureOverlay();if(overlay_){overlay_->synchronize();overlay_->update();}
  }});timer_.start();
}
MiningTicketProgress::~MiningTicketProgress(){delete overlay_.data();}
QRectF MiningTicketProgress::bar()const{return {16,28,double(std::max(1,width()-32)),50};}
void MiningTicketProgress::ensureOverlay(){if(!overlay_&&canvas_&&parentWidget())overlay_=new GapFlowOverlay(this);}
void MiningTicketProgress::bindSourceCanvas(QWidget* canvas){canvas_=canvas;if(canvas_)canvas_->setProperty("pgn_card_geometry_enabled",true);ensureOverlay();}
void MiningTicketProgress::bindResultsView(QWidget* view){results_=view;}
void MiningTicketProgress::setRecordEligibility(const QJsonArray& pairs){flow_.setEligibility(pairs);if(overlay_)overlay_->update();}
void MiningTicketProgress::present(const QJsonObject& frame){
 const auto remember=[this](const QJsonObject& packet){
  if(packet.value("work_role")=="audit")return;
  const auto ticket=packet.value("ticket_id").toString(),request=packet.value("request_id").toString();
  if(ticket.isEmpty()||!QRegularExpression("^[0-9a-f]{64}$").match(request).hasMatch())return;
   QVector<QColor> signature;for(const auto& node:flow_.collected())signature.append(meritColor(node.gap.tier));rememberReportVisual(request,std::move(signature));
  for(auto& entry:landings_)if(entry.ticket==ticket&&entry.request==request){if(!entry.complete&&packet.value("phase")=="Report ready"){entry.complete=true;entry.born=clock_.elapsed();}return;}
  Landing entry;entry.ticket=ticket;entry.request=request;entry.complete=packet.value("phase")=="Report ready";entry.born=clock_.elapsed();landings_.append(entry);
  while(landings_.size()>32)landings_.removeFirst();
 };
 if(frame_.value("ticket_id")!=frame.value("ticket_id"))remember(frame_);
 if(frame.value("phase")=="Report ready")remember(frame);
 frame_=frame;flow_.observe(frame,canvas_?canvas_->property("pgn_managed_source_device").toString():QString{},clock_.elapsed());
 setToolTip({});
 setProperty("pgn_ticket_flow_received",QVariant::fromValue(flow_.received()));setProperty("pgn_ticket_flow_coalesced",QVariant::fromValue(flow_.coalesced()));
 ensureOverlay();update();if(overlay_){overlay_->synchronize();overlay_->update();}
}
void MiningTicketProgress::paintEvent(QPaintEvent*){
 QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
 const auto phase=frame_.value("phase").toString("Waiting for a ticket");const bool active=phase=="Searching",transition=phase=="Starting"||phase=="Stopping";
 const auto role=frame_.value("work_role").toString();
 const bool audit=role=="audit"||(role.isEmpty()&&frame_.value("audit_service").toObject().value("active_role")=="audit");
 QString displayPhase=phase;
 if(audit){
  if(active)displayPhase="Auditing";
  else if(phase=="Starting")displayPhase="Preparing audit";
  else if(phase=="Report ready")displayPhase="Audit computed";
  else if(phase=="Paused")displayPhase="Audit paused";
 }
 const auto accent=transition?QColor("#ffbd69"):QColor("#69efc5");
 const double total=frame_.value("positions_total").toString().toDouble(),done=frame_.value("positions").toString().toDouble();
 const auto fraction=total>0?std::optional<double>(std::clamp(done/total,0.,1.)):std::nullopt;
 const auto field=bar();const double seconds=flow_.moving()?clock_.elapsed()/1000.:0.;
 double pressure=0.;
 for(const auto& event:flow_.events()){
  const double age=(clock_.elapsed()-event.born)/1000.;
  if(age>=1.2&&age<=2.2)pressure=std::max(pressure,std::sin((age-1.2)*3.14159265));
 }
 const auto contour=[&](int side,double u){
  const double x=std::lerp(field.left(),field.right(),u);
  const double center=std::exp(-std::pow((u-.5)/.09,2.));
  const double collector=std::exp(-std::pow((u-.85)/.12,2.));
  const double envelope=std::pow(std::sin(u*3.14159265),.55);
  const double breath=(std::sin(u*16+seconds*.9)+std::sin(u*29-seconds*.7))*(1.+pressure*1.5);
  return QPointF(x,field.center().y()+side*(5+center*(11+pressure*3)+collector*8)*envelope+breath*envelope);
 };
 QPainterPath silhouette;
 for(int side:{-1,1})for(int i=0;i<=128;++i){const double u=side<0?i/128.:1.-i/128.;const auto point=contour(side,u);if(side==-1&&i==0)silhouette.moveTo(point);else silhouette.lineTo(point);}
 silhouette.closeSubpath();
 QLinearGradient mist(field.left(),field.center().y(),field.right(),field.center().y());
 mist.setColorAt(0,QColor(5,25,29,25));mist.setColorAt(.48,QColor(7,53,58,140));mist.setColorAt(.85,QColor(9,47,51,115));mist.setColorAt(1,QColor(5,25,29,25));
 painter.setBrush(Qt::NoBrush);painter.setPen(QPen(faded(accent,12),8));painter.drawPath(silhouette);
 painter.setBrush(mist);painter.setPen(QPen(faded(accent,90),.9));painter.drawPath(silhouette);
 // The ticket's exact fraction has its own quiet track. The deforming field
 // above is event presentation and can never advance this measurement.
 const double trackY=84.;const QPointF trackStart(field.left(),trackY),trackEnd(field.right(),trackY);
 painter.setPen(QPen(QColor("#17383e"),1));painter.drawLine(trackStart,trackEnd);
 if(fraction){const QPointF measured(std::lerp(trackStart.x(),trackEnd.x(),*fraction),trackY);
  painter.setPen(QPen(faded(accent,70),4));painter.drawLine(trackStart,measured);
  painter.setPen(QPen(accent,1.2));painter.drawLine(trackStart,measured);
  painter.setPen(Qt::NoPen);painter.setBrush(accent);painter.drawEllipse(measured,2.,2.);
 }
 QFont titleFont("Consolas");titleFont.setPixelSize(15);titleFont.setBold(true);painter.setFont(titleFont);
 const QString title=frame_.contains("bits")?frame_.value("title").toString(audit?"Network audit":"Network ticket")+QString(" · %1 bits").arg(frame_.value("bits").toInt()):audit?QString("Network audit"):QString("Network work · awaiting assignment");
 const double rate=frame_.value("eta_rate").toDouble();const auto eta=active&&rate>0&&std::isfinite(rate)&&total>0?std::optional<double>(std::max(0.,total-done)/rate):std::nullopt;
 const QString status=displayPhase+(fraction?QString(" · ≈ %1%").arg(100* *fraction,0,'f',2):QString{});
 const int rightWidth=std::min(360,std::max(180,width()*35/100)),leftWidth=std::max(1,width()-rightWidth-56);
 painter.setPen(QColor("#dffff5"));painter.drawText(QRectF(18,98,leftWidth,21),Qt::AlignLeft|Qt::AlignVCenter,painter.fontMetrics().elidedText(title,Qt::ElideRight,leftWidth));
 painter.setPen(accent);painter.drawText(QRectF(width()-rightWidth-18,98,rightWidth,21),Qt::AlignRight|Qt::AlignVCenter,painter.fontMetrics().elidedText(status,Qt::ElideRight,rightWidth));
 QFont detailFont("Consolas");detailFont.setPixelSize(width()<800?11:13);painter.setFont(detailFont);painter.setPen(QColor("#b8ded8"));
 const QString positions=frame_.contains("positions_total")?(audit?QString("Complete audit · "):QString{})+positionFormats(frame_.value("positions_total").toString()).inlineText()+" positions":QString{};
 painter.drawText(QRectF(18,123,std::max(1,width()-200),19),Qt::AlignLeft|Qt::AlignVCenter,painter.fontMetrics().elidedText(positions,Qt::ElideRight,std::max(1,width()-200)));
 QString remaining;
 if(eta&&std::isfinite(*eta)&&*eta<365.*86400){const auto value=qint64(std::ceil(*eta));remaining=QString("ETA ≈ %1:%2:%3").arg(value/3600,2,10,QChar('0')).arg(value/60%60,2,10,QChar('0')).arg(value%60,2,10,QChar('0'));}
 titleFont.setPixelSize(14);painter.setFont(titleFont);painter.setPen(QColor("#effffb"));painter.drawText(QRectF(width()-182,122,164,21),Qt::AlignRight|Qt::AlignVCenter,remaining);
 setProperty("pgn_ticket_eta_seconds",eta?QVariant(*eta):QVariant{});setProperty("pgn_ticket_progress_fraction",fraction?QVariant(*fraction):QVariant{});
 setProperty("pgn_ticket_work_presentation",QJsonObject{{"role",audit?"audit":role},{"phase",displayPhase},{"title",title},{"status",status},{"quantity",positions},{"remaining",remaining}});
 setProperty("pgn_ticket_animation_active",flow_.moving());setProperty("pgn_ticket_animation_phase",flow_.moving()?QVariant(std::fmod(clock_.elapsed()/1000.,1.)):QVariant{});
 if(overlay_)overlay_->synchronize();
}
void MiningTicketProgress::bindTicketLookup(TicketLookup lookup){
 lookup_=std::move(lookup);++lookupGeneration_;++activeLookup_;lookupBusy_=false;nextLookupAt_=0;
 for(auto& entry:landings_){entry.checking=false;entry.nextCheck=0;entry.confirmed=entry.audited=false;entry.approvedAt=-1;flow_.revokeReport(entry.ticket);}
 setProperty("pgn_ticket_landing_confirmed",false);setProperty("pgn_ticket_landing_audited",false);
}
bool MiningTicketProgress::workMarksActive(qint64 now)const{
 for(const auto& entry:landings_)if((entry.complete&&now-entry.born<1800)||(entry.audited&&now-entry.approvedAt<3200))return true;
 return false;
}
void MiningTicketProgress::pollLandings(){
 if(!lookup_||lookupBusy_)return;
 const auto now=clock_.elapsed();
 if(now<nextLookupAt_)return;
 auto next=std::min_element(landings_.begin(),landings_.end(),[](const auto& a,const auto& b){return a.nextCheck<b.nextCheck;});
 if(next==landings_.end()||next->nextCheck>now)return;
 {
  auto& entry=*next;
  entry.checking=true;entry.nextCheck=now+(entry.audited?30000:5000);nextLookupAt_=now+1000;
  lookupBusy_=true;
  const auto ticket=entry.ticket,request=entry.request;const auto generation=lookupGeneration_,query=++entry.query,lookupId=++activeLookup_;QPointer<MiningTicketProgress> weak(this);
  lookup_(request,[weak,ticket,request,generation,query,lookupId](QJsonObject state,QString error){
   if(!weak||weak->lookupGeneration_!=generation||!weak->lookupBusy_||weak->activeLookup_!=lookupId)return;
   weak->lookupBusy_=false;
   auto at=std::find_if(weak->landings_.begin(),weak->landings_.end(),[&](const auto& item){return item.ticket==ticket&&item.request==request&&item.checking&&item.query==query;});
   if(at==weak->landings_.end())return;
   const auto evidence=ticketFlowEvidence(request,ticket,state,error);
   at->checking=false;at->confirmed=evidence.included;at->block=evidence.block;
   if(evidence.included&&!at->complete){at->complete=true;at->born=weak->clock_.elapsed();}
   if(evidence.audited&&!at->audited)at->approvedAt=weak->clock_.elapsed();
   if(!evidence.audited)at->approvedAt=-1;
   at->audited=evidence.audited;
   if(evidence.included)weak->flow_.confirmReport(ticket,weak->clock_.elapsed());else weak->flow_.revokeReport(ticket);
   QJsonArray deliveries;for(const auto& item:weak->landings_)deliveries.append(QJsonObject{{"ticket_id",item.ticket},{"request_id",item.request},{"confirmed",item.confirmed},{"audited",item.audited},{"block",item.block}});weak->setProperty("pgn_ticket_deliveries",deliveries);
   weak->setProperty("pgn_ticket_landing_confirmed",evidence.included);weak->setProperty("pgn_ticket_landing_audited",evidence.audited);weak->setProperty("pgn_ticket_landing_error",error.left(160));
   weak->update();if(weak->overlay_)weak->overlay_->update();
  });
 }
}
void MiningTicketProgress::mouseMoveEvent(QMouseEvent* event){
 pointer_=event->position();QString closest;double distance=12.;
 for(const auto& hit:hits_){const double d=std::hypot(pointer_.x()-hit.point.x(),pointer_.y()-hit.point.y());if(d<distance){distance=d;closest=hit.pair;}}
 hovered_=closest;setCursor(hovered_.isEmpty()?Qt::ArrowCursor:Qt::PointingHandCursor);if(overlay_)overlay_->update();
}
void MiningTicketProgress::leaveEvent(QEvent* event){hovered_.clear();unsetCursor();if(overlay_)overlay_->update();QWidget::leaveEvent(event);}
void MiningTicketProgress::mousePressEvent(QMouseEvent* event){
 if(event->button()!=Qt::LeftButton){QWidget::mousePressEvent(event);return;}
 mouseMoveEvent(event);if(hovered_.isEmpty()||!results_)return;
 if(auto* table=results_->findChild<QTableWidget*>("pgn-results-best"))for(int row=0;row<table->rowCount();++row){
  const auto* item=table->item(row,0);if(!item||item->data(Qt::UserRole).toString()!=hovered_)continue;
  results_->setProperty("pgn_best_selected_pair",hovered_);table->setCurrentCell(row,0);table->selectRow(row);pulsed_=hovered_;pulsedAt_=clock_.elapsed();if(overlay_)overlay_->update();break;
 }
}

}

#include "ui/range_calendar.h"
#include "ui/range_visuals.h"
#include "ui/booking_labels.h"
#include "booking/booking_model.h"
#include "booking/decimal_range.h"
#include "booking/range_identity.h"
#include "ui/booking_block_info.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QToolTip>
#include <QImage>
#include <QTabBar>
#include <QContextMenuEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QLineEdit>
#include <limits>
#include <QSet>

namespace pgn::ui {
std::optional<RangeCalendarInterval> rangeCalendarInterval(const RangeCalendarSlot& cell,
 const QString& start,const QString& end,const QString& cursor){
 using namespace pgn::booking;
 if(compareUnsigned(cell.start,cell.end)>=0||compareUnsigned(start,end)>=0||!overlaps(cell.start,cell.end,start,end))return {};
 const auto first=compareUnsigned(start,cell.start)>0?start:cell.start;
 const auto last=compareUnsigned(end,cell.end)<0?end:cell.end;
 RangeCalendarInterval interval{rangeFraction(first,cell.start,cell.end),rangeFraction(last,cell.start,cell.end),{}};
 if(!cursor.isEmpty()){
  const auto clipped=compareUnsigned(cursor,first)<0?first:compareUnsigned(cursor,last)>0?last:cursor;
  interval.progressEnd=rangeFraction(clipped,cell.start,cell.end);
 }
 return interval;
}
QVector<RangeCalendarSlot> rangeCalendarSlots(const QString& first,const QString& last,int maximum){
 using namespace pgn::booking;
 if(maximum<1||maximum>100||compareUnsigned(first,last)>=0)return {};
 const auto length=subtractUnsigned(last,first);const int count=compareUnsigned(length,QString::number(maximum))<0?length.toInt():maximum;
 const auto step=divideUnsignedSmall(length,count);QVector<RangeCalendarSlot> partition;partition.reserve(count);QString cursor=canonicalUnsigned(first);
 for(int i=0;i<count;++i){const auto next=i+1==count?canonicalUnsigned(last):addUnsigned(cursor,step);partition.append({cursor,next});cursor=next;}return partition;
}
namespace {
class RangeCalendar final:public QWidget {
 struct LocalSpan {RangeCalendarInterval interval;QColor color;};
public:
 RangeCalendar(pgn::booking::BookingModel* model,std::function<void(const QString&,const QString&)> inspect,DeviceAccent accent,QWidget* parent)
  :QWidget(parent),model_(model),inspect_(std::move(inspect)),accent_(std::move(accent)){
  setObjectName("pgn-range-calendar-grid");setMinimumHeight(220);setMaximumHeight(286);setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);
  setAccessibleName("Exact number-range calendar; click or drag cells to inspect a local booking interval");refresh();
  model_->subscribe(this,[this]{refresh();});
 }
 void moveParent(int delta){manual_=true;const auto next=parent_+delta;
  if(next<0&&model_->viewPage()>0){model_->setViewPage(model_->viewPage()-1);parent_=model_->viewRanges().size()-1;}
  else if(next>=model_->viewRanges().size()&&model_->viewPage()+1<model_->viewPages()){model_->setViewPage(model_->viewPage()+1);parent_=0;}
  else parent_=std::clamp(next,0,std::max(0,int(model_->viewRanges().size())-1));rebuild();browse();}
 void jumpParent(quint64 ordinal){
  if(ordinal<1||ordinal>model_->viewCount())return;
  const quint64 zero=ordinal-1;
  if(model_->viewBitLength())model_->setViewPage(zero/pgn::booking::BitLengthCatalog::pageSize);
  manual_=true;parent_=int(model_->viewBitLength()?zero%pgn::booking::BitLengthCatalog::pageSize:zero);rebuild();browse();
 }
 void focusActive(bool notifyBrowse=true){manual_=false;if(model_->viewBitLength()&&!model_->runtime().start.isEmpty()){
  const auto bits=pgn::booking::bitLength(model_->runtime().start);if(bits!=model_->viewBitLength())model_->setViewBitLength(bits);
  const auto first=pgn::booking::decimalZoneIndex(pgn::booking::powerOfTwo(bits-1));model_->setViewPage((pgn::booking::decimalZoneIndex(model_->runtime().start)-first)/pgn::booking::BitLengthCatalog::pageSize);
 }else if(model_->viewIsRoundRange()&&!model_->runtime().start.isEmpty())model_->setRoundRange(pgn::booking::decimalZoneIndex(model_->runtime().start)/1000);follow();rebuild();if(notifyBrowse)browse();}
 void firstOpen(){
  using namespace pgn::booking;auto occupied=model_->completedRanges();occupied+=model_->jobs();
  for(const auto& row:model_->source().ranges)if(row.status=="Open"){
   const int bits=model_->viewBitLength();if(bits&&(compareUnsigned(row.start,powerOfTwo(bits-1))<0||compareUnsigned(row.end,powerOfTwo(bits))>0))continue;
   bool busy=false;for(const auto& job:occupied)if(overlaps(row.start,row.end,job.start,job.end)){busy=true;break;}
   if(busy)continue;if(model_->viewIsRoundRange())model_->setRoundRange(decimalZoneIndex(row.start)/1000);
   else if(bits){const auto base=decimalZoneIndex(powerOfTwo(bits-1));model_->setViewPage((decimalZoneIndex(row.start)-base)/BitLengthCatalog::pageSize);}
   selectBounds(row.start,row.end);publish();browse();setProperty("pgn_first_open_found",true);return;
  }setProperty("pgn_first_open_found",false);QToolTip::showText(mapToGlobal(QPoint(8,8)),"No wholly free zone in the known register. UNKNOWN is not free.",this);
 }
 void inspectBounds(QString first,QString last){const auto& rows=model_->viewRanges();for(int i=0;i<rows.size();++i)
  if(pgn::booking::compareUnsigned(rows[i].start,first)<=0&&pgn::booking::compareUnsigned(last,rows[i].end)<=0){manual_=true;parent_=i;rebuild();return;}}
 std::function<void(const QString&,const QString&)> onBrowse;
 void browse(){const auto& rows=model_->viewRanges();if(onBrowse&&parent_>=0&&parent_<rows.size())onBrowse(rows[parent_].start,rows[parent_].end);}
 bool selectBounds(QString first,QString last){if(pgn::booking::compareUnsigned(first,last)>=0)return false;const auto& rows=model_->viewRanges();for(int i=0;i<rows.size();++i)
  if(pgn::booking::compareUnsigned(rows[i].start,first)<=0&&pgn::booking::compareUnsigned(last,rows[i].end)<=0){manual_=true;parent_=i;rebuild();
   for(int index=0;index<slots_.size();++index)if(pgn::booking::overlaps(slots_[index].start,slots_[index].end,first,last)){if(first_<0)first_=index;last_=index;selected_.insert(index);}notifySelection();update();return true;}return false;}
 QRectF cell(int index)const {if(index<0||index>=slots_.size())return {};const qreal w=(width()-14)/10.,h=(height()-36)/10.;return QRectF(7+(index%10)*w,22+(index/10)*h,w-2,h-1);}
 std::optional<RangeCalendarSlot> selection()const{
  const auto all=selections();if(all.isEmpty())return {};for(int i=1;i<all.size();++i)if(all[i-1].end!=all[i].start)return {};
  return RangeCalendarSlot{all.first().start,all.last().end};
 }
 QVector<RangeCalendarSlot> selections()const{QVector<RangeCalendarSlot> result;for(int i=0;i<slots_.size();++i)if(selected_.contains(i))result.append(slots_[i]);return result;}
 void restoreSelection(const QVector<RangeCalendarSlot>& saved){selected_.clear();first_=last_=-1;for(int i=0;i<slots_.size();++i)for(const auto& slot:saved)if(slots_[i].start==slot.start&&slots_[i].end==slot.end){selected_.insert(i);if(first_<0)first_=i;last_=i;}notifySelection();update();}
 std::function<void(const QVector<RangeCalendarSlot>&)> onExactSelection;
 std::function<void(int,int)> onParent;
 BookingContextHandler context;
 std::function<void()> onClear;
 std::function<void(const std::optional<RangeCalendarSlot>&)> onSelection;
protected:
 void contextMenuEvent(QContextMenuEvent* e)override{
  const int index=hit(e->pos());if(index<0||parent_<0||parent_>=model_->viewRanges().size())return;
  if(!selected(index)){first_=last_=index;selected_={index};notifySelection();publish();update();}
  const auto all=selections();const auto& zone=model_->viewRanges()[parent_];
  if(context&&!all.isEmpty()){BookingContextRequest request{all.first().start,all.last().end,zone.start,zone.end,e->globalPos()};for(const auto& slot:all)request.blocks.append({slot.start,slot.end,zone.start,zone.end});context(request);}e->accept();
 }
 void paintEvent(QPaintEvent*)override{
  if(cache_.size()!=size()){cache_=QImage(size(),QImage::Format_ARGB32_Premultiplied);cache_.fill(Qt::transparent);paintStatic();}
  QPainter p(this);p.drawImage(0,0,cache_);
  const auto& runtime=model_->runtime();if(!runtime.run.isEmpty()&&!runtime.positions.isEmpty()){
   const auto cursor=pgn::booking::addUnsigned(runtime.start,runtime.positions);
   for(const int i:progressCells_){
    if(const auto interval=rangeCalendarInterval(slots_[i],runtime.start,runtime.end,cursor))
     drawRangeSpan(p,cell(i),deviceColor(runtime.device),interval->begin,interval->end,interval->progressEnd);
   }
  }
  for(int i=0;i<slots_.size();++i)if(selected(i)){p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor("#e8fff8"),2));p.drawRoundedRect(cell(i).adjusted(1,1,-1,-1),4,4);}
 }
 void mousePressEvent(QMouseEvent* e)override{if(e->button()!=Qt::LeftButton)return;const auto i=hit(e->position());if(i<0)return;manual_=true;dragBase_=e->modifiers().testFlag(Qt::ControlModifier)?selected_:QSet<int>{};dragSubtract_=e->modifiers().testFlag(Qt::ControlModifier)&&selected_.contains(i);first_=last_=i;dragging_=true;applyDrag();notifySelection();update();e->accept();}
 void mouseMoveEvent(QMouseEvent* e)override{const auto i=hit(e->position());if(i<0)return;if(dragging_){last_=i;applyDrag();notifySelection();update();return;}
  hovered_=i;hoverGlobal_=e->globalPosition().toPoint();refreshHover();}
 void leaveEvent(QEvent* e)override{hovered_=-1;QToolTip::hideText();QWidget::leaveEvent(e);}
 void mouseReleaseEvent(QMouseEvent* e)override{if(e->button()!=Qt::LeftButton||!dragging_)return;dragging_=false;publish();e->accept();}
 void keyPressEvent(QKeyEvent* e)override{int delta=0;switch(e->key()){case Qt::Key_Left:delta=-1;break;case Qt::Key_Right:delta=1;break;case Qt::Key_Up:delta=-10;break;case Qt::Key_Down:delta=10;break;case Qt::Key_Return:case Qt::Key_Enter:publish();return;default:QWidget::keyPressEvent(e);return;}
  if(slots_.isEmpty())return;manual_=true;last_=std::clamp(last_<0?0:last_+delta,0,int(slots_.size())-1);if(first_<0||!e->modifiers().testFlag(Qt::ShiftModifier))first_=last_;dragBase_.clear();dragSubtract_=false;applyDrag();notifySelection();update();e->accept();}
private:
 QSet<int> selected_,dragBase_;bool dragSubtract_{};
 void applyDrag(){selected_=dragBase_;for(int i=std::min(first_,last_);i<=std::max(first_,last_);++i)if(dragSubtract_)selected_.remove(i);else selected_.insert(i);}
 int hovered_{-1};QPoint hoverGlobal_;
 void refreshHover(){if(hovered_<0||hovered_>=slots_.size()||!underMouse()||dragging_)return;const auto slot=slots_[hovered_];const auto info=bookingBlockInfo(*model_,slot.start,slot.end);setProperty("pgn_live_hover_text",info);QToolTip::showText(hoverGlobal_,info,this);}
 QColor deviceColor(const QString& id)const{return accent_?accent_(id):QColor("#69efc5");}
 bool selected(int i)const{return selected_.contains(i);}
 int hit(const QPointF& pos)const{for(int i=0;i<slots_.size();++i)if(cell(i).contains(pos))return i;return -1;}
 void notifySelection(){const auto interval=selection();setProperty("pgn_calendar_selected_count",selected_.size());if(onSelection)onSelection(interval);if(!interval&&onClear)onClear();if(onExactSelection)onExactSelection(selections());}
 void publish(){const auto interval=selection();if(interval&&inspect_)inspect_(interval->start,interval->end);}
 void follow(){const auto& runtime=model_->runtime();const auto& rows=model_->viewRanges();if(runtime.run.isEmpty()){parent_=model_->viewBitLength()?0:std::max(0,int(rows.size())-1);return;}parent_=0;
  for(int i=0;i<rows.size();++i)if(pgn::booking::compareUnsigned(rows[i].start,runtime.start)<=0&&pgn::booking::compareUnsigned(runtime.end,rows[i].end)<=0){parent_=i;return;}}
 void refresh(){const auto view=model_->viewKey();const bool viewChanged=viewKey_!=view;viewKey_=view;const auto key=model_->source().sha256+model_->source().error+view;const bool first=sourceKey_.isEmpty();const bool runtimeChanged=runtimeId_!=model_->runtime().run;runtimeId_=model_->runtime().run;
  if(sourceKey_!=key||revision_!=model_->jobsRevision()||runtimeChanged){sourceKey_=key;revision_=model_->jobsRevision();if(viewChanged){hovered_=-1;manual_=false;follow();}else if(!manual_&&(first||runtimeChanged))follow();rebuild(!viewChanged);}else update();refreshHover();}
 void rebuild(bool retainSelection=false){
  // Lifecycle/progress changes do not own the user's camera or selection.
  // Restore only identical exact cell endpoints, never nearest coordinates.
  const auto saved=retainSelection?selections():QVector<RangeCalendarSlot>{};const bool reversed=first_>last_;
  cache_={};first_=last_=-1;selected_.clear();const auto& rows=model_->viewRanges();if(rows.isEmpty()){slots_.clear();notifySelection();update();return;}
  parent_=std::clamp(parent_,0,int(rows.size())-1);const auto& row=rows[parent_];slots_=rangeCalendarSlots(row.start,row.end);colors_.clear();labels_.clear();localSpans_.clear();progressCells_.clear();const auto& runtime=model_->runtime();
  if(!saved.isEmpty()){int firstMatch=-1,lastMatch=-1;
   for(int i=0;i<slots_.size();++i)for(const auto& slot:saved)if(slots_[i].start==slot.start&&slots_[i].end==slot.end){selected_.insert(i);if(firstMatch<0)firstMatch=i;lastMatch=i;}
   if(firstMatch>=0&&lastMatch>=firstMatch){first_=reversed?lastMatch:firstMatch;last_=reversed?firstMatch:lastMatch;}
  }
  auto known=model_->completedRanges();known+=model_->jobs();
  for(int index=0;index<slots_.size();++index){const auto& slot=slots_[index];if(!runtime.run.isEmpty()&&pgn::booking::overlaps(slot.start,slot.end,runtime.start,runtime.end))progressCells_.append(index);
   auto color=row.status=="Complete"?QColor("#46c8ee"):row.status=="Reserved"?QColor("#ffad4a"):QColor("#37747c");QString label="COMMUNITY DECLARED "+row.status.toUpper()+" · "+row.owner;
   QVector<LocalSpan> spans;
   for(const auto& job:known)if(const auto interval=rangeCalendarInterval(slot,job.start,job.end)){
    const auto allocation=job.state=="SOURCE_FINISHED"?QColor("#46c8ee"):QColor("#ffad4a");
    if(interval->begin==0&&interval->end==1)color=allocation;
    spans.append({*interval,allocation});label+="\n"+model_->researcherName()+" · "+localWorkLabel(job.state)+" · "+(job.device==runtime.device?runtime.label:job.device)+"\n["+job.start+", "+job.end+")";}
   localSpans_.append(spans);
   colors_.append(color);labels_.append(label);}
  setProperty("pgn_calendar_parent",parent_);setProperty("pgn_calendar_slots",slots_.size());setProperty("pgn_calendar_first",row.start);setProperty("pgn_calendar_last",row.end);if(onParent)onParent(parent_,rows.size());notifySelection();update();}
 void drawNumber(QPainter& p,int i){QFont font("Segoe UI",8);font.setWeight(QFont::DemiBold);p.setFont(font);p.setPen(QColor("#c6efeb"));p.drawText(cell(i),Qt::AlignCenter,QString::number(i+1).rightJustified(2,'0'));}
 void paintStatic(){QPainter p(&cache_);p.setRenderHint(QPainter::Antialiasing);const auto& rows=model_->viewRanges();p.setPen(QColor("#91c7cd"));p.setFont(QFont("Segoe UI",9));
  if(rows.isEmpty()){p.drawText(rect(),Qt::AlignCenter,"Source unavailable · no free-range inference");return;}const auto& row=rows[parent_];
  int processed=0,allocated=0;for(int i=0;i<slots_.size();++i){bool done=false,owned=false;for(const auto& span:localSpans_.value(i)){done|=span.color==QColor("#46c8ee");owned|=span.color==QColor("#ffad4a");}processed+=done;allocated+=owned;}
  const auto state=processed||allocated?QString("%1 processed · %2 allocated · %3 other").arg(processed).arg(allocated).arg(slots_.size()-processed-allocated):QString("Community declares %1").arg(row.status);
  setProperty("pgn_calendar_processed",processed);setProperty("pgn_calendar_allocated",allocated);
  p.drawText(QRectF(7,0,width()-14,20),Qt::AlignLeft,QString("%1 bits · %2").arg(pgn::booking::bitLength(row.start)).arg(state));
  for(int i=0;i<slots_.size();++i){drawRangeTile(p,cell(i),colors_.value(i),{}, {}, {},false);
   for(const auto& span:localSpans_.value(i))drawRangeSpan(p,cell(i),span.color,span.interval.begin,span.interval.end);
   drawNumber(p,i);}
  p.setFont(QFont("Segoe UI",8));p.setPen(QColor("#8dafb6"));p.drawText(QRectF(7,height()-14,width()-14,14),Qt::AlignLeft,"Orange = allocated · cyan = processed · click / drag blocks");
 }
 pgn::booking::BookingModel* model_;std::function<void(const QString&,const QString&)> inspect_;DeviceAccent accent_;QVector<RangeCalendarSlot> slots_;QVector<int> progressCells_;QVector<QColor> colors_;QVector<QVector<LocalSpan>> localSpans_;QStringList labels_;QImage cache_;QString sourceKey_,runtimeId_,viewKey_;quint64 revision_=std::numeric_limits<quint64>::max();int parent_{},first_=-1,last_=-1;bool dragging_=false,manual_=false;
};
}
QWidget* createRangeCalendar(pgn::booking::BookingModel* model,std::function<void(const QString&,const QString&)> inspect,DeviceAccent accent,QWidget* parent){
 auto* wrapper=new QWidget(parent);wrapper->setObjectName("pgn-range-calendar");auto* layout=new QVBoxLayout(wrapper);layout->setContentsMargins(0,0,0,0);layout->setSpacing(6);
 auto* heading=new QLabel("NUMBER-RANGE CALENDAR");heading->setObjectName("pgn-calendar-heading");heading->setStyleSheet("color:#69efc5;font-weight:700");layout->addWidget(heading);
 auto* bitsTabs=new QTabBar;bitsTabs->setObjectName("pgn-catalog-bits");bitsTabs->setExpanding(false);bitsTabs->setUsesScrollButtons(true);bitsTabs->addTab("Register");for(int bits=65;bits<=pgn::booking::CatalogMaximumBits;++bits)bitsTabs->addTab(QString::number(bits)+" bits");bitsTabs->setToolTip("Exact bit lengths65..95,100 blocks per zone. Outside the community register is UNKNOWN, not automatically free. Register preserves existing zone IDs.");bitsTabs->setStyleSheet("QTabBar::tab{min-width:0;min-height:0;padding:4px 7px;}QTabBar::tab:selected{background:#14524b;color:#dffff3;}");layout->addWidget(bitsTabs);
 QObject::connect(bitsTabs,&QTabBar::currentChanged,model,[model](int index){model->setViewBitLength(index?index+64:0);});
 const auto refreshBits=[model,bitsTabs]{QSignalBlocker guard(bitsTabs);bitsTabs->setCurrentIndex(model->viewBitLength()?model->viewBitLength()-64:0);};model->subscribe(wrapper,refreshBits);refreshBits();
 auto* controls=new QHBoxLayout;controls->setSpacing(4);auto* previous=new QPushButton("‹");auto* prefix=new QLabel("ZONE");auto* jump=new QSpinBox;auto* label=new QLabel;auto* next=new QPushButton("›");auto* active=new QPushButton("Mining zone");
 jump->setObjectName("pgn-calendar-jump");jump->setStyleSheet("QSpinBox{min-width:0;padding:2px 4px;}QSpinBox QLineEdit{padding:0;}");jump->setButtonSymbols(QAbstractSpinBox::NoButtons);jump->setKeyboardTracking(false);jump->setFixedWidth(76);jump->setToolTip("Enter a zone number and press Enter. Browsing only; no reservation or mining action.");jump->setAccessibleName("Go directly to a zone number");
 previous->setObjectName("pgn-calendar-previous");next->setObjectName("pgn-calendar-next");active->setObjectName("pgn-calendar-active");previous->setFixedWidth(28);next->setFixedWidth(28);for(auto* button:{previous,next,active})button->setStyleSheet("padding:5px;");controls->addWidget(previous);controls->addWidget(prefix);controls->addWidget(jump);controls->addWidget(label,1);controls->addWidget(next);layout->addLayout(controls);auto* shortcuts=new QHBoxLayout;auto* open=new QPushButton("First open");open->setObjectName("pgn-calendar-first-open");open->setStyleSheet("padding:5px;");open->setToolTip("Select the first wholly free zone from the known register. No reservation or mining starts.");shortcuts->addWidget(open);shortcuts->addWidget(active);shortcuts->addStretch();layout->addLayout(shortcuts);
 // Queue content must not compress primary navigation into tiny click targets.
 for(auto* button:{previous,next,active,open}){button->setMinimumHeight(24);button->setStyleSheet(button->styleSheet()+"min-height:12px;");}
 auto* calendar=new RangeCalendar(model,std::move(inspect),std::move(accent),wrapper);layout->addWidget(calendar);
 auto* selection=new QLabel;selection->setObjectName("pgn-calendar-selection");selection->setWordWrap(true);selection->setTextInteractionFlags(Qt::TextSelectableByMouse);selection->setStyleSheet("background:#08242b;border:1px solid #286d73;border-radius:7px;padding:8px;color:#c8fff0;");layout->addWidget(selection);
 calendar->onSelection=[selection,model](const std::optional<RangeCalendarSlot>& interval){
  if(!interval){selection->setText("Choose a case or drag an interval · nothing is reserved automatically");return;}
  const auto identity=pgn::booking::identifyRange(model->source(),interval->start,interval->end);
  selection->setText(QString("SELECTED · %1 · %2 positions").arg(identity.title,pgn::booking::subtractUnsigned(interval->end,interval->start)));
  selection->setToolTip(identity.exact+" · local selection, not a public lease");
 };
 calendar->onParent=[model,prefix,jump,label,previous,next](int i,int count){QSignalBlocker guard(jump);prefix->setText("ZONE");const int ordinal=int(model->viewFirstOrdinal()+i);if(jump->minimum()!=1||jump->maximum()!=int(model->viewCount()))jump->setRange(1,int(model->viewCount()));const auto* editor=jump->findChild<QLineEdit*>();if(!editor||!editor->hasFocus()||jump->property("pgn_sync_zone").toInt()!=ordinal)jump->setValue(ordinal);jump->setProperty("pgn_sync_zone",ordinal);label->setText("/ "+QString::number(model->viewCount()));label->setToolTip(QString("Page %1 of %2; exact zone identities retained").arg(model->viewPage()+1).arg(model->viewPages()));previous->setEnabled(i>0||model->viewPage()>0);next->setEnabled(i+1<count||model->viewPage()+1<model->viewPages());};calendar->focusActive();
 QObject::connect(jump,&QSpinBox::valueChanged,calendar,[calendar](int value){calendar->jumpParent(quint64(value));});
 QObject::connect(open,&QPushButton::clicked,calendar,[calendar]{calendar->firstOpen();});
 QObject::connect(previous,&QPushButton::clicked,calendar,[calendar]{calendar->moveParent(-1);});QObject::connect(next,&QPushButton::clicked,calendar,[calendar]{calendar->moveParent(1);});QObject::connect(active,&QPushButton::clicked,calendar,[calendar]{calendar->focusActive();});return wrapper;
}
QRectF rangeCalendarCell(QWidget* widget,int index){if(auto* calendar=dynamic_cast<RangeCalendar*>(widget))return calendar->cell(index);return {};}
std::optional<RangeCalendarSlot> rangeCalendarSelection(QWidget* widget){if(auto* calendar=dynamic_cast<RangeCalendar*>(widget))return calendar->selection();return {};}
QVector<RangeCalendarSlot> rangeCalendarSelections(QWidget* widget){if(auto* calendar=dynamic_cast<RangeCalendar*>(widget))return calendar->selections();return {};}
void bindRangeCalendarSelection(QWidget* widget,std::function<void(const QVector<RangeCalendarSlot>&)> handler){if(auto* calendar=dynamic_cast<RangeCalendar*>(widget)){const auto prior=calendar->onExactSelection;calendar->onExactSelection=[prior,handler=std::move(handler)](const auto& cells){if(prior)prior(cells);if(handler)handler(cells);};}}
bool selectRangeCalendar(QWidget* widget,const QString& first,const QString& last){if(auto* calendar=dynamic_cast<RangeCalendar*>(widget))return calendar->selectBounds(first,last);return false;}
void inspectRangeCalendar(QWidget* widget,const QString& first,const QString& last){if(auto* calendar=dynamic_cast<RangeCalendar*>(widget))calendar->inspectBounds(first,last);}
void focusCurrentRangeCalendar(QWidget* widget){if(auto* calendar=dynamic_cast<RangeCalendar*>(widget)){
 const auto selected=calendar->selections();calendar->focusActive(false);
 // A camera-only action must retain an explicit selection already inside
 // the mining zone; a selection from another zone must not pull the
 // calendar back there after synchronization.
 if(!selected.isEmpty()&&pgn::booking::compareUnsigned(calendar->property("pgn_calendar_first").toString(),selected.first().start)<=0&&
    pgn::booking::compareUnsigned(selected.last().end,calendar->property("pgn_calendar_last").toString())<=0)
  calendar->restoreSelection(selected);
}}
void bindRangeCalendarBrowse(QWidget* widget,std::function<void(const QString&,const QString&)> handler){if(auto* calendar=dynamic_cast<RangeCalendar*>(widget))calendar->onBrowse=std::move(handler);}
void bindRangeCalendarContext(QWidget* widget,BookingContextHandler handler){if(auto* calendar=dynamic_cast<RangeCalendar*>(widget))calendar->context=std::move(handler);}
void bindRangeCalendarClear(QWidget* widget,std::function<void()> handler){if(auto* calendar=dynamic_cast<RangeCalendar*>(widget))calendar->onClear=std::move(handler);}
}

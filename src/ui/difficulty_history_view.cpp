#include "ui/difficulty_history_view.h"
#include "storage/sqlite_read.h"
#include "ui/render/graph_view.h"
#include "ui/render/graph_border_ticks.h"
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QWidget>
#include <QPainter>
#include <QFontMetrics>
#include <QElapsedTimer>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>
#include <limits>
namespace pgn::ui {
DifficultyRaster renderDifficultyHistory(const QString& file,const pgn::wallet::DifficultySummary& summary,const DenseView& view,
 const std::shared_ptr<std::atomic_bool>& cancel,const DifficultyRaster* previous){
 DifficultyRaster result;result.genesis=summary.genesis;result.tipHash=summary.tipHash;result.height=summary.height;result.view=view;QElapsedTimer timer;timer.start();
 if(!summary.available){result.error="No verified header history received";return result;}
 if(view.pixels.width()<1||view.pixels.height()<1||!std::isfinite(view.minX)||!std::isfinite(view.maxX)||!std::isfinite(view.minY)||!std::isfinite(view.maxY)||
    !std::isfinite(view.maxX-view.minX)||!std::isfinite(view.maxY-view.minY)||view.maxX<=view.minX||view.maxY<=view.minY){result.error="Invalid difficulty viewport; previous complete raster retained";return result;}
 try{
  pgn::storage::ReadOnlyDatabase db(file);db.execute("BEGIN");qint64 cutoff=-1;DensePointLayer layer;
  if(previous&&previous->complete&&previous->genesis==summary.genesis&&previous->view==view&&previous->height<=summary.height){
   pgn::storage::ReadStatement ancestor(db.db,R"SQL(WITH RECURSIVE chain(hash,previous,height) AS (
    SELECT hash,previous,height FROM difficulty_headers WHERE genesis=?1 AND hash=?2
    UNION ALL SELECT p.hash,p.previous,p.height FROM chain c CROSS JOIN difficulty_headers p WHERE p.genesis=?1 AND p.hash=c.previous AND c.height>?3
   ) SELECT hash FROM chain WHERE height=?3)SQL");ancestor.text(1,summary.genesis);ancestor.text(2,summary.tipHash);ancestor.integer(3,previous->height);
   if(ancestor.step()==SQLITE_ROW&&ancestor.text(0)==previous->tipHash&&layer.resume(view,previous->image,std::size_t(previous->points))){cutoff=previous->height;result.incremental=true;}
  }
  if(!result.incremental)layer.begin(view);
  pgn::storage::ReadStatement rows(db.db,R"SQL(WITH RECURSIVE chain(hash,previous,height,difficulty) AS (
   SELECT hash,previous,height,difficulty FROM difficulty_headers WHERE genesis=?1 AND hash=?2 AND ancestry_complete=1
   UNION ALL SELECT p.hash,p.previous,p.height,p.difficulty FROM chain c CROSS JOIN difficulty_headers p WHERE p.genesis=?1 AND p.hash=c.previous AND c.height>?3+1
  ) SELECT height,difficulty FROM chain WHERE height>?3)SQL");rows.text(1,summary.genesis);rows.text(2,summary.tipHash);rows.integer(3,cutoff);
  std::vector<DensePoint> points;points.reserve(256);
  while(rows.step()==SQLITE_ROW){
   if(cancel&&cancel->load()){result.error="Cancelled for a newer view; previous complete raster retained";return result;}
   ++result.rowsRead;const auto text=rows.text(1);if(text.isEmpty())continue;
   bool ok=false;const double difficulty=text.toDouble(&ok);if(!ok||!std::isfinite(difficulty)||difficulty<0)throw std::runtime_error("Stored reported difficulty is invalid");
   points.push_back({double(sqlite3_column_int64(rows.value,0)),difficulty,qRgb(85,233,190)});
   if(points.size()==256){layer.append(points);points.clear();}
  }
  layer.append(points);layer.finish();result.image=layer.image();result.points=qint64(layer.visibleCount());
  if(cutoff<0&&view.minX<=0&&view.maxX>=double(summary.height)&&summary.minimum&&summary.maximum&&view.minY<=*summary.minimum&&view.maxY>=*summary.maximum&&result.points!=summary.points)throw std::runtime_error("Full history point count mismatch; no complete frame published");
  result.complete=true;db.execute("COMMIT");
 }catch(const std::exception& error){result.error=QString::fromUtf8(error.what());result.complete=false;}
 result.elapsedNs=timer.nsecsElapsed();return result;
}
namespace {
class DifficultyView final:public QWidget{
public:
 explicit DifficultyView(QString file):file_(std::move(file)){
  setObjectName("pgn-wallet-difficulty-history");setMinimumHeight(220);
  auto* layout=new QVBoxLayout(this);caption_=new QLabel("DIFFICULTY HISTORY · awaiting verified Core headers · no values invented");caption_->setWordWrap(true);layout->addWidget(caption_);
  auto* reset=new QPushButton("Show complete history");reset->setObjectName("pgn-wallet-difficulty-reset");layout->addWidget(reset,0,Qt::AlignLeft);layout->addStretch();
  connect(reset,&QPushButton::clicked,this,[this]{custom_=false;request();});
  connect(&worker_,&QFutureWatcher<DifficultyRaster>::finished,this,[this]{
   const auto result=worker_.result();if(activeGeneration_==generation_){
    if(result.complete){frame_=result;setProperty("pgn_wallet_difficulty_frame_points",frame_.points);setProperty("pgn_wallet_difficulty_rows_read",frame_.rowsRead);setProperty("pgn_wallet_difficulty_incremental",frame_.incremental);setProperty("pgn_wallet_difficulty_render_ns",frame_.elapsedNs);}
    setProperty("pgn_wallet_difficulty_error",result.error);
   }
   setProperty("pgn_wallet_difficulty_loading",false);update();if(pending_){pending_=false;start();}
  });
 }
 ~DifficultyView()override{if(cancel_)cancel_->store(true);}
 void sourceError(const QString& error){setProperty("pgn_wallet_difficulty_source_error",error);caption_->setText("DIFFICULTY HISTORY · "+error+" · original snapshot retained");}
 void present(const pgn::wallet::DifficultySummary& summary){
  if(!summary.available){
   setProperty("pgn_wallet_difficulty_error",summary.error.isEmpty()?QString("History unavailable; original snapshot retained"):summary.error);
   caption_->setText(summary_.available?QString("DIFFICULTY HISTORY · source unavailable · retained snapshot from %1").arg(summary_.observedUtc.toUTC().toString(Qt::ISODateWithMs)):QString("DIFFICULTY HISTORY · awaiting verified Core headers · no values invented"));update();return;
  }
  const bool changed=summary_.tipHash!=summary.tipHash||summary_.genesis!=summary.genesis;summary_=summary;
  setProperty("pgn_wallet_difficulty_source_error",QVariant());
  setProperty("pgn_wallet_difficulty_points",summary.points);setProperty("pgn_wallet_difficulty_missing",summary.missing);setProperty("pgn_wallet_difficulty_tip",summary.tipHash);
  setProperty("pgn_wallet_difficulty_observed_utc",summary.observedUtc);
  caption_->setText(summary.available?QString("DIFFICULTY HISTORY · %1 retained points · %2 unavailable · snapshot height %3 · source UTC %4").arg(summary.points).arg(summary.missing).arg(summary.height).arg(summary.observedUtc.toUTC().toString(Qt::ISODateWithMs)):
   QString("DIFFICULTY HISTORY · awaiting verified Core headers · no values invented"));caption_->setToolTip("Archived header observations, not a live connection or consensus approval. Chain: "+summary.genesis+"\nTip: "+summary.tipHash);
  if(changed||!frame_.complete)request();
 }
protected:
 void paintEvent(QPaintEvent*)override{
  QPainter painter(this);painter.fillRect(plot(),QColor("#06181d"));painter.setPen(QColor("#4d7978"));painter.drawRect(plot().adjusted(0,0,-1,-1));
  if(frame_.complete){
   const auto& v=frame_.view;const auto area=plot();
   const auto ticks=[&](double low,double high,int n,bool integral){return FrontierVisibleMeritTicks({0.,1.,low,high},n,integral);};
   const auto yTicks=FrontierBorderTicks(v.minY,v.maxY,ticks(v.minY,v.maxY,std::clamp(int(area.height()/30.),3,12),false),area.height(),22.);
   const auto xTicks=FrontierBorderTicks(v.minX,v.maxX,ticks(v.minX,v.maxX,std::clamp(int(area.width()/90.),2,12),true),area.width(),82.);
   QVariantList xValues,yValues;
   for(const auto value:xTicks)xValues.append(value);
   for(const auto value:yTicks)yValues.append(value);
   setProperty("pgn_wallet_difficulty_x_ticks",xValues);setProperty("pgn_wallet_difficulty_y_ticks",yValues);
   setProperty("pgn_wallet_difficulty_plot",area);
   setProperty("pgn_wallet_difficulty_view",QVariantList{v.minX,v.maxX,v.minY,v.maxY});
   painter.setFont(QFont("Segoe UI",8));
   for(double value:yTicks){
    const auto y=area.bottom()-area.height()*(value-v.minY)/(v.maxY-v.minY);
    painter.setPen(QColor(55,101,110,110));painter.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
    painter.setPen(QColor("#b6e5dd"));painter.drawLine(QPointF(area.left()-4,y),QPointF(area.left(),y));
    painter.drawLine(QPointF(area.right(),y),QPointF(area.right()+4,y));
    painter.drawText(QRectF(1,y-8,area.left()-7,16),Qt::AlignRight|Qt::AlignVCenter,QString::number(value,'g',6));
   }
   for(double value:xTicks){
    const auto x=area.left()+area.width()*(value-v.minX)/(v.maxX-v.minX);
    painter.setPen(QColor(55,101,110,110));painter.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));
    painter.setPen(QColor("#b6e5dd"));painter.drawLine(QPointF(x,area.bottom()),QPointF(x,area.bottom()+4));
    painter.drawLine(QPointF(x,area.top()-4),QPointF(x,area.top()));
    const auto label=QString::number(value,'g',6);const qreal ink=QFontMetricsF(painter.font()).horizontalAdvance(label)+6.;
    const qreal left=std::clamp(x-ink/2.,0.,std::max(0.,width()-ink));
    painter.drawText(QRectF(left,area.bottom()+5,ink,16),Qt::AlignCenter,label);
   }
   painter.drawImage(area,frame_.image);
   // Keep the viewport frame visible over data at either extreme. Nice grid
   // ticks are interior choices; the exact visible boundaries always exist.
   painter.setPen(QColor("#b6e5dd"));painter.setBrush(Qt::NoBrush);painter.drawRect(area);
   painter.setPen(QColor("#b6e5dd"));painter.drawText(QRectF(50,height()-22,width()-75,20),Qt::AlignCenter,QString("BLOCK HEIGHT · %1 visible points").arg(frame_.points));
  }
  else{painter.setPen(QColor("#89bab4"));painter.drawText(plot(),Qt::AlignCenter,"No verified difficulty history available");}
 }
 void resizeEvent(QResizeEvent*)override{request();}
 void mousePressEvent(QMouseEvent* event)override{if(event->button()==Qt::LeftButton&&plot().contains(event->position())&&summary_.available){drag_=true;last_=event->position();custom_=true;}else QWidget::mousePressEvent(event);}
 void mouseReleaseEvent(QMouseEvent* event)override{drag_=false;QWidget::mouseReleaseEvent(event);}
 void mouseMoveEvent(QMouseEvent* event)override{if(!drag_){QWidget::mouseMoveEvent(event);return;}const auto delta=event->position()-last_;last_=event->position();const double x=delta.x()*(view_.maxX-view_.minX)/plot().width(),y=delta.y()*(view_.maxY-view_.minY)/plot().height();view_.minX-=x;view_.maxX-=x;view_.minY+=y;view_.maxY+=y;request();}
 void wheelEvent(QWheelEvent* event)override{if(!plot().contains(event->position())||!summary_.available){QWidget::wheelEvent(event);return;}const double scale=std::exp(std::clamp(-event->angleDelta().y()/1200.,-2.,2.)),x=view_.minX+(event->position().x()-plot().left())*(view_.maxX-view_.minX)/plot().width(),y=view_.maxY-(event->position().y()-plot().top())*(view_.maxY-view_.minY)/plot().height();view_.minX=x+(view_.minX-x)*scale;view_.maxX=x+(view_.maxX-x)*scale;view_.minY=y+(view_.minY-y)*scale;view_.maxY=y+(view_.maxY-y)*scale;custom_=true;request();event->accept();}
private:
 QRectF plot()const{return {50.,100.,double(std::max(1,width()-75)),double(std::max(1,height()-135))};}
 void request(){
  ++generation_;if(!summary_.available||!summary_.minimum||!summary_.maximum){
   if(cancel_)cancel_->store(true);pending_=false;
   if(summary_.available&&summary_.points==0){frame_={};setProperty("pgn_wallet_difficulty_frame_points",qint64(0));setProperty("pgn_wallet_difficulty_loading",false);}
   update();return;
  }
  if(!custom_){view_.minX=0;view_.maxX=std::max(1.,std::exp2(std::ceil(std::log2(double(summary_.height)+1.))));view_.minY=0;const double top=std::exp2(std::ceil(std::log2(std::max(1.,*summary_.maximum))));view_.maxY=std::isfinite(top)?top:std::numeric_limits<double>::max();}
  view_.pixels=plot().size().toSize();if(worker_.isRunning()){pending_=true;if(cancel_)cancel_->store(true);return;}start();
 }
 void start(){
  activeGeneration_=generation_;cancel_=std::make_shared<std::atomic_bool>(false);setProperty("pgn_wallet_difficulty_loading",true);
  const auto file=file_;const auto summary=summary_;const auto view=view_;const auto cancel=cancel_;const auto previous=frame_;
  worker_.setFuture(QtConcurrent::run([file,summary,view,cancel,previous]{return renderDifficultyHistory(file,summary,view,cancel,previous.complete?&previous:nullptr);}));
 }
 QString file_;QLabel* caption_{};pgn::wallet::DifficultySummary summary_;DenseView view_;DifficultyRaster frame_;QFutureWatcher<DifficultyRaster> worker_;
 std::shared_ptr<std::atomic_bool> cancel_;quint64 generation_{},activeGeneration_{};bool pending_{},custom_{},drag_{};QPointF last_;
};
}
QWidget* createDifficultyHistoryView(const QString& file){return new DifficultyView(file);}
void presentDifficultyHistory(QWidget* widget,const pgn::wallet::DifficultySummary& summary){if(auto* view=dynamic_cast<DifficultyView*>(widget))view->present(summary);}
void presentDifficultyHistoryError(QWidget* widget,const QString& error){if(auto* view=dynamic_cast<DifficultyView*>(widget))view->sourceError(error);}
}

#include "ui/booking_rail_layout.h"
#include "ui/mining_canvas.h"
#include "ui/range_calendar.h"
#include "booking/booking_model.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFontMetrics>
#include <QSet>
#include <iostream>
#include <memory>
#include <QThread>
#include <QElapsedTimer>

int main(int argc,char** argv){
 QApplication app(argc,argv);if(argc<2)return 1;
  const auto outside=pgn::booking::identifyRange({},"878591182965606809580","878591184965606809580");
  const auto title=pgn::ui::bookingRailTitle(outside,"9e1af426a98d462f8ef787eb0c2c76b2");
  if(!title.contains("Z786")||!title.contains("2 T positions")||title.contains("9e1af426"))return 24;
 if(pgn::ui::bookingRailRuntimeDetail(false,1.,"DISPATCHED")!="CALCULATION COMPLETE · VERIFYING RESULTS" ||
    pgn::ui::bookingRailRuntimeDetail(false,1.,"SOURCE_FINISHED")!="COMPLETED" ||
    pgn::ui::bookingRailRuntimeDetail(true,0.25,"DISPATCHED")!="MINING · 25.00%" ||
    pgn::ui::bookingRailRuntimeDetail(true,std::nullopt,"DISPATCHED")!="MINING · ?")return 23;
 for(int total:{0,1,2,9,100,4096})for(int width:{40,150,320,1000,4000})for(int offset:{-100,0,1,4096}){
  const QRectF track(7,20,width,36);const auto page=pgn::ui::bookingRailPage(track,total,offset);
  if(page.count<0||page.count>total||page.cells.size()!=page.count||page.first<0||page.first+page.count>total)return 2;
  for(int i=0;i<page.cells.size();++i){if(!track.contains(page.cells[i])||(i&&page.cells[i-1].right()>=page.cells[i].left()))return 3;}
 }
 pgn::booking::RangeIdentity identity;identity.zone=999;identity.firstBlock=2;identity.lastBlock=2;
 if(pgn::ui::bookingRailTitle(identity,"qa")!="Z1000 · B02")return 4;
 identity.lastBlock=100;identity.title="68 bits · Zone 1000 · Blocks 2–100 · partial";
 if(pgn::ui::bookingRailTitle(identity,"qa")!="Z1000 · B02–100 *")return 5;
 identity.zone=-1;identity.title="70 bits · 2 T positions";if(pgn::ui::bookingRailTitle(identity,"abcdefgh123")!=identity.title)return 6;
 QTemporaryDir temp;QDir().mkpath(temp.path()+"/data/runtime");
 for(const auto name:{"binding.json","community-source.csv"})if(!QFile::copy(QString::fromLocal8Bit(argv[1])+"/"+name,temp.path()+"/data/runtime/"+name))return 7;
 pgn::booking::BookingModel model(temp.path());const auto& zones=model.source().ranges;
 int open=-1;for(int i=0;i<zones.size();++i)if(model.source().declaredOpenContaining(zones[i].start,zones[i].end)){open=i;break;}
 if(open<0)return 8;const auto& zone=zones[open];
 const auto firstCell=pgn::ui::rangeCalendarSlots(zone.start,zone.end).first();
 const QString run(32,'a');const auto observed=QDateTime::currentDateTimeUtc();
 model.observeRuntime(run,firstCell.start,firstCell.end,"GPU-11111111-2222-3333-4444-555555555555","QA","0",observed,true);
 const auto reserved=model.reserveBlocks(zone.start,zone.end,zone.start,zone.end);
 if(!reserved.isEmpty()||model.jobs().size()!=100){std::cerr<<"reserve="<<reserved.toStdString()<<" jobs="<<model.jobs().size()<<" zone="<<open+1<<'\n';return 9;}
 std::unique_ptr<QWidget> canvas(pgn::ui::createMiningCanvas());pgn::ui::bindBookingModel(canvas.get(),&model);
 canvas->resize(1200,900);canvas->show();app.processEvents();canvas->repaint();
 if(argc>2){const auto rail=canvas->property("pgn_booking_rail_track").toRectF().adjusted(-12,-24,12,8).toAlignedRect();
  if(!canvas->grab(rail).save(QString::fromLocal8Bit(argv[2])))return 21;}
 auto ids=[&]{return canvas->property("pgn_booking_rail_visible_ids").toStringList();};

 if(ids().size()!=100||canvas->property("pgn_reservation_segments").toInt()!=100)return 10;
 QString selected;model.onSelect=[&](const QString& id){selected=id;};
 for(const auto& id:ids()){
  const auto box=pgn::ui::reservationSegmentRect(canvas.get(),id);if(box.isEmpty())return 11;
  QMouseEvent click(QEvent::MouseButtonPress,box.center(),box.center(),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
  QApplication::sendEvent(canvas.get(),&click);if(selected!=id)return 12;
 }
 if(!canvas->property("pgn_booking_rail_next").toRectF().isEmpty()||model.jobs().size()!=100)return 13;
 for(const auto& state:{"ACTIVE","DISPATCHING","DISPATCHED","RUNNING"})if(!pgn::ui::bookingRailJobActive(state))return 24;
 if(pgn::ui::bookingRailJobActive("QUEUED")||!pgn::ui::bookingRailJobActive("PAUSED",true))return 25;
 for(int width:{320,480,900,1800})for(int activeCount:{0,1,12,64}){
  QVector<bool> active(100,false);QStringList keys;
  for(int i=0;i<100;++i){active[i]=i<activeCount;keys.append("zone/state");}
  const auto overview=pgn::ui::bookingRailOverview(QRectF(7,20,width,36),active,keys);
  if(overview.cells.size()!=100||overview.rows<1)return 14;
  const QRectF bounds(7,20,width,overview.height);
  for(int i=0;i<100;++i){const auto box=overview.cells[i];
   if(box.width()<=0||!bounds.adjusted(-.01,-.01,.01,.01).contains(box))return 15;
   if(active[i]&&box.width()<145.9)return 16;
   for(int j=0;j<i;++j)if(box.intersected(overview.cells[j]).width()>.01&&box.intersected(overview.cells[j]).height()>.01)return 17;
  }
 }
 for(double maximum:{0.,500.})for(double wanted:{-1e9,-30.,0.,30.,1e9}){
  const auto actual=pgn::ui::elasticDeviceScroll(wanted,maximum);
  if(actual< -56.001||actual>maximum+56.001)return 18;
 }
 canvas->resize(1800,900);canvas->repaint();
 const double stripLeft=canvas->property("pgn_device_strip_view_left").toDouble();
 const double stripWidth=canvas->property("pgn_device_strip_view_width").toDouble();
 const double content=canvas->property("pgn_device_strip_content_width").toDouble();
 if(canvas->property("pgn_device_strip_scroll_maximum").toDouble()==0&&
    std::abs(canvas->property("pgn_device_strip_left").toDouble()-(stripLeft+(stripWidth-content)/2.))>.01)return 19;

 const QPointF anchor(stripLeft+stripWidth/2.,canvas->property("pgn_device_strip_top").toDouble()+60);
 QMouseEvent press(QEvent::MouseButtonPress,anchor,anchor,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
 QApplication::sendEvent(canvas.get(),&press);
 const auto dragged=anchor+QPointF(80,0);
 QMouseEvent move(QEvent::MouseMove,dragged,dragged,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
 QApplication::sendEvent(canvas.get(),&move);canvas->repaint();
 if(canvas->property("pgn_device_strip_scroll").toDouble()>=-1)return 20;
 QMouseEvent release(QEvent::MouseButtonRelease,dragged,dragged,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
 QApplication::sendEvent(canvas.get(),&release);
 QElapsedTimer clock;clock.start();while(clock.elapsed()<2000){app.processEvents();canvas->repaint();if(std::abs(canvas->property("pgn_device_strip_scroll").toDouble())<.01)break;QThread::msleep(10);}
 canvas->repaint();if(std::abs(canvas->property("pgn_device_strip_scroll").toDouble())>.01){std::cerr<<"SPRING_END="<<canvas->property("pgn_device_strip_scroll").toDouble()<<"\n";return 21;}
 std::cout<<"PASS all100 jobs represented/clicked;12/64 active geometry at4widths;real elastic drag/spring;no queue mutation\n";

}

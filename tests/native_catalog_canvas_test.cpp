#include "qt_offscreen_fixture.h"
#include "ui/mining_canvas.h"
#include "ui/catalog_projection.h"
#include "ui/catalog_frame_presentation.h"
#include "horizon_graph_controls.h"
#include "graph_view.h"
#include "telemetry.h"
#include <QApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QElapsedTimer>
#include <QThread>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QPluginLoader>
#include <QJsonArray>
#include <QDir>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <QTimeZone>
#include <bit>
#include <iostream>
#include <memory>
int main(int argc,char** argv){
 if(!pgn::test::prepareOffscreen())return 80;
 QApplication app(argc,argv);if(app.platformName()!="offscreen")return 81;
 QTemporaryDir temporary;const auto file=temporary.path()+"/native.sqlite";
 {const FrontierGraphView requested{201,451,19.125,33.375};
  const auto fitted=FrontierNiceGapGraphView(requested,5);
  if(fitted.minTimeMs>requested.minTimeMs||fitted.maxTimeMs<requested.maxTimeMs||
     fitted.minMerit>requested.minMerit||fitted.maxMerit<requested.maxMerit){
   std::cerr<<"FAIL LENGTH auto-fit discards requested lower edge to fit upper edge\n";return 59;
  }
 }
 for(int intervals=1;intervals<=8;++intervals)for(int low=0;low<=160;++low)for(int width=1;width<=80;++width){
  const FrontierGraphView requested{123,987,low*.25,low*.25+width*.25};
  const auto fitted=FrontierIntegerMeritGraphView(requested,intervals);
  const auto step=(fitted.maxMerit-fitted.minMerit)/intervals;
  if(fitted.minMerit>requested.minMerit||fitted.maxMerit<requested.maxMerit||
     fitted.minTimeMs!=123||fitted.maxTimeMs!=987||step<1||step!=std::floor(step)){
   std::cerr<<"FAIL integral merit auto-fit clips requested source bounds\n";return 34;
  }
  for(const auto& view:{FrontierNiceGraphView(requested,intervals),
                       FrontierNiceGapGraphView({requested.minMerit,requested.maxMerit,
                                                requested.minMerit,requested.maxMerit},intervals)}){
   const auto isTime=view.maxTimeMs>=987;
   const double expectedMinX=isTime?123:requested.minMerit;
   const double expectedMaxX=isTime?987:requested.maxMerit;
   if(view.minTimeMs>expectedMinX||view.maxTimeMs<expectedMaxX||
      view.minMerit>requested.minMerit||view.maxMerit<requested.maxMerit){
    std::cerr<<"FAIL shared axis fit clips requested source bounds\n";return 60;
   }
  }
 }
 const QString a(32,'a'),b(32,'b');constexpr qint64 base=1791682500000;
 {auto db=QSqlDatabase::addDatabase("QSQLITE","native-fixture");db.setDatabaseName(file);if(!db.open())return 1;
  {QSqlQuery q(db);if(!q.exec("CREATE TABLE native_plot_coordinates(scope TEXT,axis TEXT,point_key TEXT,gap_id INTEGER,x REAL,y REAL,admission_revision INTEGER,PRIMARY KEY(scope,axis,point_key)) WITHOUT ROWID"))return 2;
   if(!q.exec("CREATE INDEX native_plot_revision ON native_plot_coordinates(scope,axis,admission_revision,point_key)"))return 14;
   db.transaction();q.prepare("INSERT INTO native_plot_coordinates VALUES(?,?,?,?,?,?,?)");
   for(int i=0;i<5000;++i)for(const QString& scope:{QStringLiteral(""),i<2500?a:b})for(const QString& axis:{QStringLiteral("time"),QStringLiteral("length")}){
    const double x=axis=="time"?base+i*1000.0:1000+(i%8)*2.0;
    const auto bits=std::bit_cast<std::uint64_t>(x)^(std::uint64_t{1}<<63);
    q.bindValue(0,scope);q.bindValue(1,axis);q.bindValue(2,QString::number(bits,16).rightJustified(16,'0')+QString::number(i+1,16).rightJustified(16,'0'));
    q.bindValue(3,i+1);q.bindValue(4,x);q.bindValue(5,19+i%15);q.bindValue(6,scope.isEmpty()||i<2500?i+1:i-2499);
    if(!q.exec()){std::cerr<<"fixture insertion failed\n";return 3;}
   }db.commit();}db.close();}QSqlDatabase::removeDatabase("native-fixture");
 const auto token=std::make_shared<std::atomic_bool>(false);
 const pgn::ui::DenseView directView{999,1015,18,34,QSize(900,300),5000};
 const auto all=pgn::ui::renderNativeCatalog(file,{},"length",directView,token,qRgb(90,220,170));
 const auto current=pgn::ui::renderNativeCatalog(file,b,"length",directView,token,qRgb(90,220,170));
 if(!all.complete||all.points!=5000||!current.complete||current.points!=2500)return 4;
 auto older=directView;older.revision=1000;
 if(pgn::ui::renderNativeCatalog(file,b,"length",older,token,qRgb(90,220,170)).points!=1000)return 13;
 const auto baseRaster=pgn::ui::renderNativeCatalog(file,{},"length",older,token,qRgb(90,220,170));
 const auto appended=pgn::ui::renderNativeCatalog(file,{},"length",directView,token,qRgb(90,220,170),&baseRaster);
 if(!appended.complete||!appended.incremental||appended.rowsRead!=4000||appended.points!=all.points||appended.image!=all.image)return 15;
 const auto nearCompleteView=pgn::ui::DenseView{999,1015,18,34,QSize(900,300),4990};
 const auto nearComplete=pgn::ui::renderNativeCatalog(file,{},"length",nearCompleteView,token,qRgb(90,220,170));
 const auto lastTen=pgn::ui::renderNativeCatalog(file,{},"length",directView,token,qRgb(90,220,170),&nearComplete);
 if(!lastTen.complete||!lastTen.incremental||lastTen.rowsRead!=10||lastTen.image!=all.image)return 16;
 const auto changedScope=pgn::ui::renderNativeCatalog(file,b,"length",directView,token,qRgb(90,220,170),&baseRaster);
 if(changedScope.incremental||changedScope.image!=current.image)return 17;
 auto zoom=directView;zoom.minX=1003;
 const auto zoomFull=pgn::ui::renderNativeCatalog(file,{},"length",zoom,token,qRgb(90,220,170));
 const auto changedZoom=pgn::ui::renderNativeCatalog(file,{},"length",zoom,token,qRgb(90,220,170),&baseRaster);
 if(changedZoom.incremental||changedZoom.image!=zoomFull.image){std::cerr<<"FAIL changed zoom raster parity\n";return 18;}
 // Query-order independence and exact inclusive Y clipping: no source point
 // is averaged, sampled, deleted or moved. Reverse fixture order on purpose.
 auto narrow=directView;narrow.minY=25;narrow.maxY=26;
 std::vector<pgn::ui::DensePoint> reverse;
 for(int i=4999;i>=0;--i)reverse.push_back({1000+(i%8)*2.0,double(19+i%15),qRgb(90,220,170)});
 pgn::ui::DensePointLayer expected;expected.rebuild(narrow,reverse);
 const auto filtered=pgn::ui::renderNativeCatalog(file,{},"length",narrow,token,qRgb(90,220,170));
 if(!filtered.complete||filtered.points!=qint64(expected.visibleCount())||filtered.image!=expected.image()||filtered.rowsRead!=filtered.points){std::cerr<<"FAIL exact unsorted Y viewport parity\n";return 19;}
 auto narrowOld=narrow;narrowOld.revision=4990;
 const auto narrowBase=pgn::ui::renderNativeCatalog(file,{},"length",narrowOld,token,qRgb(90,220,170));
 const auto narrowAppend=pgn::ui::renderNativeCatalog(file,{},"length",narrow,token,qRgb(90,220,170),&narrowBase);
 if(!narrowAppend.complete||!narrowAppend.incremental||narrowAppend.points!=filtered.points||narrowAppend.image!=filtered.image){std::cerr<<"FAIL Y-clipped incremental parity\n";return 20;}
 token->store(true);if(pgn::ui::renderNativeCatalog(file,b,"length",directView,token,qRgb(90,220,170)).complete)return 5;
 std::unique_ptr<QWidget> canvas(pgn::ui::createMiningCanvas());canvas->resize(1380,850);
 pgn::ui::bindNativeCatalog(canvas.get(),file);
  pgn::ui::selectNativeGraphAxis(canvas.get(),"length");canvas->show();app.processEvents();
  if(!canvas->property("pgn_native_loading").toBool()||
     canvas->property("pgn_native_visible_points").toLongLong()!=0||
     !canvas->property("pgn_graph_x_ticks").toList().isEmpty()||
     canvas->property("pgn_native_display_revision").toULongLong()!=0){
      std::cerr<<"FAIL loading catalogue must not show a cached subset as FULL HISTORY\n";return 95;
  }
 FrontierTelemetrySnapshot lane;lane.runId=b;lane.gpuUuid="GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";lane.gpuName="RTX 3090";
 lane.authoritative=true;lane.state="running";lane.observedUtc=QDateTime::currentDateTimeUtc();lane.sourceMode="live";
 auto bounds=[](qint64 points,double minX,double maxX){return QJsonObject{{"points",points},{"revision",points},{"min_x",minX},{"max_x",maxX},{"min_y",19},{"max_y",33}};};
 pgn::ui::presentNativeIndex(canvas.get(),{{"complete",false},{"archive_only",true},{"error","closed fixture archive unavailable"}});
 if(canvas->property("pgn_catalog_archive_error").toString().isEmpty()||canvas->property("pgn_catalog_total_points").isValid())return 90;
 pgn::ui::presentNativeIndex(canvas.get(),{{"complete",true},{"archive_only",true},
  {"all",QJsonObject{{"length",bounds(5000,1000,1014)},{"time",bounds(5000,base,base+4999000)}}}});
 if(!canvas->property("pgn_catalog_archive_loaded").toBool()||canvas->property("pgn_catalog_source_index_seen").toBool()||
    canvas->property("pgn_catalog_total_points").toLongLong()!=5000||canvas->property("pgn_catalog_current_scope").isValid()||
    pgn::ui::catalogDiagnostics(canvas.get()).value("current").toObject().value("ready").toBool())return 91;
 pgn::ui::selectNativeGraphAxis(canvas.get(),"length");canvas->show();
 {QElapsedTimer deadline;deadline.start();do{app.processEvents();QThread::msleep(1);}while((canvas->property("pgn_native_visible_points").toLongLong()!=5000||canvas->property("pgn_native_loading").toBool())&&deadline.elapsed()<10000);
  if(canvas->property("pgn_native_visible_points").toLongLong()!=5000||canvas->property("pgn_native_loading").toBool())return 94;}
 canvas->hide();pgn::ui::selectNativeGraphAxis(canvas.get(),"time");pgn::ui::presentDevice(canvas.get(),lane);
 // Number Lens has no animation timer and no rich point cache. Its first
 // complete indexed frame must appear through real QWidget update events,
 // not a test repeatedly forcing paint or the Mining timer rescuing it.
 {
  std::unique_ptr<QWidget> lens(pgn::ui::createNumberLensCanvas());lens->resize(1380,780);
  pgn::ui::bindNativeCatalog(lens.get(),file);
  pgn::ui::presentNativeIndex(lens.get(),{{"complete",true},{"archive_only",true},
   {"all",QJsonObject{{"length",bounds(5000,1000,1014)},{"time",bounds(5000,base,base+4999000)}}}});
  lens->show();
  const auto waitLens=[&](qint64 points){QElapsedTimer deadline;deadline.start();
   do{app.processEvents();QThread::msleep(1);}while((lens->property("pgn_native_visible_points").toLongLong()!=points||lens->property("pgn_native_loading").toBool())&&deadline.elapsed()<10000);
   return lens->property("pgn_native_visible_points").toLongLong()==points&&!lens->property("pgn_native_loading").toBool();};
  if(!waitLens(5000)){std::cerr<<"FAIL Number Lens first frame without forced paint: "<<QJsonDocument(pgn::ui::catalogDiagnostics(lens.get())).toJson(QJsonDocument::Compact).toStdString()<<"\n";return 61;}
  if(lens->property("pgn_catalog_source_index_seen").toBool()||lens->property("pgn_catalog_current_scope").isValid())return 93;
  pgn::ui::presentDevice(lens.get(),lane);
  pgn::ui::presentNativeIndex(lens.get(),{{"complete",true},
   {"all",QJsonObject{{"length",bounds(5000,1000,1014)},{"time",bounds(5000,base,base+4999000)}}},
   {"current",QJsonObject{{"scope",b},{"length",bounds(2500,1000,1014)},{"time",bounds(2500,base+2500000,base+4999000)}}}});
  const QRectF lensPlot=lens->property("pgn_graph_plot_rectangle").toRect();
  const auto clickLens=[&](const QRectF& hit){QMouseEvent e(QEvent::MouseButtonPress,hit.center(),lens->mapToGlobal(hit.center().toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(lens.get(),&e);};
  clickLens(HorizonGraphControls::current(lensPlot));if(!waitLens(2500))return 62;
  clickLens(HorizonGraphControls::history(lensPlot));if(!waitLens(5000))return 63;
  clickLens(HorizonGraphControls::axis(lensPlot,1));if(!waitLens(5000))return 64;
  clickLens(HorizonGraphControls::axis(lensPlot,0));if(!waitLens(5000))return 65;
  clickLens(HorizonGraphControls::timeRangeButton(lensPlot,1));if(!waitLens(3601))return 66;
  clickLens(HorizonGraphControls::reset(lensPlot));if(!waitLens(5000))return 67;
 }
 pgn::ui::presentNativeIndex(canvas.get(),{{"complete",true},
  {"all",QJsonObject{{"length",bounds(5000,1000,1014)},{"time",bounds(5000,base,base+4999000)}}},
  {"current",QJsonObject{{"scope",b},{"length",bounds(2500,1000,1014)},{"time",bounds(2500,base+2500000,base+4999000)}}}});
 pgn::ui::presentNativeIndex(canvas.get(),{{"complete",true},{"archive_only",true},{"all",QJsonObject{{"length",bounds(1,1000,1000)},{"time",bounds(1,base,base)}}}});
 pgn::ui::presentNativeIndex(canvas.get(),{{"complete",false},{"archive_only",true},{"error","late obsolete bootstrap failure"}});
 if(canvas->property("pgn_catalog_total_points").toLongLong()!=5000||canvas->property("pgn_catalog_current_scope").toString()!=b||
    !canvas->property("pgn_catalog_archive_error").toString().isEmpty())return 92;
 pgn::ui::beginHistoryBatch(canvas.get());
 for(int i=4000;i<5000;++i){FrontierGapEvent e;e.runId=b;e.gpuUuid=lane.gpuUuid;e.domainId="pgn-durable-native-history";
  e.endpointLeft=QString::number(1000000+i*2000);e.gap=QString::number(1000+(i%8)*2);e.endpointRight=QString::number(1000000+i*2000+e.gap.toInt());
  e.candidateId=e.endpointLeft+":"+e.endpointRight;e.merit=QString::number(19+i%15);e.observedUtc=QDateTime::fromMSecsSinceEpoch(base+i*1000,QTimeZone::utc());
  e.proofState="prp_pending";pgn::ui::presentGap(canvas.get(),e);}
 pgn::ui::endHistoryBatch(canvas.get());
 if(canvas->property("pgn_rich_cache_points").toInt()!=512||canvas->property("pgn_archive_pairs").toLongLong()!=5000)return 6;
 QImage image(canvas->size(),QImage::Format_ARGB32_Premultiplied);
 auto render=[&]{image.fill(Qt::transparent);QPainter p(&image);canvas->render(&p);};
 auto wait=[&](qint64 points){QElapsedTimer deadline;deadline.start();do{app.processEvents();render();QThread::msleep(1);}while((canvas->property("pgn_native_visible_points").toLongLong()!=points||canvas->property("pgn_native_loading").toBool())&&deadline.elapsed()<10000);return canvas->property("pgn_native_visible_points").toLongLong()==points&&!canvas->property("pgn_native_loading").toBool();};
 if(!wait(5000))return 7;
 const auto contentFits=[&](double first,double last){
  const auto v=canvas->property("pgn_graph_presented_bounds").toList();
  return v.size()==4&&v[0].toDouble()==first&&v[1].toDouble()==last&&
         v[2].toDouble()>18.5&&v[2].toDouble()<=19.&&
         v[3].toDouble()>=33.&&v[3].toDouble()<34.;
 };
 if(!contentFits(base,base+4999000)){std::cerr<<"FAIL ALL TIME tight content bounds\n";return 68;}
 const QRectF plot=canvas->property("pgn_graph_plot_rectangle").toRect();
 auto click=[&](const QRectF& hit){QMouseEvent e(QEvent::MouseButtonPress,hit.center(),canvas->mapToGlobal(hit.center().toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas.get(),&e);};
 click(HorizonGraphControls::current(plot));if(!wait(2500))return 8;
 click(HorizonGraphControls::axis(plot,1));if(!wait(2500))return 9;
 if(!contentFits(1000.,1014.)){std::cerr<<"FAIL LENGTH tight content bounds\n";return 69;}
 click(HorizonGraphControls::history(plot));if(!wait(5000))return 10;
 render();if(!canvas->property("pgn_plot_cache_hit").toBool())return 11;
 const auto pixels=image.copy(plot.toAlignedRect());render();if(image.copy(plot.toAlignedRect())!=pixels)return 12;
 click(HorizonGraphControls::axis(plot,0));if(!wait(5000))return 15;
 for(int index=0;index<static_cast<int>(FrontierTimeRangePresetCount);++index){click(HorizonGraphControls::timeRangeButton(plot,index));
  const auto& preset=FrontierTimeRangePresets[index];const qint64 expected=preset.allHistory?5000:std::min(qint64(5000),preset.spanMs/1000+1);
  if(!wait(expected)){std::cerr<<"FAIL time preset "<<index<<" did not complete, expected "<<expected<<"\n";return 16;}}
 click(HorizonGraphControls::timeRangeButton(plot,1));if(!wait(3601))return 17;
 click(HorizonGraphControls::reset(plot));if(!wait(5000)||canvas->property("pgn_graph_time_preset").toInt()!=static_cast<int>(FrontierTimeRangePresetCount)-1){std::cerr<<"FAIL reset: points="<<canvas->property("pgn_native_visible_points").toLongLong()<<" preset="<<canvas->property("pgn_graph_time_preset").toInt()<<"\n";return 18;}
 // Continuous metadata invalidation must not replace the displayed frame's
 // caption with a different loading paragraph on every admission. Snapshot
 // the actual paint region, then render BEFORE any worker completion event.
 render();
 const QRect caption=HorizonGraphControls::frameCaption(plot).toAlignedRect();
 for(int width:{900,1000,1200,1500}){
  const QRectF area(20,30,width,300),slot=HorizonGraphControls::frameCaption(area);
  if(slot.intersects(HorizonGraphControls::axis(area,0))||slot.intersects(HorizonGraphControls::axis(area,1))||
     slot.intersects(HorizonGraphControls::current(area))||slot.intersects(HorizonGraphControls::history(area))||slot.intersects(HorizonGraphControls::reset(area)))return 25;
  for(int index=0;index<int(FrontierTimeRangePresetCount);++index)if(slot.intersects(HorizonGraphControls::timeRangeButton(area,index)))return 26;
 }
 pgn::ui::CatalogRaster captionFixture;captionFixture.complete=true;captionFixture.points=0;captionFixture.view.revision=123;
 if(!pgn::ui::catalogFrameCaption(&captionFixture,{}).contains("0 POINTS")||
    !pgn::ui::catalogFrameCaption(&captionFixture,"failed").contains("INDEX ERROR")||
    !pgn::ui::catalogFrameCaption(nullptr,"failed").contains("INDEX ERROR"))return 27;
 const auto displayedCaption=image.copy(caption);
 const auto displayedCaptionText=canvas->property("pgn_native_frame_caption").toString();
 const auto displayedPoints=image.copy(plot.adjusted(88,66,-70,-48).toAlignedRect());
 {auto db=QSqlDatabase::addDatabase("QSQLITE","native-live");db.setDatabaseName(file);if(!db.open())return 21;
  {QSqlQuery q(db);q.prepare("INSERT INTO native_plot_coordinates VALUES(?,?,?,?,?,?,?)");
   for(const QString& scope:{QStringLiteral(""),b})for(const QString& axis:{QStringLiteral("time"),QStringLiteral("length")}){
    const double x=axis=="time"?base+4999500.0:1002;
    const auto bits=std::bit_cast<std::uint64_t>(x)^(std::uint64_t{1}<<63);
    q.bindValue(0,scope);q.bindValue(1,axis);q.bindValue(2,QString::number(bits,16).rightJustified(16,'0')+QString::number(5001,16).rightJustified(16,'0'));
    q.bindValue(3,5001);q.bindValue(4,x);q.bindValue(5,23);q.bindValue(6,scope.isEmpty()?5001:2501);if(!q.exec())return 22;
   }}db.close();}QSqlDatabase::removeDatabase("native-live");
 pgn::ui::presentNativeIndex(canvas.get(),{{"complete",true},
  {"all",QJsonObject{{"length",bounds(5001,1000,1014)},{"time",bounds(5001,base,base+4999500)}}},
  {"current",QJsonObject{{"scope",b},{"length",bounds(2501,1000,1014)},{"time",bounds(2501,base+2500000,base+4999500)}}}});
 render();
 if(!canvas->property("pgn_native_loading").toBool()||canvas->property("pgn_native_visible_points").toLongLong()!=5000||
    image.copy(caption)!=displayedCaption||canvas->property("pgn_native_frame_caption").toString()!=displayedCaptionText||
    canvas->property("pgn_native_display_revision").toLongLong()!=5000||canvas->property("pgn_native_target_revision").toLongLong()!=5001||
    image.copy(plot.adjusted(88,66,-70,-48).toAlignedRect())!=displayedPoints){
  std::cerr<<"FAIL continuous admission flicker: completed points/caption must remain byte-identical while next exact frame is pending\n";return 23;}
 if(!wait(5001)||canvas->property("pgn_native_display_revision").toLongLong()!=5001||
    canvas->property("pgn_native_target_revision").toLongLong()!=5001)return 24;
 // The previous admission stayed inside the existing axes. Real streaming
 // input also crosses the auto-fit minute boundary: the complete pixels and
 // their axes must remain one atomic frame, not a temporarily rescaled image.
 const auto beforeBoundaryPoints=image.copy(plot.adjusted(88,66,-70,-48).toAlignedRect());
 const auto beforeBoundaryCaption=canvas->property("pgn_native_frame_caption").toString();
 {auto db=QSqlDatabase::addDatabase("QSQLITE","native-boundary");db.setDatabaseName(file);if(!db.open())return 25;
  {QSqlQuery q(db);db.transaction();q.prepare("INSERT INTO native_plot_coordinates VALUES(?,?,?,?,?,?,?)");
   for(const QString& scope:{QStringLiteral(""),b})for(const QString& axis:{QStringLiteral("time"),QStringLiteral("length")}){
    const double x=axis=="time"?base+5059500.0:1000.0;
    const auto bits=std::bit_cast<std::uint64_t>(x)^(std::uint64_t{1}<<63);
    q.bindValue(0,scope);q.bindValue(1,axis);q.bindValue(2,QString::number(bits,16).rightJustified(16,'0')+QString::number(5002,16).rightJustified(16,'0'));
    q.bindValue(3,5002);q.bindValue(4,x);q.bindValue(5,23);q.bindValue(6,scope.isEmpty()?5002:2502);if(!q.exec())return 26;
   }db.commit();}db.close();}QSqlDatabase::removeDatabase("native-boundary");
 pgn::ui::presentNativeIndex(canvas.get(),QJsonObject{{"complete",true},
  {"all",QJsonObject{{"length",bounds(5002,1000,1014)},{"time",bounds(5002,base,base+5059500)}}},
  {"current",QJsonObject{{"scope",b},{"length",bounds(2502,1000,1014)},{"time",bounds(2502,base+2500000,base+5059500)}}}});
 render();
 if(!canvas->property("pgn_native_loading").toBool()||canvas->property("pgn_native_visible_points").toLongLong()!=5001||
    canvas->property("pgn_native_frame_caption").toString()!=beforeBoundaryCaption||
    image.copy(plot.adjusted(88,66,-70,-48).toAlignedRect())!=beforeBoundaryPoints){
  std::cerr<<"FAIL auto-fit boundary flash: old complete pixels were rescaled before new complete frame\n";return 27;}
 if(!wait(5002)||canvas->property("pgn_native_display_revision").toLongLong()!=5002)return 28;
 // Repeated stream admissions change BOTH time and merit auto-fit geometry.
 // Each pending paint must preserve pixels AND axes, then publish every new
 // point at its exact completed revision. One successful transition is not a
 // substitute for this adversarial sequence.
 for(int step=0;step<8;++step){
  const int revision=5003+step;const double latestTime=base+5119500.0+step*60000.0;
  const auto oldPixels=image.copy(plot.adjusted(88,66,-70,-48).toAlignedRect());
  const double oldMaxX=canvas->property("pgn_native_display_max_x").toDouble();
  const double oldMaxY=canvas->property("pgn_native_display_max_y").toDouble();
  const auto oldCaption=canvas->property("pgn_native_frame_caption").toString();
  {auto db=QSqlDatabase::addDatabase("QSQLITE","native-sequence");db.setDatabaseName(file);if(!db.open())return 29;
   {QSqlQuery q(db);db.transaction();q.prepare("INSERT INTO native_plot_coordinates VALUES(?,?,?,?,?,?,?)");
    for(const QString& scope:{QStringLiteral(""),b})for(const QString& axis:{QStringLiteral("time"),QStringLiteral("length")}){
     const double x=axis=="time"?latestTime:1000.0;
     const auto bits=std::bit_cast<std::uint64_t>(x)^(std::uint64_t{1}<<63);
     q.bindValue(0,scope);q.bindValue(1,axis);q.bindValue(2,QString::number(bits,16).rightJustified(16,'0')+QString::number(revision,16).rightJustified(16,'0'));
     q.bindValue(3,revision);q.bindValue(4,x);q.bindValue(5,34+step);q.bindValue(6,scope.isEmpty()?revision:revision-2500);if(!q.exec())return 30;
    }if(!db.commit())return 31;}db.close();}QSqlDatabase::removeDatabase("native-sequence");
  auto allTime=bounds(revision,base,latestTime);allTime["max_y"]=34+step;
  auto currentTime=bounds(revision-2500,base+2500000,latestTime);currentTime["max_y"]=34+step;
  auto allLength=bounds(revision,1000,1014);allLength["max_y"]=34+step;
  auto currentLength=bounds(revision-2500,1000,1014);currentLength["max_y"]=34+step;
  pgn::ui::presentNativeIndex(canvas.get(),QJsonObject{{"complete",true},
   {"all",QJsonObject{{"length",allLength},{"time",allTime}}},
   {"current",QJsonObject{{"scope",b},{"length",currentLength},{"time",currentTime}}}});
  render();
  if(!canvas->property("pgn_native_loading").toBool()||
     canvas->property("pgn_native_display_revision").toInt()!=revision-1||
     canvas->property("pgn_native_target_revision").toInt()!=revision||
     canvas->property("pgn_native_display_max_x").toDouble()!=oldMaxX||
     canvas->property("pgn_native_display_max_y").toDouble()!=oldMaxY||
     canvas->property("pgn_native_frame_caption").toString()!=oldCaption||
     image.copy(plot.adjusted(88,66,-70,-48).toAlignedRect())!=oldPixels){
   std::cerr<<"FAIL repeated complete-frame transition at revision "<<revision<<"\n";return 32;
  }
  if(!wait(revision)||canvas->property("pgn_native_display_revision").toInt()!=revision||
     canvas->property("pgn_native_display_max_x").toDouble()<=oldMaxX){
   std::cerr<<"FAIL stream worker did not publish exact new geometry at revision "<<revision
    <<" points="<<canvas->property("pgn_native_visible_points").toLongLong()
    <<" display="<<canvas->property("pgn_native_display_revision").toInt()
    <<" target="<<canvas->property("pgn_native_target_revision").toInt()
    <<" loading="<<canvas->property("pgn_native_loading").toBool()
    <<" oldX="<<std::fixed<<oldMaxX<<" newX="<<canvas->property("pgn_native_display_max_x").toDouble()
    <<" error="<<canvas->property("pgn_native_error").toString().toStdString()<<"\n";return 33;
  }
 }
 // An exact compressed prefix must conserve pixels/counts with a raw suffix,
 // including an older requested revision and incremental redraws.
 const auto cacheToken=std::make_shared<std::atomic_bool>(false);
 const pgn::ui::DenseView cacheView{999,1015,18,50,QSize(640,210),5010};
 const auto rawCacheOracle=pgn::ui::renderNativeCatalog(file,{},"length",cacheView,cacheToken,qRgb(90,220,170));
 if(!rawCacheOracle.complete||rawCacheOracle.points!=5010)return 68;
 {auto db=QSqlDatabase::addDatabase("QSQLITE","native-compressed-prefix");db.setDatabaseName(file);if(!db.open())return 69;
  {QSqlQuery q(db);if(!q.exec("CREATE TABLE native_plot_multiplicity(scope TEXT,axis TEXT,x REAL,y REAL,multiplicity INTEGER,PRIMARY KEY(scope,axis,x,y)) WITHOUT ROWID")||
    !q.exec("CREATE TABLE native_plot_multiplicity_manifest(scope TEXT,axis TEXT,base_revision INTEGER,points INTEGER,cells INTEGER,PRIMARY KEY(scope,axis)) WITHOUT ROWID")||
    !q.exec("INSERT INTO native_plot_multiplicity SELECT scope,axis,x,y,COUNT(*) FROM native_plot_coordinates WHERE scope='' AND admission_revision<=5005 GROUP BY scope,axis,x,y")||
    !q.exec("INSERT INTO native_plot_multiplicity_manifest SELECT scope,axis,5005,SUM(multiplicity),COUNT(*) FROM native_plot_multiplicity GROUP BY scope,axis"))return 70;
  }db.close();}QSqlDatabase::removeDatabase("native-compressed-prefix");
 const auto compressed=pgn::ui::renderNativeCatalog(file,{},"length",cacheView,cacheToken,qRgb(90,220,170));
 if(!compressed.complete||compressed.points!=rawCacheOracle.points||compressed.image!=rawCacheOracle.image||compressed.rowsRead>=rawCacheOracle.rowsRead)return 71;
 auto baseView=cacheView;baseView.revision=5005;
 const auto compressedBase=pgn::ui::renderNativeCatalog(file,{},"length",baseView,cacheToken,qRgb(90,220,170));
 const auto compressedAppend=pgn::ui::renderNativeCatalog(file,{},"length",cacheView,cacheToken,qRgb(90,220,170),&compressedBase);
 if(!compressedAppend.complete||!compressedAppend.incremental||compressedAppend.points!=5010||compressedAppend.image!=rawCacheOracle.image||compressedAppend.rowsRead!=5)return 72;
 auto olderView=cacheView;olderView.revision=5000;
 const auto olderRaster=pgn::ui::renderNativeCatalog(file,{},"length",olderView,cacheToken,qRgb(90,220,170));
 if(!olderRaster.complete||olderRaster.points!=5000||olderRaster.rowsRead!=5000)return 73;
 cacheToken->store(true);
 if(pgn::ui::renderNativeCatalog(file,{},"length",cacheView,cacheToken,qRgb(90,220,170)).complete)return 74;
 // New native TIME window discriminator: both outliers are absent from the
 // 512 rich-record cache. These are public synthetic fixture coordinates, not
 // reconstructed production event dates. The 1000-merit point is OUTSIDE 1H;
 // the 100-merit point is INSIDE 1H and must remain visible.
 const double finalTime=base+5119500.0+7*60000.0;
 {auto db=QSqlDatabase::addDatabase("QSQLITE","native-window-outliers");db.setDatabaseName(file);if(!db.open())return 82;
  {QSqlQuery q(db);q.prepare("INSERT INTO native_plot_coordinates VALUES(?,?,?,?,?,?,?)");
   for(int n=0;n<2;++n){const int id=5011+n;const double x=base+(n?1000000.0:3000000.0),y=n?1000.0:100.0;
    const auto bits=std::bit_cast<std::uint64_t>(x)^(std::uint64_t{1}<<63);
    for(const QString& scope:{QStringLiteral(""),b}){
     q.bindValue(0,scope);q.bindValue(1,"time");q.bindValue(2,QString::number(bits,16).rightJustified(16,'0')+QString::number(id,16).rightJustified(16,'0'));
     q.bindValue(3,id);q.bindValue(4,x);q.bindValue(5,y);q.bindValue(6,scope.isEmpty()?id:id-2500);if(!q.exec())return 83;
    }
   }
  }db.close();}QSqlDatabase::removeDatabase("native-window-outliers");
 auto allFinal=bounds(5012,base,finalTime);allFinal["max_y"]=1000.;
 auto currentFinal=bounds(2512,base+1000000.,finalTime);currentFinal["max_y"]=1000.;
 pgn::ui::presentNativeIndex(canvas.get(),{{"complete",true},
  {"all",QJsonObject{{"length",bounds(5010,1000,1014)},{"time",allFinal}}},
  {"current",QJsonObject{{"scope",b},{"length",bounds(2510,1000,1014)},{"time",currentFinal}}}});
 click(HorizonGraphControls::timeRangeButton(plot,1));
 {QElapsedTimer deadline;deadline.start();do{app.processEvents();render();QThread::msleep(1);}
  while((canvas->property("pgn_native_loading").toBool()||canvas->property("pgn_native_display_revision").toInt()!=5012)&&deadline.elapsed()<10000);
 }
 const auto fittedWindow=canvas->property("pgn_graph_presented_bounds").toList();
 if(fittedWindow.size()!=4||fittedWindow[2].toDouble()>19.||fittedWindow[3].toDouble()<100.||fittedWindow[3].toDouble()>105.){
  std::cerr<<"FAIL exact TIME Y: outside-X outlier inflated view or uncached inside-X outlier disappeared; bounds="
   <<QJsonDocument::fromVariant(fittedWindow).toJson(QJsonDocument::Compact).toStdString()<<"\n";return 84;
 }
 if(canvas->property("pgn_rich_cache_points").toInt()!=512)return 85;
 if(!canvas->property("pgn_native_window_fit_requested").toBool())return 86;
 const auto waitRevision=[&](int revision){QElapsedTimer deadline;deadline.start();
  do{app.processEvents();render();QThread::msleep(1);}while((canvas->property("pgn_native_loading").toBool()||
   canvas->property("pgn_native_display_revision").toInt()!=revision)&&deadline.elapsed()<10000);
  return !canvas->property("pgn_native_loading").toBool()&&canvas->property("pgn_native_display_revision").toInt()==revision;
 };
 const auto anchor=plot.adjusted(88,66,-70,-48).center();
 QWheelEvent wheel(anchor,canvas->mapToGlobal(anchor.toPoint()),QPoint{},QPoint{0,120},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
 QApplication::sendEvent(canvas.get(),&wheel);render();
 if(!waitRevision(5012)||canvas->property("pgn_native_window_fit_requested").toBool())return 87;
 const auto manualBounds=canvas->property("pgn_graph_presented_bounds").toList();
 if(manualBounds==fittedWindow)return 88;
 // A real new admission OUTSIDE the manual view changes global Y metadata.
 // It must not replace manual axes with a fresh automatic fit.
 {auto db=QSqlDatabase::addDatabase("QSQLITE","native-manual-append");db.setDatabaseName(file);if(!db.open())return 89;
  {QSqlQuery q(db);q.prepare("INSERT INTO native_plot_coordinates VALUES(?,?,?,?,?,?,?)");
   const double x=base+1000000.0;const auto bits=std::bit_cast<std::uint64_t>(x)^(std::uint64_t{1}<<63);
   for(const QString& scope:{QStringLiteral(""),b}){
    q.bindValue(0,scope);q.bindValue(1,"time");q.bindValue(2,QString::number(bits,16).rightJustified(16,'0')+QString::number(5013,16).rightJustified(16,'0'));
    q.bindValue(3,5013);q.bindValue(4,x);q.bindValue(5,2000.);q.bindValue(6,scope.isEmpty()?5013:2513);if(!q.exec())return 90;
   }
  }db.close();}QSqlDatabase::removeDatabase("native-manual-append");
 allFinal["points"]=5013;allFinal["revision"]=5013;allFinal["max_y"]=2000.;
 currentFinal["points"]=2513;currentFinal["revision"]=2513;currentFinal["max_y"]=2000.;
 pgn::ui::presentNativeIndex(canvas.get(),{{"complete",true},
  {"all",QJsonObject{{"length",bounds(5010,1000,1014)},{"time",allFinal}}},
  {"current",QJsonObject{{"scope",b},{"length",bounds(2510,1000,1014)},{"time",currentFinal}}}});
 if(!waitRevision(5013)||canvas->property("pgn_graph_presented_bounds").toList()!=manualBounds||
    canvas->property("pgn_native_window_fit_requested").toBool())return 91;
 click(HorizonGraphControls::reset(plot));
 if(!wait(5013)||canvas->property("pgn_native_window_fit_requested").toBool()||
    canvas->property("pgn_graph_presented_bounds").toList()[3].toDouble()<2000.)return 92;
 click(HorizonGraphControls::current(plot));if(!wait(2513))return 93;
 click(HorizonGraphControls::timeRangeButton(plot,1));if(!wait(2511))return 94;
 const auto currentWindow=canvas->property("pgn_graph_presented_bounds").toList();
 if(currentWindow[3].toDouble()<100.||currentWindow[3].toDouble()>105.||
    !canvas->property("pgn_native_window_fit_requested").toBool())return 95;
 std::cout<<"PASS exact finite TIME outlier selection with rich512, ALL/CURRENT, real wheel manual zoom preserved across admission; offscreen only\n";
 if(argc>1)image.save(QString::fromLocal8Bit(argv[1]));
 std::cout<<"PASS native complete ALL/CURRENT TIME/LENGTH, source conservation with512rich cache, cancellation, all presets/reset, exact5000..5010 stream frames with atomic pending axes/pixels/caption across time+merit boundaries\n";
}

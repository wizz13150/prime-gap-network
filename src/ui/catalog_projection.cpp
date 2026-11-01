#include "ui/catalog_projection.h"
#include "ui/render/graph_view.h"
#include <sqlite3.h>
#include <QElapsedTimer>
#include <QFileInfo>
#include <bit>
#include <cmath>
namespace pgn::ui {
namespace {
class ReadConnection {
public:
 sqlite3* db{};
 // Each worker owns its connection. NOMUTEX does not disable WAL/file locks.
 explicit ReadConnection(const QString& file){
  if(!QFileInfo::exists(file))return;
  const auto path=file.toUtf8();
  if(sqlite3_open_v2(path.constData(),&db,SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX,nullptr)!=SQLITE_OK){
   if(db)sqlite3_close(db);db=nullptr;return;
  }
  sqlite3_busy_timeout(db,1000);
 }
 ~ReadConnection(){if(db)sqlite3_close(db);}
 ReadConnection(const ReadConnection&)=delete;
 ReadConnection& operator=(const ReadConnection&)=delete;
};
class Statement {
public:
 sqlite3_stmt* value{};
 Statement(sqlite3* db,const char* sql){sqlite3_prepare_v2(db,sql,-1,&value,nullptr);}
 ~Statement(){if(value)sqlite3_finalize(value);}
 bool bind(int index,const QByteArray& text){return sqlite3_bind_text(value,index,text.constData(),text.size(),SQLITE_TRANSIENT)==SQLITE_OK;}
 Statement(const Statement&)=delete;
 Statement& operator=(const Statement&)=delete;
};
QByteArray coordinatePrefix(double x){
 const auto bits=std::bit_cast<std::uint64_t>(x==0?0.0:x),sign=std::uint64_t{1}<<63;
 const auto ordered=bits&sign?~bits:bits^sign;
 return QByteArray::number(ordered,16).rightJustified(16,'0');
}
bool axisValid(const QString& axis){return axis=="length"||axis=="time";}
}
CatalogBounds readCatalogBounds(const QString& file,const QString& axis){
 CatalogBounds result;if(!axisValid(axis)){result.error="Unknown catalogue axis";return result;}
 ReadConnection connection(file);if(!connection.db){result.error="Read-only observation catalogue unavailable";return result;}
 Statement query(connection.db,"SELECT min_x,max_x,min_y,max_y,points,revision FROM plot_bounds WHERE axis=?");
 if(!query.value||!query.bind(1,axis.toUtf8())){result.error=QString::fromUtf8(sqlite3_errmsg(connection.db));return result;}
 const int step=sqlite3_step(query.value);
 if(step==SQLITE_ROW){result.minX=sqlite3_column_double(query.value,0);result.maxX=sqlite3_column_double(query.value,1);
  result.minY=sqlite3_column_double(query.value,2);result.maxY=sqlite3_column_double(query.value,3);
  result.points=sqlite3_column_int64(query.value,4);result.revision=sqlite3_column_int64(query.value,5);}
 else if(step!=SQLITE_DONE)result.error=QString::fromUtf8(sqlite3_errmsg(connection.db));
 return result;
}
static CatalogRaster renderIndexed(const QString& file,const QString& axis,const DenseView& requestedView,
 const std::shared_ptr<std::atomic_bool>& cancelled,const QString* scope,QRgb color,const CatalogRaster* previous=nullptr,bool useCompressedPrefix=true,const CatalogYFit& yFit={}){
 auto view=requestedView;
 CatalogRaster result;result.requestedView=requestedView;result.view=view;result.yFit=yFit;result.axis=axis;result.scope=scope?*scope:QString{};result.color=color;QElapsedTimer timer;timer.start();
 if(!axisValid(axis)||!std::isfinite(view.minX)||!std::isfinite(view.maxX)||!std::isfinite(view.minY)||!std::isfinite(view.maxY)||
    view.maxX<=view.minX||view.maxY<=view.minY||view.pixels.width()<1||view.pixels.height()<1||
    view.pixels.width()>8192||view.pixels.height()>8192){result.error="Invalid catalogue view";return result;}
 if((yFit.enabled&&(!scope||axis!="time"))||(yFit.targetY&&(!std::isfinite(*yFit.targetY)||*yFit.targetY<0))){result.error="Invalid native TIME fit/target";return result;}
 ReadConnection connection(file);if(!connection.db){result.error="Read-only observation catalogue unavailable";return result;}
 // Cancellation also reaches a long SQLite scan with few visible rows, not
 // only the 256-result batching boundary below. Each worker owns this handle.
 if(cancelled){
  if(cancelled->load(std::memory_order_relaxed)){result.elapsedNs=timer.nsecsElapsed();return result;}
  sqlite3_progress_handler(connection.db,2048,[](void* data){
   return static_cast<const std::atomic_bool*>(data)->load(std::memory_order_relaxed)?1:0;
  },cancelled.get());
 }
 // Bind cache manifest, compressed cells and raw suffix to one SQLite read
 // snapshot. A concurrent cache publisher must never mix two generations.
 if(sqlite3_exec(connection.db,"BEGIN",nullptr,nullptr,nullptr)!=SQLITE_OK){result.error=QString::fromUtf8(sqlite3_errmsg(connection.db));return result;}
 if(yFit.enabled){
  // Exact X/revision selection, no Y predicate and no rich-record sampling.
  // This adds an O(window candidate rows) aggregate pass in this SAME readonly
  // snapshot/worker. fitPoints is selected COUNT(*), not physical scan work.
  Statement bounds(connection.db,"SELECT MIN(y),MAX(y),COUNT(*),COUNT(y) FROM native_plot_coordinates WHERE scope=? AND axis=? AND point_key>=? AND point_key<=? AND admission_revision<=?");
  if(!bounds.value||!bounds.bind(1,scope->toUtf8())||!bounds.bind(2,axis.toUtf8())||
     !bounds.bind(3,coordinatePrefix(view.minX)+"0000000000000000")||
     !bounds.bind(4,coordinatePrefix(view.maxX)+"ffffffffffffffff")||
     sqlite3_bind_int64(bounds.value,5,static_cast<sqlite3_int64>(view.revision))!=SQLITE_OK){result.error="Invalid native window bounds query";return result;}
  const int boundStep=sqlite3_step(bounds.value);
  if(boundStep==SQLITE_INTERRUPT&&cancelled&&cancelled->load(std::memory_order_relaxed)){result.elapsedNs=timer.nsecsElapsed();return result;}
  if(boundStep!=SQLITE_ROW){result.error=QString::fromUtf8(sqlite3_errmsg(connection.db));return result;}
  result.fitPoints=sqlite3_column_int64(bounds.value,2);
  if(result.fitPoints!=sqlite3_column_int64(bounds.value,3)){result.error="Missing native window Y coordinate";return result;}
  std::optional<double> low,high;
  if(result.fitPoints>0){
   low=sqlite3_column_double(bounds.value,0);high=sqlite3_column_double(bounds.value,1);
   if(!std::isfinite(*low)||!std::isfinite(*high)||*low<0||*high<*low){result.error="Invalid native window extrema";return result;}
  }
  if(yFit.targetY){low=low?std::min(*low,*yFit.targetY):*yFit.targetY;high=high?std::max(*high,*yFit.targetY):*yFit.targetY;}
  if(low){
   const auto fitted=FrontierContentGraphView({view.minX,view.maxX,*low,*high});
   if(!fitted){result.error="Unrepresentable native window fit";return result;}
   view.minY=fitted->minMerit;view.maxY=fitted->maxMerit;result.view=view;
  } // No native data/overlay: retain requested empty-axis fallback, not data0.
 }
 auto geometry=view;if(previous)geometry.revision=previous->view.revision;
 const bool incremental=scope&&previous&&previous->complete&&previous->axis==axis&&previous->scope==*scope&&previous->color==color&&previous->yFit==yFit&&
    previous->view==geometry&&previous->view.revision<view.revision&&previous->image.size()==view.pixels;
 DensePointLayer layer;
 if(incremental){
  if(!layer.resume(view,previous->image,static_cast<std::size_t>(previous->points))){result.error="Incremental raster base is invalid";return result;}
  result.incremental=true;
 }else layer.begin(view);
 qint64 compressedBase=-1;
 if(scope&&!incremental&&useCompressedPrefix){
  Statement exists(connection.db,"SELECT 1 FROM sqlite_master WHERE type='table' AND name='native_plot_multiplicity_manifest'");
  if(exists.value&&sqlite3_step(exists.value)==SQLITE_ROW){
   Statement manifest(connection.db,"SELECT base_revision,points FROM native_plot_multiplicity_manifest WHERE scope=? AND axis=?");
   if(!manifest.value||!manifest.bind(1,scope->toUtf8())||!manifest.bind(2,axis.toUtf8())){result.error="Invalid display cache manifest";return result;}
   const auto step=sqlite3_step(manifest.value);
   if(step==SQLITE_ROW){
    const auto base=sqlite3_column_int64(manifest.value,0),points=sqlite3_column_int64(manifest.value,1);
    // A newer cache cannot reconstruct an older requested revision. In that
    // case use the conserved raw coordinate index, not future points.
    if(base>=0&&points>=0&&static_cast<quint64>(base)<=view.revision){
     Statement cells(connection.db,"SELECT x,y,multiplicity FROM native_plot_multiplicity WHERE scope=? AND axis=? AND x>=? AND x<=? AND y>=? AND y<=?");
     if(!cells.value||!cells.bind(1,scope->toUtf8())||!cells.bind(2,axis.toUtf8())||
        sqlite3_bind_double(cells.value,3,view.minX)!=SQLITE_OK||sqlite3_bind_double(cells.value,4,view.maxX)!=SQLITE_OK||
        sqlite3_bind_double(cells.value,5,view.minY)!=SQLITE_OK||sqlite3_bind_double(cells.value,6,view.maxY)!=SQLITE_OK){result.error="Invalid display cache query";return result;}
     std::vector<DensePoint> weighted;weighted.reserve(256);int cellStep;
     while((cellStep=sqlite3_step(cells.value))==SQLITE_ROW){
      const auto count=sqlite3_column_int64(cells.value,2);
      if(count<=0){result.error="Invalid exact display multiplicity";return result;}
      ++result.rowsRead;weighted.push_back({sqlite3_column_double(cells.value,0),sqlite3_column_double(cells.value,1),color,static_cast<std::size_t>(count)});
      if(weighted.size()==256){if(cancelled&&cancelled->load(std::memory_order_relaxed)){result.elapsedNs=timer.nsecsElapsed();return result;}layer.append(weighted);weighted.clear();}
     }
     if(cellStep==SQLITE_INTERRUPT&&cancelled&&cancelled->load(std::memory_order_relaxed)){result.elapsedNs=timer.nsecsElapsed();return result;}
     if(cellStep!=SQLITE_DONE){result.error=QString::fromUtf8(sqlite3_errmsg(connection.db));return result;}
     layer.append(weighted);compressedBase=base;
    }
   }else if(step!=SQLITE_DONE){result.error="Display cache manifest read failed";return result;}
  }
 }
 const bool suffix=incremental||compressedBase>=0;
 Statement query(connection.db,suffix?
  "SELECT x,y FROM native_plot_coordinates INDEXED BY native_plot_revision WHERE scope=? AND axis=? AND admission_revision>? AND admission_revision<=? AND point_key>=? AND point_key<=? AND y>=? AND y<=?":scope?
  "SELECT x,y FROM native_plot_coordinates WHERE scope=? AND axis=? AND point_key>=? AND point_key<=? AND admission_revision<=? AND y>=? AND y<=?":
  "SELECT x,y FROM plot_coordinates WHERE axis=? AND point_key>=? AND point_key<=? AND y>=? AND y<=?");
 // A catalogue layer paints one fixed colour: overlapping footprints commute.
 // ORDER BY added a temporary sort to the revision index without changing a
 // pixel. Filter exact viewport Y at the read boundary; source rows stay intact.
 const int axisIndex=scope?2:1,minIndex=suffix?5:axisIndex+1;
 if(!query.value||(scope&&!query.bind(1,scope->toUtf8()))||!query.bind(axisIndex,axis.toUtf8())||
    !query.bind(minIndex,coordinatePrefix(view.minX)+"0000000000000000")||
    !query.bind(minIndex+1,coordinatePrefix(view.maxX)+"ffffffffffffffff")){result.error=QString::fromUtf8(sqlite3_errmsg(connection.db));return result;}
 if(scope&&sqlite3_bind_int64(query.value,suffix?4:5,static_cast<sqlite3_int64>(view.revision))!=SQLITE_OK){result.error=QString::fromUtf8(sqlite3_errmsg(connection.db));return result;}
 if(suffix&&sqlite3_bind_int64(query.value,3,incremental?static_cast<sqlite3_int64>(previous->view.revision):compressedBase)!=SQLITE_OK){result.error=QString::fromUtf8(sqlite3_errmsg(connection.db));return result;}
 const int yIndex=suffix?7:scope?6:4;
 if(sqlite3_bind_double(query.value,yIndex,view.minY)!=SQLITE_OK||sqlite3_bind_double(query.value,yIndex+1,view.maxY)!=SQLITE_OK){result.error=QString::fromUtf8(sqlite3_errmsg(connection.db));return result;}
 std::vector<DensePoint> batch;batch.reserve(256);
 int step;
 while((step=sqlite3_step(query.value))==SQLITE_ROW){
  ++result.rowsRead;
  batch.push_back({sqlite3_column_double(query.value,0),sqlite3_column_double(query.value,1),color});
  if(batch.size()==256){if(cancelled&&cancelled->load(std::memory_order_relaxed)){result.elapsedNs=timer.nsecsElapsed();return result;}
   layer.append(batch);batch.clear();}
 }
 if(step==SQLITE_INTERRUPT&&cancelled&&cancelled->load(std::memory_order_relaxed)){result.elapsedNs=timer.nsecsElapsed();return result;}
 if(step!=SQLITE_DONE){result.error=QString::fromUtf8(sqlite3_errmsg(connection.db));return result;}
 if(cancelled&&cancelled->load(std::memory_order_relaxed)){result.elapsedNs=timer.nsecsElapsed();return result;}
 layer.append(batch);layer.finish();result.image=layer.image();result.points=static_cast<qint64>(layer.visibleCount());
 result.complete=!result.image.isNull();result.elapsedNs=timer.nsecsElapsed();return result;
}
CatalogRaster renderCatalog(const QString& file,const QString& axis,const DenseView& view,
 const std::shared_ptr<std::atomic_bool>& cancelled){return renderIndexed(file,axis,view,cancelled,nullptr,ImportedObservationColor);}
CatalogRaster renderNativeCatalog(const QString& file,const QString& scope,const QString& axis,const DenseView& view,
 const std::shared_ptr<std::atomic_bool>& cancelled,QRgb color,const CatalogRaster* previous,bool useCompressedPrefix,const CatalogYFit& yFit){return renderIndexed(file,axis,view,cancelled,&scope,color,previous,useCompressedPrefix,yFit);}
}

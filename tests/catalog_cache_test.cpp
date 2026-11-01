#include "ui/catalog_layer.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <sqlite3.h>
#include <algorithm>
#include <bit>
#include <iostream>

namespace {
QByteArray key(double x,int id){
    const auto bits=std::bit_cast<std::uint64_t>(x)^(std::uint64_t{1}<<63);
    return QByteArray::number(bits,16).rightJustified(16,'0')+
           QByteArray::number(id,16).rightJustified(16,'0');
}
bool wait(pgn::ui::CatalogLayer& layer,const QString& axis,const pgn::ui::DenseView& view,const pgn::ui::CatalogYFit& fit={}){
    QElapsedTimer elapsed;elapsed.start();
    while(!layer.raster(axis,view,fit)&&elapsed.elapsed()<5000){
        QCoreApplication::processEvents();QThread::msleep(1);
    }
    return layer.raster(axis,view,fit)!=nullptr;
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QTemporaryDir temporary;
    const auto file=temporary.path()+"/cache.sqlite";sqlite3* db=nullptr;
    if(sqlite3_open(file.toUtf8().constData(),&db)!=SQLITE_OK)return 1;
    const char* schema="PRAGMA journal_mode=WAL; CREATE TABLE native_plot_coordinates(scope TEXT,axis TEXT,point_key TEXT,gap_id INTEGER,x REAL,y REAL,admission_revision INTEGER,PRIMARY KEY(scope,axis,point_key)) WITHOUT ROWID; CREATE INDEX native_plot_revision ON native_plot_coordinates(scope,axis,admission_revision,point_key); BEGIN";
    if(sqlite3_exec(db,schema,nullptr,nullptr,nullptr)!=SQLITE_OK)return 2;
    sqlite3_stmt* insert=nullptr;
    if(sqlite3_prepare_v2(db,"INSERT INTO native_plot_coordinates VALUES('',?,?,?,?,?,?)",-1,&insert,nullptr)!=SQLITE_OK)return 3;
    const auto add=[&](int id,double x,std::optional<double> y=std::nullopt){
        for(const QByteArray axis:{QByteArray("time"),QByteArray("length")}){
            const auto identity=key(x,id);
            sqlite3_bind_text(insert,1,axis.constData(),axis.size(),SQLITE_TRANSIENT);
            sqlite3_bind_text(insert,2,identity.constData(),identity.size(),SQLITE_TRANSIENT);
            sqlite3_bind_int(insert,3,id);sqlite3_bind_double(insert,4,x);
            sqlite3_bind_double(insert,5,y.value_or(20+id%7));sqlite3_bind_int(insert,6,id);
            if(sqlite3_step(insert)!=SQLITE_DONE)return false;
            sqlite3_reset(insert);sqlite3_clear_bindings(insert);
        }return true;
    };
    for(int i=1;i<=1000;++i)if(!add(i,i))return 4;
    if(sqlite3_exec(db,"COMMIT",nullptr,nullptr,nullptr)!=SQLITE_OK)return 5;
    pgn::ui::CatalogLayer layer(file,[]{},nullptr,true,qRgb(90,220,170));
    auto bounds=pgn::ui::CatalogBounds{0,1001,18,30,1000,1000,{}};
    layer.publishBounds({},bounds,bounds);
    const pgn::ui::DenseView full{0,1001,18,30,QSize(900,300),1000};
    auto half=full;half.minX=500;
    layer.request("time",full);if(!wait(layer,"time",full))return 6;
    const auto original=layer.raster("time",full)->image;
    layer.request("time",half);if(!wait(layer,"time",half))return 7;
    layer.request("length",full);if(!wait(layer,"length",full))return 8;
    const auto hits=layer.cacheHits();layer.request("time",full);
    if(!layer.raster("time",full)||layer.busy()||layer.cacheHits()!=hits+1||
       layer.raster("time",full)->image!=original)return 9;
    // Restore cached A while an uncached B worker is pending. Cancel B without
    // launching or publishing a redundant A revision when B completes.
    auto uncached=full;uncached.minX=123;
    layer.request("time",uncached);layer.request("time",full);
    if(!layer.raster("time",full))return 10;
    QElapsedTimer timer;timer.start();
    while(layer.busy()&&timer.elapsed()<5000){QCoreApplication::processEvents();QThread::msleep(1);}
    if(layer.busy()||!layer.raster("time",full))return 11;
    if(!add(1001,501))return 12;
    ++bounds.points;++bounds.revision;layer.publishBounds({},bounds,bounds);
    auto newer=half;++newer.revision;
    layer.request("time",newer);if(!wait(layer,"time",newer))return 13;
    const auto* delta=layer.raster("time",newer);
    const auto exact=pgn::ui::renderNativeCatalog(file,{},"time",newer,{},qRgb(90,220,170));
    if(!delta->incremental||delta->rowsRead!=1||delta->points!=exact.points||delta->image!=exact.image)return 14;
    // Many view geometries cannot grow memory or silently evict source rows.
    for(int n=0;n<10;++n){auto v=newer;v.minX=n*20;
        layer.request("time",v);if(!wait(layer,"time",v))return 15;
        if(layer.cachedFrames()>layer.CacheFrameLimit||layer.cacheBytes()>layer.CacheByteLimit)return 16;
    }
    layer.publishBounds(QString(32,'a'),bounds,bounds);
    if(layer.cachedFrames()!=0||layer.lastComplete("time"))return 17;
    // New producer revisions can arrive before the first full-frame callback.
    // Its exact old prefix must finish once, remain labelled with revision1000,
    // then feed the exact10-row delta. Never cancel/restart the full scan solely
    // because new observations were admitted. No counters are smoothed.
    for(int id=1002;id<=1010;++id)if(!add(id,501+id%20))return 18;
    std::vector<std::uint64_t> completed;
    pgn::ui::CatalogLayer* observed=nullptr;
    pgn::ui::CatalogLayer live(file,[&]{
        if(observed)if(const auto* frame=observed->lastComplete("time"))completed.push_back(frame->view.revision);
    },nullptr,true,qRgb(90,220,170));
    observed=&live;
    auto olderBounds=bounds;olderBounds.points=1000;olderBounds.revision=1000;
    live.publishBounds({},olderBounds,olderBounds);live.request("time",full);
    auto newestBounds=olderBounds;newestBounds.points=1010;newestBounds.revision=1010;
    live.publishBounds({},newestBounds,newestBounds);auto newest=full;newest.revision=1010;
    live.request("time",newest);
    if(!wait(live,"time",newest))return 19;
    const auto* newestFrame=live.raster("time",newest);
    const auto newestExact=pgn::ui::renderNativeCatalog(file,{},"time",newest,{},qRgb(90,220,170));
    if(std::find(completed.begin(),completed.end(),1000)==completed.end()||
       !newestFrame->incremental||newestFrame->rowsRead!=10||
       newestFrame->image!=newestExact.image||newestFrame->points!=newestExact.points)return 20;
    // A regression is not an append: invalidate the old scope's prefix/cache.
    live.publishBounds({},olderBounds,olderBounds);
    if(live.lastComplete("time")||live.cachedFrames()!=0)return 21;
    // New exact finite-TIME contract. All combinations exercise the SAME guard
    // used by the current PGN factories, including populated legacy staircase.
    for(int flags=0;flags<16;++flags){
        const bool time=flags&1,finite=flags&2,manual=flags&4,stairs=flags&8;
        if(pgn::ui::nativeWindowYFitAllowed(time,finite,manual,stairs)!=(time&&finite&&!manual&&!stairs))return 30;
    }
    pgn::ui::CatalogLayer fitted(file,[]{},nullptr,true,qRgb(90,220,170));
    auto fitBounds=newestBounds;fitted.publishBounds({},fitBounds,fitBounds);
    auto window=newest;window.minX=500;window.maxX=600;
    const pgn::ui::CatalogYFit fit{true,{}};
    fitted.request("time",window,fit);if(!wait(fitted,"time",window,fit))return 31;
    const auto bare=*fitted.raster("time",window,fit);
    if(bare.requestedView!=window||bare.view==window||bare.fitPoints!=bare.points||bare.yFit!=fit)return 32;
    // The request API/cache must still identify ORIGINAL geometry, not the
    // worker-fitted Y. Manual Y and auto Y are different keys, even for same X.
    if(fitted.raster("time",bare.view,fit)||fitted.raster("time",window))return 33;
    auto empty=window;empty.minX=3000;empty.maxX=4000;
    const auto noPoints=pgn::ui::renderNativeCatalog(file,{},"time",empty,{},qRgb(90,220,170),nullptr,true,fit);
    if(!noPoints.complete||noPoints.points||noPoints.fitPoints||noPoints.view!=empty||noPoints.yFit.targetY)return 34;
    const pgn::ui::CatalogYFit knownZeroTarget{true,0.};
    const auto zeroTarget=pgn::ui::renderNativeCatalog(file,{},"time",empty,{},qRgb(90,220,170),nullptr,true,knownZeroTarget);
    if(!zeroTarget.complete||zeroTarget.points||zeroTarget.view.minY!=0||zeroTarget.view.maxY<=0||zeroTarget.yFit.targetY!=0.)return 35;
    const pgn::ui::CatalogYFit target{true,90.};
    fitted.request("time",window,target);
    const auto* pending=fitted.lastComplete("time");
    if(!pending||pending->view!=bare.view||pending->image!=bare.image||pending->yFit!=bare.yFit||
       fitted.raster("time",window,target))return 36;
    if(!wait(fitted,"time",window,target))return 37;
    const auto withTarget=*fitted.raster("time",window,target);
    if(withTarget.view.minY>20||withTarget.view.maxY<90||withTarget.yFit.targetY!=90.)return 38;
    const auto oldHits=fitted.cacheHits();
    fitted.request("time",window,{true,150.});fitted.request("time",window,target);
    if(!fitted.raster("time",window,target)||fitted.cacheHits()!=oldHits+1)return 39;
    QElapsedTimer cancelled;cancelled.start();while(fitted.busy()&&cancelled.elapsed()<5000){QCoreApplication::processEvents();QThread::msleep(1);}
    if(fitted.busy()||!fitted.raster("time",window,target)||fitted.raster("time",window,target)->image!=withTarget.image)return 40;
    // A newly admitted extreme changes effective Y: never resume old pixels
    // under new axes. The next non-extreme append may resume exactly.
    if(!add(1011,550,100.))return 41;
    ++fitBounds.points;++fitBounds.revision;fitted.publishBounds({},fitBounds,fitBounds);
    auto spike=window;spike.revision=1011;fitted.request("time",spike,target);
    if(!wait(fitted,"time",spike,target))return 42;
    const auto spikeFrame=*fitted.raster("time",spike,target);
    const auto spikeExact=pgn::ui::renderNativeCatalog(file,{},"time",spike,{},qRgb(90,220,170),nullptr,true,target);
    if(spikeFrame.incremental||spikeFrame.view.maxY<100||spikeFrame.image!=spikeExact.image||spikeFrame.points!=spikeExact.points)return 43;
    if(!add(1012,551,25.))return 44;
    ++fitBounds.points;++fitBounds.revision;fitted.publishBounds({},fitBounds,fitBounds);
    auto sameFit=spike;sameFit.revision=1012;fitted.request("time",sameFit,target);
    if(!wait(fitted,"time",sameFit,target))return 45;
    const auto sameFrame=*fitted.raster("time",sameFit,target);
    const auto sameExact=pgn::ui::renderNativeCatalog(file,{},"time",sameFit,{},qRgb(90,220,170),nullptr,true,target);
    if(!sameFrame.incremental||sameFrame.rowsRead!=1||sameFrame.image!=sameExact.image||sameFrame.points!=sameExact.points)return 46;
    const auto historical=pgn::ui::renderNativeCatalog(file,{},"time",window,{},qRgb(90,220,170),nullptr,true,target);
    if(historical.view!=withTarget.view||historical.image!=withTarget.image||historical.points!=withTarget.points)return 47;
    auto cancelledToken=std::make_shared<std::atomic_bool>(true);
    if(pgn::ui::renderNativeCatalog(file,{},"time",sameFit,cancelledToken,qRgb(90,220,170),nullptr,true,target).complete)return 48;
    if(fitted.cachedFrames()>fitted.CacheFrameLimit||fitted.cacheBytes()>fitted.CacheByteLimit)return 49;
    sqlite3_finalize(insert);sqlite3_close(db);
    std::cout<<"PASS complete-frame cache, immediate exact revision, cancelled A-B-A, cached delta/full pixel-count parity, bounded memory/scope isolation, continuous append prefix/delta and regression invalidation\n";
    std::cout<<"PASS exact selected TIME fit, request/render identity, empty vs known0 overlay, target snapshot pending/A-B-A, revision-filtered extrema, changed-Y full redraw and same-Y incremental parity, manual/ALL/staircase guard\n";
}

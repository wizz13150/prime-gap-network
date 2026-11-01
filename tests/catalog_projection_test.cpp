#include "ui/catalog_projection.h"
#include "ui/catalog_layer.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QElapsedTimer>
#include <QThread>
#include <bit>
#include <iostream>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);QTemporaryDir directory;QString file;
 if(argc>1)file=QString::fromLocal8Bit(argv[1]);
 else{
  file=directory.path()+"/fixture.sqlite";
  {auto db=QSqlDatabase::addDatabase("QSQLITE","fixture");db.setDatabaseName(file);if(!db.open())return 1;
   {QSqlQuery q(db);if(!q.exec("CREATE TABLE plot_coordinates(axis TEXT,point_key TEXT,observation_id INTEGER,x REAL,y REAL,PRIMARY KEY(axis,point_key)) WITHOUT ROWID"))return 2;
    if(!q.exec("CREATE TABLE plot_bounds(axis TEXT PRIMARY KEY,min_x REAL,max_x REAL,min_y REAL,max_y REAL,points INTEGER,revision INTEGER)"))return 3;
    db.transaction();q.prepare("INSERT INTO plot_coordinates VALUES('length',?,?,800,19)");
    const auto bits=std::bit_cast<std::uint64_t>(800.0)^(std::uint64_t{1}<<63);
    for(int i=1;i<=1000;++i){q.bindValue(0,QString::number(bits,16).rightJustified(16,'0')+QString::number(i,16).rightJustified(16,'0'));q.bindValue(1,i);if(!q.exec())return 4;}
    if(!q.exec("INSERT INTO plot_bounds VALUES('length',800,800,19,19,1000,1000)"))return 5;db.commit();}
   db.close();}QSqlDatabase::removeDatabase("fixture");
 }
 const auto bounds=pgn::ui::readCatalogBounds(file,"length");
 if(!bounds.error.isEmpty()||bounds.points<=0){std::cerr<<bounds.error.toStdString();return 6;}
 const pgn::ui::DenseView view{bounds.minX-1,bounds.maxX+1,bounds.minY-1,bounds.maxY+1,QSize(1000,400),static_cast<std::uint64_t>(bounds.revision)};
 auto token=std::make_shared<std::atomic_bool>(false);const auto raster=pgn::ui::renderCatalog(file,"length",view,token);
 if(!raster.complete||raster.points!=bounds.points||raster.image.isNull()){std::cerr<<raster.error.toStdString();return 7;}
 const auto unknown=pgn::ui::readCatalogBounds(file,"time");if(!unknown.error.isEmpty()||unknown.points!=0)return 8;
 token->store(true);if(pgn::ui::renderCatalog(file,"length",view,token).complete)return 9;
 if(pgn::ui::renderCatalog(file,"wrong-axis",view,{}).error.isEmpty())return 10;
 // Independent Qt reference, identical source/view: no visual or data delta.
 pgn::ui::DensePointLayer reference;reference.begin(view);QElapsedTimer timer;timer.start();
 {auto db=QSqlDatabase::addDatabase("QSQLITE","oracle");db.setConnectOptions("QSQLITE_OPEN_READONLY");db.setDatabaseName(file);if(!db.open())return 11;
  {QSqlQuery q(db);q.setForwardOnly(true);if(!q.exec("SELECT x,y FROM plot_coordinates WHERE axis='length' ORDER BY point_key"))return 12;
   std::vector<pgn::ui::DensePoint> batch;batch.reserve(256);
   while(q.next()){batch.push_back({q.value(0).toDouble(),q.value(1).toDouble(),pgn::ui::ImportedObservationColor});
    if(batch.size()==256){reference.append(batch);batch.clear();}}
   reference.append(batch);reference.finish();}db.close();}QSqlDatabase::removeDatabase("oracle");
 const auto referenceNs=timer.nsecsElapsed();
 if(reference.visibleCount()!=raster.points||reference.image()!=raster.image)return 13;
 auto wait=[&](const auto& done){QElapsedTimer deadline;deadline.start();while(!done()&&deadline.elapsed()<10000){app.processEvents();QThread::msleep(1);}return done();};
 pgn::ui::CatalogLayer controller(file,[]{});
 if(!wait([&]{return controller.ready();}))return 14;
 auto alternate=view;alternate.pixels=QSize(501,200);
 controller.request("length",view);controller.request("length",alternate);controller.request("length",view);
 if(!wait([&]{return !controller.busy()&&controller.raster("length",view);}))return 15;
 if(controller.raster("length",view)->image!=raster.image)return 16;
 controller.request("length",alternate);
 if(!wait([&]{return !controller.busy()&&controller.raster("length",alternate);}))return 17;
 if(controller.raster("length",view))return 18; // previous viewport cannot masquerade as latest
 if(!controller.lastComplete("length")||controller.lastComplete("time"))return 20; // display-only prior complete frame retains explicit view/revision
 {auto pending=std::make_unique<pgn::ui::CatalogLayer>(file,[]{});if(!wait([&]{return pending->ready();}))return 19;
  pending->request("length",view);pending.reset();app.processEvents();}
 std::cout<<"PASS read-only indexed raw raster; points="<<raster.points<<" render_ms="<<raster.elapsedNs/1e6<<" (one execution), memory=one raster +256points, unknown dates not invented\n";
 std::cout<<"Qt SQL oracle_ms="<<referenceNs/1e6<<" (separate execution); exact raster parity PASS\n";
 std::cout<<"PASS latest-only async view, A-B-A cancellation, destruction without QWidget capture\n";
}

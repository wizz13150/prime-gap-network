#include "ui/mining_canvas.h"
#include "ui/catalog_projection.h"
#include "horizon_graph_controls.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QThread>
#include <iostream>
#include <memory>

int main(int argc,char** argv){
    QApplication app(argc,argv);
    if(argc<2)return 2;
    const QString database=QString::fromLocal8Bit(argv[1]);
    const auto bounds=pgn::ui::readCatalogBounds(database,"length");
    if(bounds.points<=0||!bounds.error.isEmpty())return 3;
    std::unique_ptr<QWidget> canvas(pgn::ui::createMiningCanvas());canvas->resize(1380,850);
    pgn::ui::bindObservationCatalog(canvas.get(),database);
    QImage image(canvas->size(),QImage::Format_ARGB32_Premultiplied);
    auto render=[&]{image.fill(Qt::transparent);QPainter p(&image);canvas->render(&p);};
    render(); // locate actual painted graph controls, not a mock
    QRectF plot=canvas->property("pgn_plot_rectangle").toRect();
    if(plot.isEmpty()){
        // Cache rectangles are also published on small empty-data frames.
        const auto rect=canvas->property("pgn_graph_plot_rectangle").toRect();plot=rect;
    }
    if(plot.isEmpty())return 4;
    auto click=[&](const QRectF& hit){QMouseEvent e(QEvent::MouseButtonPress,hit.center(),canvas->mapToGlobal(hit.center().toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas.get(),&e);};
    click(HorizonGraphControls::axis(plot,1));render();
    QElapsedTimer wait;wait.start();
    while(canvas->property("pgn_imported_visible_points").toLongLong()!=bounds.points&&wait.elapsed()<15000){
        app.processEvents();render();QThread::msleep(2);
    }
    if(canvas->property("pgn_imported_visible_points").toLongLong()!=bounds.points){
        std::cerr<<"FAIL complete imported length projection: "<<canvas->property("pgn_imported_error").toString().toStdString();return 5;
    }
    if(canvas->property("pgn_graph_current_points").toInt()!=0||canvas->property("pgn_archive_pairs").toLongLong()!=0)return 6;
    render();if(!canvas->property("pgn_plot_cache_hit").toBool())return 7;
    const auto graphPixels=image.copy(plot.toAlignedRect());render();if(image.copy(plot.toAlignedRect())!=graphPixels)return 8;
    if(argc>2)image.save(QString::fromLocal8Bit(argv[2]));
    click(HorizonGraphControls::current(plot));render();
    if(canvas->property("pgn_imported_visible_points").toLongLong()!=0)return 9;
    click(HorizonGraphControls::history(plot));click(HorizonGraphControls::axis(plot,0));render();
    if(canvas->property("pgn_imported_visible_points").toLongLong()!=0||canvas->property("pgn_graph_history_points").toInt()!=0)return 10;
    std::cout<<"PASS complete imported LENGTH="<<bounds.points<<", cached identical pixels, CURRENT exclusion, unknown dates not projected on TIME\n";
}

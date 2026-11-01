#include <QApplication>
#include <QElapsedTimer>
#include <QPainter>
#include <QToolTip>
#include <QMouseEvent>
#include <QThread>
#include "ui/render/horizon_difficulty_graph.h"
#include <iostream>

int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    if(QGuiApplication::platformName()!=QStringLiteral("offscreen")) return 2;
    HorizonDifficultyGraph graph(HorizonDifficulty::Coin::Riecoin);
    graph.setSource("closed-border-fixture","fixture","Closed render fixture; no live source");
    const auto now=QDateTime::currentMSecsSinceEpoch();
    graph.appendObservation(HorizonDifficulty::Coin::Riecoin,"closed-border-fixture",now-30000,19.5);
    graph.appendObservation(HorizonDifficulty::Coin::Riecoin,"closed-border-fixture",now-10000,25.8);
    graph.show();
    int checks=0;
    const auto check=[&](bool condition){++checks;if(!condition){std::cerr<<"failed check "<<checks<<'\n';std::exit(1);}};
    const auto pump=[&]{QElapsedTimer timer;timer.start();while(timer.elapsed()<80){app.processEvents();QThread::msleep(1);}};
    graph.resize(1180,480);
    QElapsedTimer populated;populated.start();
    while(graph.property("horizon_difficulty_visible_samples").toULongLong()!=2&&populated.elapsed()<3000){pump();graph.grab();}
    check(graph.property("horizon_difficulty_visible_samples").toULongLong()==2);
    const auto verify=[&]{
        pump();const auto image=graph.grab().toImage();
        const auto area=graph.property("horizon_difficulty_plot").toRectF();
        const auto view=graph.property("horizon_difficulty_view").toList();
        const auto x=graph.property("horizon_difficulty_x_ticks").toList();
        const auto y=graph.property("horizon_difficulty_y_ticks").toList();
        check(view.size()==4&&x.size()>=2&&y.size()>=2);
        check(x.front()==view[0]&&x.back()==view[1]);
        check(y.front()==view[2]&&y.back()==view[3]);
        // Verify actual painted pixels on every side, including after data is
        // drawn. A correct tick list alone cannot prove the frame is visible.
        for(const QPointF p:{QPointF(area.left(),area.center().y()),QPointF(area.right(),area.center().y()),
                            QPointF(area.center().x(),area.top()),QPointF(area.center().x(),area.bottom())}){
            int brightest=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                const auto color=image.pixelColor(qRound(p.x())+dx,qRound(p.y())+dy);
                brightest=std::max(brightest,color.green());
            }
            check(brightest>100);
        }
        return area;
    };
    for(const QSize size:{QSize(720,260),QSize(1180,480),QSize(1700,760)}){
        graph.resize(size);auto area=verify();
        for(int zoom=0;zoom<3;++zoom){
            QWheelEvent wheel(area.center(),graph.mapToGlobal(area.center().toPoint()),QPoint(),QPoint(0,360),
                Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            app.sendEvent(&graph,&wheel);area=verify();
        }
        const auto anchor=area.center();
        QMouseEvent press(QEvent::MouseButtonPress,anchor,graph.mapToGlobal(anchor.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        app.sendEvent(&graph,&press);
        QMouseEvent move(QEvent::MouseMove,anchor+QPointF(37,-19),graph.mapToGlobal((anchor+QPointF(37,-19)).toPoint()),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        app.sendEvent(&graph,&move);
        QMouseEvent release(QEvent::MouseButtonRelease,anchor+QPointF(37,-19),graph.mapToGlobal((anchor+QPointF(37,-19)).toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        app.sendEvent(&graph,&release);verify();
    }
    if(argc>1)check(graph.grab().save(QString::fromLocal8Bit(argv[1])));
    std::cout<<checks<<" viewport boundary/render checks passed\n";
}

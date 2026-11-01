#include "ui/mining_canvas.h"
#include "ui/render/telemetry.h"
#include "booking/booking_model.h"
#include "ui/render/mining_layout.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPluginLoader>
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <iostream>
#include <memory>

// Closed offscreen fixture, never the application launcher. Only the offscreen
// container may show; no desktop window, runtime bridge, GPU controls, user
// settings/database, network or live performance data.
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const QString plugin = QString::fromLocal8Bit(argv[1]);
    const QString output = QString::fromLocal8Bit(argv[2]);
    if (!QFileInfo(plugin).isFile() || !QDir(output).exists()) return 3;
    // Fail as a console test BEFORE QApplication if the specified matching Qt
    // platform plugin/dependencies cannot load; never fall back to Windows.
    QPluginLoader preflight(plugin);
    if (!preflight.load()) {
        std::cerr << preflight.errorString().toStdString() << '\n';
        return 4;
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QFileInfo(plugin).absolutePath().toUtf8());
    QApplication app(argc, argv);
    if (QGuiApplication::platformName() != "offscreen") return 5;
    QCoreApplication::setOrganizationName("PGN-closed-render-fixture");
    QCoreApplication::setApplicationName("Metric-canvas-test");
    QTemporaryDir isolated;
    if (!isolated.isValid()) return 6;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolated.path());
    QSettings settings;
    settings.setValue("frontier/device-card-collapsed-v3/fixture-expanded", false);
    settings.setValue("frontier/device-card-collapsed-v3/fixture-compact", true);
    pgn::booking::BookingModel booking(isolated.path());
    std::unique_ptr<QWidget> canvas(pgn::ui::createMiningCanvas());
    pgn::ui::bindBookingModel(canvas.get(), &booking);
    FrontierTelemetrySnapshot lane;
    lane.authoritative = true;
    lane.runId = "fixture-run-a"; lane.gpuUuid = "fixture-expanded";
    lane.gpuName = "OFFSCREEN FIXTURE - expanded card";
    lane.engineId = "metric-test-not-live";
    lane.observedUtc = QDateTime::currentDateTimeUtc();
    lane.rowsCompleted = "400"; lane.cumulativeGapCandidates = "4";
    lane.e2ePositionsPerSecond = 224e9; lane.powmPerSecond = 746e6;
    lane.gapPairsTestedPerSecond = 4.;
    lane.raw = {{"pgn_process_running", true}, {"legacy_miner_bound", true},
        {"legacy_estimated_pps", 224e9}, {"pgn_cumulative_tests", "500"},
        {"pgn_cumulative_processed_gaps", "8"}, {"pgn_processed_gaps_per_second", 8.},
        {"pgn_retained_gaps_per_billion_positions", 0.018},
        {"pgn_source_interval_ns", "1000000000"}, {"pgn_source_time_ns", "1000000000"},
        {"pgn_clock_namespace", "closed-fixture-only"}};
    pgn::ui::presentDevice(canvas.get(), lane);
    lane.runId = "fixture-run-b"; lane.gpuUuid = "fixture-compact";
    lane.gpuName = "OFFSCREEN FIXTURE - compact card";
    lane.rowsCompleted = "600";
    pgn::ui::presentDevice(canvas.get(), lane, true);
    int rendered = 0;
    for (int width : {1280, 1800}) {
        canvas->resize(width, 900);
        QImage image(canvas->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        canvas->render(&painter);
        painter.setPen(Qt::yellow);
        painter.drawText(QPoint(18, 22), "OFFSCREEN TEST FIXTURE - NOT LIVE PERFORMANCE");
        painter.end();
        if (canvas->isVisible() || !canvas->property("pgn_total_card_first").toBool() ||
            canvas->property("pgn_total_card_accent").toString() != "#e6d2b5" ||
            canvas->property("pgn_total_positions").toString() != "1000" ||
            canvas->property("pgn_total_card_gauges").toStringList() !=
                QStringList{"pos/s", "tests/s", "gaps/s", "gaps/Gpos"}) return 7;
        const auto path = QDir(output).filePath(QString("root537-metric-canvas-%1.png").arg(width));
        if (QFileInfo::exists(path) || !image.save(path)) return 8;
        ++rendered;
    }
    QWidget* content = canvas.get();
    std::unique_ptr<QWidget> wrapper(pgn::ui::createMiningViewport(canvas.release()));
    if(qobject_cast<QScrollArea*>(wrapper.get())||!wrapper->findChildren<QScrollBar*>().empty())return 9;
    for (const QSize requested : {QSize(640,480), QSize(800,400), QSize(800,360), QSize(1000,560), QSize(1280,640), QSize(1380,768), QSize(1920,1200)}) {
        wrapper->resize(requested);wrapper->show();app.processEvents();
        const QSize size=wrapper->size();
        if(size.width()!=requested.width()||size.height()!=std::max(requested.height(),pgn::ui::MiningLayout::MinimumViewportHeight))return 16;
        if(content->size()!=size)return 10;
        const auto layout=pgn::ui::MiningLayout::fit(size.width(),size.height());
        for(int section=0;section<(layout.sections?3:1);++section){
        if(layout.sections){
            QPointF at(layout.scene.left()+(section+.5)*layout.scene.width()/3,layout.scene.top()+12);
            QMouseEvent press(QEvent::MouseButtonPress,at,at,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(content,&press);app.processEvents();
        }
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);wrapper->render(&painter);
        painter.setPen(Qt::yellow);painter.drawText(18,22,"OFFSCREEN FIXTURE - NOT LIVE PERFORMANCE");painter.end();
        const auto central=content->property("pgn_central_summary_rectangle").toRectF();
        const auto plot=content->property("pgn_graph_plot_rectangle").toRectF();
        const auto verifier=content->property("pgn_central_verifier_y").toDouble();
        const QRectF bounds(QPointF(0,0),QSizeF(size));
        if(!central.isEmpty()&&(!bounds.contains(central)||central.height()<94||central.bottom()>=verifier)){std::cerr<<"FAIL central "<<central.x()<<','<<central.y()<<' '<<central.width()<<'x'<<central.height()<<" bottom="<<central.bottom()<<" verifier="<<verifier<<" requested="<<requested.width()<<'x'<<requested.height()<<'\n';return 11;}
        if(!plot.isEmpty()&&(!bounds.contains(plot)||plot.height()<230)){std::cerr<<"FAIL graph panel "<<plot.width()<<"x"<<plot.height()<<" in requested viewport "<<requested.width()<<"x"<<requested.height()<<'\n';return 12;}
        if(!central.isEmpty()&&!plot.isEmpty()&&central.intersects(plot))return 13;
        if(!central.isEmpty()){
            const auto cells=content->property("pgn_central_metric_rectangles").toList();
            const auto footer=content->property("pgn_central_footer_rectangle").toRectF();
            const auto instruments=content->property("pgn_central_boundary_rectangles").toList();
            if(cells.size()!=5||instruments.size()!=2||!central.contains(footer))return 18;
            for(const auto& instrument:instruments)
                if(central.intersects(instrument.toRectF()))return 19;
            for(int i=0;i<cells.size();++i){
                const auto cell=cells[i].toRectF();
                if(!central.contains(cell)||cell.intersects(footer))return 20;
                for(int j=i+1;j<cells.size();++j)
                    if(cell.intersects(cells[j].toRectF()))return 21;
            }
        }
        if(!plot.isEmpty()){
            const auto view=content->property("pgn_graph_presented_bounds").toList();
            const auto xt=content->property("pgn_graph_x_ticks").toList();
            const auto yt=content->property("pgn_graph_y_ticks").toList();
            const auto labels=content->property("pgn_graph_x_tick_labels").toList();
            if(view.size()!=4||xt.size()<2||yt.size()<2||
                labels.size()!=xt.size()||labels.front().toString()==labels.back().toString()||
                !content->property("pgn_graph_border_tick_contract").toBool()||
                xt.front().toDouble()!=view[0].toDouble()||xt.back().toDouble()!=view[1].toDouble()||
                yt.front().toDouble()!=view[2].toDouble()||yt.back().toDouble()!=view[3].toDouble())return 17;
        }
        if((!layout.sections||section==1)&&central.isEmpty())return 14;
        if((!layout.sections||section==2)&&plot.isEmpty())return 15;
        const auto path=QDir(output).filePath(QString("root575-metric-viewport-%1x%2-section%3.png").arg(requested.width()).arg(requested.height()).arg(section));
        if(QFileInfo::exists(path)||!image.save(path))return 12;
        ++rendered;
        }
    }
    std::cout << "PASS " << rendered << " actual canvas/viewport offscreen renders; no scrollbars, exact viewport fit, readable-size central/graph separation and reachable sections; no desktop/runtime/network; fixture data only\n";
}

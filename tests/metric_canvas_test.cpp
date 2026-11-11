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
#include <QJsonArray>
#include <QRegularExpression>
#include <iostream>
#include <memory>

static int renderCardMatrix(QApplication& app,QSettings& settings,const QString& output,
                            const QString& isolatedPath) {
    int checked=0;
    const auto jsonRect=[](const QJsonArray& a){
        return QRectF(a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble());
    };
    for(bool expanded:{false,true})for(double rate:{582.64e9,999.99e9,1.10e12,0.})
    for(int width:{1280,1366,1920}){
        settings.setValue("frontier/device-card-collapsed-v3/card-fixture",!expanded);
        settings.setValue("frontier/device-card-collapsed-v3/@total",!expanded);
        pgn::booking::BookingModel booking(isolatedPath);
        std::unique_ptr<QWidget> canvas(pgn::ui::createMiningCanvas());
        pgn::ui::bindBookingModel(canvas.get(),&booking);
        pgn::ui::configureMiningDashboard(canvas.get());
        canvas->setProperty("pgn_card_layout_audit",true);
        canvas->setProperty("pgn_values_audit_enabled",true);
        canvas->setProperty("pgn_network_ticket_frame",QJsonObject{{"positions","200000000000000"}});
        FrontierTelemetrySnapshot lane;
        lane.authoritative=true;lane.runId="closed-card-fixture";lane.gpuUuid="card-fixture";
        lane.gpuName="RTX FIXTURE — no scientific work";lane.engineId="closed-card-test";
        lane.observedUtc=QDateTime::currentDateTimeUtc();lane.rowsCompleted="400";
        lane.cumulativeGapCandidates="4";lane.e2ePositionsPerSecond=rate;
        lane.powmPerSecond=746e6;lane.gapPairsTestedPerSecond=4.;lane.gpuSmUtilPercent=100.;
        lane.raw={{"pgn_process_running",true},{"legacy_miner_bound",true},
                  {"legacy_estimated_pps",rate},{"pgn_processed_gaps_per_second",4.},
                  {"pgn_source_interval_ns","1000000000"},{"pgn_source_time_ns","1000000000"},
                  {"pgn_clock_namespace","closed-fixture-only"}};
        pgn::ui::presentDevice(canvas.get(),lane);
        canvas->resize(width,180);
        QImage image(canvas->size(),QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);QPainter painter(&image);canvas->render(&painter);painter.end();
        app.processEvents();
        image=QImage(canvas->size(),QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);painter.begin(&image);canvas->render(&painter);painter.end();
        const auto cards=canvas->property("pgn_card_layout").toJsonArray();
        if(canvas->isVisible()||cards.size()!=2)return 30;
        if(canvas->property("pgn_total_card_gauges").toStringList()!=QStringList{"pos/s","tests/s","gaps/s"})return 31;
        int rateValues=0,positionValues=0;
        const QString expectedPosition=rate==0.?"0.00":rate==582.64e9?"582.64G":rate==999.99e9?"999.99G":"1.10T";
        for(const auto& row:canvas->property("pgn_values_audit_rects").toJsonArray()){
            const auto value=row.toObject();const auto label=value.value("label").toString();
            const auto text=value.value("value").toString();
            if(label=="gaps/Gpos"||text.contains(QChar(0x2248))||text.contains('~'))return 32;
            if(label!="pos/s"&&label!="tests/s"&&label!="gaps/s")continue;
            if(!QRegularExpression("^[0-9]+\\.[0-9]{2}[kKMGTPEZY]?$").match(text).hasMatch())return 33;
            if(label=="pos/s"){
                if(text!=expectedPosition)return 38;
                ++positionValues;
            }
            const QRectF glyph(value.value("x").toDouble(),value.value("y").toDouble(),
                               value.value("w").toDouble(),value.value("h").toDouble());
            bool contained=false;
            for(const auto& card:cards)contained|=jsonRect(card.toObject().value("rect").toArray()).contains(glyph);
            if(!contained||glyph.height()<12.)return 34;
            ++rateValues;
        }
        if(rateValues<5||positionValues!=2)return 35;
        for(const auto& card:cards){
            const auto c=card.toObject();
            if(c.value("header_end").toDouble()>=c.value("gauge_top").toDouble()||
               c.value("gauge_bottom").toDouble()>=c.value("footer_start").toDouble())return 36;
        }
        const QString path=QDir(output).filePath(QString("cards-%1-%2-%3.png")
            .arg(expanded?"expanded":"compact").arg(rate,0,'f',0).arg(width));
        if(QFileInfo::exists(path)||!image.save(path))return 37;
        ++checked;
    }
    std::cout<<"PASS "<<checked<<" closed actual GPU/Total card renders; 1280/1366/1920, compact/expanded, sub1T/1.10T/zero, two decimals, no approximation or gaps/Gpos, measured glyph/band bounds; no scientific work\n";
    return 0;
}

// Closed offscreen fixture, never the application launcher. Only the offscreen
// container may show; no desktop window, runtime bridge, GPU controls, user
// settings/database, network or live performance data.
int main(int argc, char** argv) {
    if (argc != 3 && !(argc == 4 && QString::fromLocal8Bit(argv[3]) == "--cards-only")) return 2;
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
    if(argc>3&&QString::fromLocal8Bit(argv[3])=="--cards-only")
        return renderCardMatrix(app,settings,output,isolated.path());
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
                QStringList{"pos/s", "tests/s", "gaps/s"}) return 7;
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
            // The central work panel is three rows of three metrics. Check the
            // complete current panel, including elapsed/full/100-block timing.
            if(cells.size()!=9||instruments.size()!=2||!central.contains(footer)){
                std::cerr<<"FAIL central structure: "<<cells.size()<<" metrics, "
                         <<instruments.size()<<" instruments, footer contained="
                         <<central.contains(footer)<<'\n';return 18;
            }
            for(const auto& instrument:instruments)
                if(central.intersects(instrument.toRectF()))return 19;
            for(int i=0;i<cells.size();++i){
                const auto cell=cells[i].toRectF();
                if(!central.contains(cell)||cell.intersects(footer)){
                    std::cerr<<"FAIL central cell "<<i<<": "<<cell.x()<<','<<cell.y()<<' '
                             <<cell.width()<<'x'<<cell.height()<<" panel="<<central.x()<<','
                             <<central.y()<<' '<<central.width()<<'x'<<central.height()
                             <<" footer="<<footer.x()<<','<<footer.y()<<' '<<footer.width()
                             <<'x'<<footer.height()<<" viewport="<<requested.width()<<'x'
                             <<requested.height()<<'\n';return 20;
                }
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

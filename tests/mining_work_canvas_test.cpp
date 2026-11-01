#include "ui/mining_canvas.h"
#include "booking/booking_model.h"
#include "ui/render/telemetry.h"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPluginLoader>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <iostream>
#include <memory>

int main(int argc,char** argv){
    if(argc!=3)return 2;
    const QString plugin=QString::fromLocal8Bit(argv[1]),output=QString::fromLocal8Bit(argv[2]);
    if(!QFileInfo(plugin).isFile()||!QDir(output).exists())return 3;
    QPluginLoader preflight(plugin);if(!preflight.load()){std::cerr<<preflight.errorString().toStdString()<<'\n';return 4;}
    qputenv("QT_QPA_PLATFORM","offscreen");qputenv("QT_QPA_PLATFORM_PLUGIN_PATH",QFileInfo(plugin).absolutePath().toUtf8());
    QApplication app(argc,argv);if(QGuiApplication::platformName()!="offscreen")return 5;
    QTemporaryDir isolated;if(!isolated.isValid()||!QDir().mkpath(isolated.path()+"/data/runtime"))return 6;
    QCoreApplication::setOrganizationName("PGN-closed-render-fixture");QCoreApplication::setApplicationName("Mining-work-canvas-test");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,isolated.path());
    pgn::booking::BookingModel model(isolated.path());
    std::unique_ptr<QWidget> canvas(pgn::ui::createMiningCanvas());pgn::ui::bindBookingModel(canvas.get(),&model);canvas->resize(1280,900);
    const QString run(32,'a'),start="199999000000000000000",end="200000000000000000000";
    FrontierTelemetrySnapshot rate;rate.authoritative=true;rate.runId="another-run";rate.gpuUuid="fixture-only";
    rate.gpuName="OFFSCREEN RATE FIXTURE — NOT LIVE";rate.state="running";rate.observedUtc=QDateTime::currentDateTimeUtc();rate.e2ePositionsPerSecond=224e9;
    rate.raw={{"pgn_process_running",true}};pgn::ui::presentDevice(canvas.get(),rate);
    const auto time=QDateTime::fromString("2026-10-14T12:00:00.000Z",Qt::ISODateWithMs);int step=0;
    for(const auto& positions:{QString("0"),QString("250000000000000"),QString("414000000000000"),QString("1000000000000000")}){
        if(step==1){rate.runId=run;pgn::ui::presentDevice(canvas.get(),rate);}
        model.observeRuntime(run,start,end,"GPU-offscreen-fixture","NOT LIVE",positions,time.addSecs(step),true);
        if(model.runtime().positions!=positions||!model.runtime().fraction||model.jobs().size()!=1)return 7;
        QImage image(canvas->size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        QPainter painter(&image);canvas->render(&painter);painter.end();
        const auto fractions=canvas->property("pgn_booking_rail_observed_fractions").toList();
        if(fractions.size()!=1||!fractions.first().isValid()||std::abs(fractions.first().toDouble()-*model.runtime().fraction)>1e-12)return 8;
        const auto visual=canvas->property("pgn_booking_rail_visual_fractions").toList();
        if(visual.size()!=1||visual.first().toDouble()>*model.runtime().fraction+1e-12)return 13;
        const auto phaseBefore=canvas->property("pgn_booking_texture_phase");
        // Allow only the bounded 250ms paint transition to finish. This waits
        // on this isolated offscreen event loop, never a user window/process.
        QEventLoop wait;QTimer::singleShot(270,&wait,&QEventLoop::quit);wait.exec();
        {QPainter settled(&image);canvas->render(&settled);}
        if(step==0&&canvas->property("pgn_booking_texture_phase").isValid())return 15;
        if(step>0&&(!canvas->property("pgn_booking_texture_phase").isValid()||canvas->property("pgn_booking_texture_rate").toDouble()!=224e9))return 16;
        if(step>0&&canvas->property("pgn_booking_texture_phase")==phaseBefore)return 18;
        const auto final=canvas->property("pgn_booking_rail_visual_fractions").toList();
        if(final.size()!=1||std::abs(final.first().toDouble()-*model.runtime().fraction)>1e-12)return 14;
        const auto detail=canvas->property("pgn_booking_rail_visible_details").toStringList();if(detail.size()!=1||!detail.first().startsWith("MINING"))return 9;
        if(model.runtime().positions!=positions||model.runtime().observedUtc!=time.addSecs(step)||canvas->isVisible())return 10;
        const auto tile=pgn::ui::reservationSegmentRect(canvas.get(),run).toAlignedRect();if(tile.isEmpty())return 11;
        auto crop=image.copy(tile.adjusted(-8,-25,8,20));
        QPainter caption(&crop);caption.setPen(Qt::yellow);caption.drawText(QPoint(8,crop.height()-4),"OFFSCREEN SOURCE-CURSOR FIXTURE — NOT LIVE WORK");caption.end();
        const auto path=QDir(output).filePath(QString("mining-work-%1.png").arg(step));if(QFileInfo::exists(path)||!crop.save(path))return 12;
        ++step;
    }
    const auto render=[&]{QImage image(canvas->size(),QImage::Format_ARGB32_Premultiplied);QPainter painter(&image);canvas->render(&painter);};
    rate.stale=true;pgn::ui::presentDevice(canvas.get(),rate);render();const auto stalePhase=canvas->property("pgn_booking_texture_phase");
    QEventLoop stale;QTimer::singleShot(270,&stale,&QEventLoop::quit);stale.exec();render();
    if(canvas->property("pgn_booking_texture_rate").isValid()||canvas->property("pgn_booking_texture_phase")!=stalePhase)return 19;
    rate.stale=false;pgn::ui::presentDevice(canvas.get(),rate);
    model.observeRuntime(run,start,end,"GPU-offscreen-fixture","NOT LIVE",model.runtime().positions,time.addSecs(5),false);
    render();const auto frozen=canvas->property("pgn_booking_texture_phase");
    QEventLoop stopped;QTimer::singleShot(270,&stopped,&QEventLoop::quit);stopped.exec();render();
    if(canvas->property("pgn_booking_texture_rate").isValid()||canvas->property("pgn_booking_texture_phase")!=frozen)return 17;
    std::cout<<"mining work canvas: 4 actual offscreen renders; exact raw fractions/positions/source times; matching-run texture moves, wrong-run/stale/stopped freeze PASS\n";return 0;
}

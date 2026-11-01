#include "ui/mining_canvas.h"
#include "telemetry.h"
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <iostream>
#include <memory>

// The painter remains the fidelity oracle: a foreign process environment may
// not replace the configuration tile, with or without a live PGN source.
static QImage configurationTile(QWidget& canvas)
{
    QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    canvas.render(&painter);
    painter.end();
    return image.copy(QRect(95, 42, canvas.width() - 115, 34));
}

static void clearForeignConfiguration()
{
    qunsetenv("GAPMINER_SHIM_SHIFT");
    qunsetenv("GAPMINER_SHIM_SEARCH_MERIT");
    qunsetenv("GAPMINER_SHIM_CRT");
}

static void injectForeignConfiguration()
{
    qputenv("GAPMINER_SHIM_SHIFT", "512");
    qputenv("GAPMINER_SHIM_SEARCH_MERIT", "22");
    qputenv("GAPMINER_SHIM_CRT", "foreign-unopened-path.crt");
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("PGN-QA");
    QCoreApplication::setApplicationName("configuration-isolation-test");
    clearForeignConfiguration();
    std::unique_ptr<QWidget> canvas(pgn::ui::createMiningCanvas());
    canvas->resize(1380, 900);
    const auto emptySource = configurationTile(*canvas);
    injectForeignConfiguration();
    if (configurationTile(*canvas) != emptySource) {
        std::cerr << "FAIL foreign environment changed PGN configuration without a source\n";
        return 4;
    }

    clearForeignConfiguration();
    FrontierTelemetrySnapshot lane;
    lane.gpuUuid = "GPU-9b4a9af7-c0e9-a061-212b-9d31188a24ac";
    lane.gpuName = "NVIDIA GeForce RTX3090";
    lane.runId = "qa-configuration-source";
    lane.engineId = "pgn-native-walk";
    lane.engineProfile = "GPU WALK";
    lane.sourceMode = "live";
    lane.authoritative = true;
    lane.observedUtc = QDateTime::currentDateTimeUtc();
    lane.state = "running";
    lane.producerStatus = "SOURCE PRP";
    lane.e2ePositionsPerSecond = 206000000000.0;
    lane.powmPerSecond = 1000000.0;
    lane.gapPairsTestedPerSecond = 15.0;
    pgn::ui::presentDevice(canvas.get(), lane);
    const auto liveSource = configurationTile(*canvas);
    if (liveSource == emptySource) {
        std::cerr << "FAIL PGN source configuration was not rendered\n";
        return 5;
    }
    injectForeignConfiguration();
    if (configurationTile(*canvas) != liveSource) {
        std::cerr << "FAIL foreign environment overrode actual PGN configuration\n";
        return 6;
    }
    clearForeignConfiguration();
    // These fields used to feed unrelated chain estimates, geometry modes and
    // session averages. They are not part of the native PGN source contract.
    lane.raw.insert("legacy_estimated_pps", 7.0);
    lane.raw.insert("legacy_network_primesps", 123456.0);
    lane.raw.insert("legacy_network_difficulty", 987654.0);
    lane.raw.insert("diamond_continuous", true);
    lane.raw.insert("diamond_cumulative_rows", 600000.0);
    lane.raw.insert("diamond_exact_total", 200.0);
    pgn::ui::presentDevice(canvas.get(), lane);
    if (configurationTile(*canvas) != liveSource) {
        std::cerr << "FAIL legacy raw fields changed PGN header semantics\n";
        return 7;
    }
    lane.e2ePositionsPerSecond = 0.0;
    lane.powmPerSecond = 0.0;
    lane.gapPairsTestedPerSecond = 0.0;
    pgn::ui::presentDevice(canvas.get(), lane);
    if (configurationTile(*canvas) == liveSource) {
        std::cerr << "FAIL actual source zero did not change the PGN header\n";
        return 8;
    }
    std::cout << "PASS actual PGN configuration isolated from foreign environment\n";
    return 0;
}

// New OFFSCREEN widget only. No native runtime, source, wallet or GPU created.
#include "ui/mining_canvas.h"
#include "ui/render/telemetry.h"
#include <QApplication>
#include <QPluginLoader>
#include <QFileInfo>
#include <iostream>
#include <memory>
int main(int argc,char** argv){
    if(argc!=2)return 2;const QString plugin=QString::fromLocal8Bit(argv[1]);QPluginLoader preflight(plugin);if(!preflight.load())return 3;
    qputenv("QT_QPA_PLATFORM","offscreen");qputenv("QT_QPA_PLATFORM_PLUGIN_PATH",QFileInfo(plugin).absolutePath().toUtf8());
    QApplication app(argc,argv);QCoreApplication::setOrganizationName("PGN-QA");QCoreApplication::setApplicationName("local-proof-canvas-isolated");
    std::unique_ptr<QWidget> canvas(pgn::ui::createMiningCanvas());canvas->resize(1380,850);
    const QString run(32,'a');FrontierTelemetrySnapshot lane;lane.runId=run;lane.gpuUuid="PUBLIC-GPU-FIXTURE";lane.gpuName="PUBLIC FIXTURE";
    lane.sourceMode="live";lane.authoritative=true;lane.state="running";lane.producerStatus="running";lane.observedUtc=QDateTime::currentDateTimeUtc();
    lane.e2ePositionsPerSecond=1e11;lane.powmPerSecond=1e8;lane.raw["legacy_miner_bound"]=true;pgn::ui::presentDevice(canvas.get(),lane);
    QJsonObject status{{"scope","LOCAL_CERTIFIED_PAIRS_ONLY"},{"run",run},{"declared_identity_sha256",QString(64,'a')},{"policy_sha256",QString(64,'b')},
        {"total","10000000000"},{"verified","9999999999"},{"rejected","1"},{"after_gap_id","10000000000"},{"source_watermark","10000000001"},
        {"more",true},{"error",""},{"network_credit",false},{"range_coverage",false}};
    unsigned checks=0,failures=0;const auto check=[&](bool value){++checks;if(!value)++failures;};
    const auto paint=[&]{const auto pixmap=canvas->grab();check(!pixmap.isNull());return canvas->property("pgn_live_verifier_state").toString();};
    pgn::ui::presentLocalSourceProofStatus(canvas.get(),status);check(paint().startsWith("LOCAL EXACT"));
    status["run"]=QString(32,'b');pgn::ui::presentLocalSourceProofStatus(canvas.get(),status);check(!paint().startsWith("LOCAL EXACT"));
    status["run"]=run;pgn::ui::presentLocalSourceProofStatus(canvas.get(),status);lane.raw["legacy_verifier_chain_errors"]=2;pgn::ui::presentDevice(canvas.get(),lane);
    check(paint().contains("INTEGRITY ERROR"));
    lane.raw["legacy_verifier_chain_errors"]=0;lane.raw["legacy_verifier_pending_canonical"]=1;pgn::ui::presentDevice(canvas.get(),lane);check(paint().contains("NODE ACCEPTED"));
    lane.raw["legacy_verifier_pending_canonical"]=0;lane.stale=true;pgn::ui::presentDevice(canvas.get(),lane);check(!paint().startsWith("LOCAL EXACT"));
    std::cout<<"LOCAL_SOURCE_PROOF_CANVAS "<<checks<<" checks / "<<failures<<" failures; actual offscreen rail, exact10billion/current source, real chain error/ack precedence, stale source not relabeled\n";
    return failures?1:0;
}

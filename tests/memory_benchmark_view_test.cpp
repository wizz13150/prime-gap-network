#include "qt_offscreen_fixture.h"
#include "memory_benchmark_fixture.h"
#include "ui/engine_tools_view.h"
#include "ui/memory_benchmark_view.h"
#include "telemetry.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QJsonArray>
#include <QDir>
#include <QPluginLoader>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QLabel>
#include <QRegularExpression>
#include <iostream>
#define REQUIRE(x) do{if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(0)
class ClosedExecutor final:public pgn::ui::MemoryBenchmarkExecutor {
public:
    pgn::mining::MemoryBenchmarkCapabilities caps{memoryCaps()};
    QString session,original{"fixture-original-policy"},setting{original};
    int acquires{},measures{},restores{};bool hold{},holdAcquire{},failRestore{};
    std::function<void(pgn::mining::MemoryBenchmarkCapabilities,QString)> pendingAcquire;
    std::function<void(pgn::mining::MemoryBenchmarkSample,QString)> pending;
    void acquire(const QString& id,const pgn::ui::MemoryBenchmarkTarget& target,
        std::function<void(pgn::mining::MemoryBenchmarkCapabilities,QString)> reply) override {
        ++acquires;session=id;if(target.gpuUuid!=caps.identity.gpuUuid){reply({},"fixture target mismatch");return;}
        if(holdAcquire){pendingAcquire=std::move(reply);return;}reply(caps,{});
    }
    void measure(const QString& id,const pgn::mining::MemoryBenchmarkPoint& point,
        std::function<void(pgn::mining::MemoryBenchmarkSample,QString)> reply) override {
        if(id!=session){reply({},"fixture lease owner mismatch");return;}++measures;
        setting=point.control?original:QString::number(point.memoryMhz);
        if(hold){pending=std::move(reply);return;}
        if(point.memoryMhz<4000){reply({},"fixture forbidden below4000");return;}
        auto sample=memorySample(caps,point,point.phase==pgn::mining::MemoryBenchmarkPhase::Confirmation&&!point.control&&point.candidateMhz<=5001?8000000000ULL:0);
        reply(sample,{});
    }
    void cancelAndRestore(const QString& id,std::function<void(bool,QString)> reply) noexcept override {
        ++restores;if(id!=session){reply(false,"fixture owner mismatch");return;}
        if(failRestore){reply(false,"fixture disconnected");return;}
        setting=original;reply(true,"fixture original-policy readback");
    }
};
int main(int argc,char** argv) {
    REQUIRE(pgn::test::prepareOffscreen());
    QApplication app(argc,argv);REQUIRE(QGuiApplication::platformName()=="offscreen");QTemporaryDir temp;REQUIRE(temp.isValid());
    std::unique_ptr<QWidget> bench(pgn::ui::createBenchmarkView(temp.path()));
    auto* memory=bench->findChild<QWidget*>("pgn-benchmark-memory-workspace");REQUIRE(memory);
    REQUIRE(memory->findChild<QSpinBox*>("pgn-benchmark-memory-minimum")->minimum()==4000);
    REQUIRE(memory->findChild<QSpinBox*>("pgn-benchmark-memory-count")->value()==10);
    auto* start=memory->findChild<QPushButton*>("pgn-benchmark-memory-start");auto* cancel=memory->findChild<QPushButton*>("pgn-benchmark-memory-cancel");
    REQUIRE(start&&cancel);start->click();REQUIRE(memory->property("pgn_benchmark_memory_error").toString().contains("unavailable"));
    REQUIRE(!memory->property("pgn_benchmark_memory_receipt").isValid());
    auto backend=std::make_shared<ClosedExecutor>();pgn::ui::bindMemoryBenchmarkExecutor(bench.get(),backend);
    FrontierTelemetrySnapshot source;source.authoritative=true;source.observedUtc=QDateTime::currentDateTimeUtc();source.runId="actual-source-run";
    source.gpuUuid=backend->caps.identity.gpuUuid;source.artifactSha256=backend->caps.identity.imageSha256;source.raw={{"pgn_process_running",true}};
    pgn::ui::presentEngineTools(bench.get(),source);start->click();QElapsedTimer timer;timer.start();
    while(memory->property("pgn_benchmark_memory_running").toBool()&&timer.elapsed()<3000)app.processEvents();
    REQUIRE(!memory->property("pgn_benchmark_memory_running").toBool());REQUIRE(backend->acquires==1&&backend->measures==14&&backend->restores==1);
    REQUIRE(backend->setting==backend->original);auto receipt=memory->property("pgn_benchmark_memory_receipt").toJsonObject();
    REQUIRE(receipt.value("restore_confirmed")==true&&receipt.value("complete")==true&&receipt.value("best_observed_memory_mhz")==5001);
    REQUIRE(receipt.value("requested_target").toObject().value("minimum_memory_mhz")==4000);
    REQUIRE(receipt.value("distinct_applied_group_count")==1&&memory->findChild<QTableWidget*>("pgn-benchmark-memory-groups")->rowCount()==1);
    REQUIRE(receipt.value("requested_grid_realized")==false&&receipt.value("requested_targets_achieved")==0);
    REQUIRE(memory->findChild<QTableWidget*>("pgn-benchmark-memory-results")->rowCount()==14);
    REQUIRE(QDir(temp.path()+"/data/runtime/benchmarks/memory").entryList({"*.json"},QDir::Files).size()==1);
    auto other=source;other.gpuUuid="other-fixture-gpu";other.runId="other-source-run";pgn::ui::presentEngineTools(bench.get(),other);
    auto* selector=memory->findChild<QComboBox*>("pgn-benchmark-memory-device");REQUIRE(selector&&selector->count()==2&&selector->currentData()==source.gpuUuid);
    // Cancellation wins over a late measurement callback. The captured original
    // is restored and no new measurement is scheduled after the release barrier.
    backend->hold=true;start->click();REQUIRE(cancel->isEnabled());
    const auto workStatus=memory->findChild<QLabel*>("pgn-benchmark-memory-status")->text();
    REQUIRE(workStatus.contains("positions + warmup")&&workStatus.count("2^")>=2&&QRegularExpression("[0-9]e[0-9]").match(workStatus).hasMatch());
    const int before=backend->measures;cancel->click();
    REQUIRE(backend->setting==backend->original&&!cancel->isEnabled());
    REQUIRE(memory->property("pgn_benchmark_memory_receipt").toJsonObject().value("complete")==false);
    REQUIRE(memory->property("pgn_benchmark_memory_receipt").toJsonObject().value("restore_confirmed")==true);
    auto late=std::move(backend->pending);late(memorySample(backend->caps,{0,500,9501,true}),{});app.processEvents();REQUIRE(backend->measures==before);
    backend->hold=false;backend->holdAcquire=true;start->click();cancel->click();
    REQUIRE(backend->setting==backend->original);const auto heldMeasures=backend->measures;
    auto lateAcquire=std::move(backend->pendingAcquire);lateAcquire(backend->caps,{});app.processEvents();REQUIRE(backend->measures==heldMeasures);
    backend->holdAcquire=false;
    // Errors and view destruction both take the SAME idempotent owned restore
    // path. Unconfirmed restoration never unlocks another search.
    backend->hold=false;backend->caps.coreAutomatic=false;start->click();REQUIRE(backend->setting==backend->original);
    REQUIRE(memory->property("pgn_benchmark_memory_error").toString().contains("lease"));
    backend->caps=memoryCaps();backend->hold=true;backend->failRestore=true;start->click();cancel->click();
    REQUIRE(!start->isEnabled());REQUIRE(memory->property("pgn_benchmark_memory_receipt").toJsonObject().value("restore_confirmed")==false);
    const int priorRestores=backend->restores;backend->failRestore=false;bench.reset();REQUIRE(backend->restores==priorRestores+1&&backend->setting==backend->original);
    std::cout<<"PASS closed offscreen GUI: real Benchmark tab integration, explicit unavailable path, async fixed-work search, save, cancel/late callback, owned restoration and unconfirmed/destruction guards; no hardware accessed\n";
}

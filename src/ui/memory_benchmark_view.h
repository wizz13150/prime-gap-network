#pragma once
#include "mining/memory_benchmark_search.h"
#include <QWidget>
#include <functional>
#include <memory>

struct FrontierTelemetrySnapshot;
namespace pgn::ui {
struct MemoryBenchmarkTarget {
    QString gpuUuid, sourceRun, imageSha256;
    int minimumMemoryMhz{4000}, maximumMemoryMhz{9250}, pointCount{10};
    QVector<int> requestedMemoryMhz{mining::regularMemoryRequests(minimumMemoryMhz,maximumMemoryMhz,pointCount)};
};
// Deliberately no default GPU/SSH implementation. A production implementation
// must use the application's ONE serialized writer and an exclusive remote
// memory-clock lease, captured original POLICY and owner-CAS restoration.
// Acquisition must pin the target and the real Native fixed workload (including
// output/accounting verification). Reusing a display-window rate is forbidden.
// Capabilities echo target min/max/count and carry SHORT probe work separately
// from LONG confirmation work, with the SAME pinned Native configuration.
// measure follows point.phase and exact work/warmup counts. Setting rc0 alone
// is not evidence: report expected applied MHz + Exact/RoundedSupported effect,
// and >=3 real observations covering the active-work plateau. Fine offsets
// masked by an absolute clock clamp are NotApplied, not rounded fine control.
// Enforce requested physical bounds BEFORE starting productive measurement;
// no active probe below4000MHz. Original-policy restoration controls are separate.
// GPU service-only observations cannot be returned as Native source counters.
// Methods are nonblocking; callbacks occur on the creating GUI thread.
// cancelAndRestore is idempotent and also cancels a pending acquire; its barrier
// prevents late acquisition/measurement from mutating after successful release.
// Restoration must survive GUI/process/transport loss in the lease owner, without
// ever resetting clocks belonging to a subsequent owner. Core remains AUTO;
// record its actual min/max during ACTIVE work. Core/voltage/power/fan changes
// are outside this port. Destruction cannot be its only guard.
class MemoryBenchmarkExecutor {
public:
    virtual ~MemoryBenchmarkExecutor() = default;
    virtual void acquire(const QString& sessionId,const MemoryBenchmarkTarget&,
        std::function<void(mining::MemoryBenchmarkCapabilities,QString)> reply)=0;
    virtual void measure(const QString& sessionId,const mining::MemoryBenchmarkPoint&,
        std::function<void(mining::MemoryBenchmarkSample,QString)> reply)=0;
    virtual void cancelAndRestore(const QString& sessionId,
        std::function<void(bool,QString)> reply) noexcept=0;
};
QWidget* createMemoryBenchmarkView(const QString& root,QWidget* parent);
void presentMemoryBenchmarkSource(QWidget*,const FrontierTelemetrySnapshot&);
void bindMemoryBenchmarkExecutor(QWidget*,std::shared_ptr<MemoryBenchmarkExecutor>);
}

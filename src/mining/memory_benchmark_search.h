#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <optional>

namespace pgn::mining {
QVector<int> regularMemoryRequests(int minimumMhz,int maximumMhz,int count);
// Policy and evidence only. This class never accesses a GPU or changes a clock.
struct MemoryBenchmarkIdentity {
    QString gpuUuid, driverVersion, imageSha256, profile, workSha256;
    QStringList nativeCli;
    int numberBits{}, gap{};
    quint64 measuredPositions{}, warmupPositions{};
    bool operator==(const MemoryBenchmarkIdentity&) const = default;
    QJsonObject json() const;
};
struct MemoryBenchmarkCapabilities {
    MemoryBenchmarkIdentity identity;
    QVector<int> supportedMemoryMhz;
    // Requested settings may be accepted/rounded by the real controller even
    // when absent from the driver's declared application-clock list.
    QVector<int> requestedMemoryMhz;
    int minimumRequestedMhz{4000}, maximumRequestedMhz{9250}, requestedPointCount{10};
    int baselineMemoryMhz{};
    quint64 probePositions{}, probeWarmupPositions{};
    int maximumFinalistGroups{3};
    // Supplied by an acquired typed executor, never by an imported JSON file.
    bool exclusiveLease{}, restoreOnDisconnect{}, memoryOnly{}, coreAutomatic{};
};
enum class MemoryBenchmarkPhase { Probe, Confirmation };
enum class MemoryClockEffect { Unknown, Exact, RoundedSupported, NotApplied };
enum class MemoryBenchmarkEvidence { Unknown, NativeCounters, GpuServiceOnly };
struct MemoryBenchmarkPoint {
    int sequence{}, candidateMhz{}, memoryMhz{};
    bool control{}; // true: restore the captured ORIGINAL memory policy, not a new lock
    MemoryBenchmarkPhase phase{MemoryBenchmarkPhase::Probe};
    quint64 measuredPositions{}, warmupPositions{};
    int expectedPlateauMhz{}; // confirmation pins the physical group found by probes
};
struct MemoryBenchmarkSample {
    MemoryBenchmarkIdentity identity;
    QString measurementId, sourceRun, counterNamespace;
    int requestSequence{}, requestedMemoryMhz{};
    bool control{};
    MemoryBenchmarkPhase phase{MemoryBenchmarkPhase::Probe};
    MemoryClockEffect clockEffect{MemoryClockEffect::Unknown};
    MemoryBenchmarkEvidence evidence{MemoryBenchmarkEvidence::Unknown};
    int expectedAppliedMemoryMhz{}, activeClockSamples{};
    bool nativeAccountingVerified{}, activeClockPlateauVerified{};
    QDateTime observedUtc;
    quint64 sourceBeginNs{}, sourceEndNs{}, positionsBegin{}, positionsEnd{}, warmupPositions{};
    int memoryMinMhz{}, memoryMaxMhz{};
    int coreMinMhz{}, coreMaxMhz{};
    std::optional<bool> thermalThrottle;
    QString clockLimitReasons;
    QJsonObject json(const MemoryBenchmarkPoint&) const;
};

class MemoryBenchmarkSearch final {
public:
    bool begin(const MemoryBenchmarkCapabilities&);
    std::optional<MemoryBenchmarkPoint> next() const;
    bool accept(const MemoryBenchmarkSample&);
    void cancel(const QString& reason);
    bool complete() const { return started_ && planningComplete_ && error_.isEmpty() && cursor_ == points_.size(); }
    QString error() const { return error_; }
    int bestMemoryMhz() const { return bestMemoryMhz_; }
    QJsonObject receipt() const;
private:
    MemoryBenchmarkCapabilities capabilities_;
    QVector<MemoryBenchmarkPoint> points_;
    QVector<MemoryBenchmarkSample> samples_;
    QVector<QJsonObject> comparisons_;
    QVector<QJsonObject> groups_;
    QVector<QJsonObject> probes_, probeGroups_;
    QString error_;
    bool started_{};
    bool planningComplete_{};
    qsizetype confirmationStart_{};
    qsizetype cursor_{};
    int bestMemoryMhz_{};
    int bestRequestedMemoryMhz_{};
    double bestRate_{};
    void rebuildGroups();
    void planConfirmations();
};
}

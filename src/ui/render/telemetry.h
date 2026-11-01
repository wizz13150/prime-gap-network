#pragma once

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QTimer>
#include <QVector>

#include <functional>
#include <optional>

struct FrontierGapEvent {
    QString runId;
    qint64 sequence{-1};
    // Immutable producer discovery time retained for metadata and optional
    // diagnostic timelines.  The canonical mining graph uses gap size on X.
    QDateTime observedUtc;
    // Later proof/receipt timestamp.  This may animate a proof transition but
    // must never replace observedUtc or move a physical event on the graph.
    QDateTime verifiedUtc;
    QString domainId;
    QString segment;
    QString candidateId;
    QString gap;
    QString merit;
    // Source-bound Q48 values from GapMiner's immutable result journal.
    // When both are present they are the authoritative graph-admission gate.
    QString meritQ48;
    QString networkTargetQ48;
    QString exactGap;
    QString meritDecimal;
    QString epistemicStatus;
    QString proofState;
    // Optional network submission outcome.  This is separate from proofState:
    // a mathematically exact point can still be rejected or become stale at
    // the node, and the Horizon graph must preserve that terminal red state.
    QString networkOutcome;
    QString leftGlobalIndex;
    QString rightGlobalIndex;
    QString endpointLeft;
    QString endpointRight;
    QString blockHeight;
    QString blockHash;
    QString certificateKind;
    QString certificateSha256;
    QString gpuUuid;
    std::optional<int> numberBits;
    std::optional<int> computeShift;
};

std::optional<FrontierGapEvent> LoadFrontierExactGapReceipt(
    const QString& path,
    QString& error,
    const QStringList& candidateTelemetrySources = {});

struct FrontierIndexedExactReceipt {
    QString path;
    QString fileSha256;
    FrontierGapEvent event;
};

struct FrontierExactReceiptDirectoryScan {
    QVector<FrontierIndexedExactReceipt> added;
    qulonglong inventoryCount{0};
    qulonglong filesDiscovered{0};
    qulonglong unchangedFiles{0};
    qulonglong hashedFiles{0};
    qulonglong parsedFiles{0};
    qulonglong duplicateFiles{0};
    qulonglong rejectedFiles{0};
    QStringList errors;
};

// Incremental create-only receipt inventory.  Unchanged files are never
// rehashed or reparsed; a previously incomplete/rejected file is retried only
// after its size or timestamp changes.
class FrontierExactReceiptDirectoryIndex {
public:
    explicit FrontierExactReceiptDirectoryIndex(QStringList candidateTelemetrySources = {});
    FrontierExactReceiptDirectoryScan scan(const QString& directoryPath);
    // Scan only a bounded number of directory entries.  The cursor is kept
    // internally so a large receipt directory can be attached one event-loop
    // slice at a time without blocking the first Horizon frame.
    FrontierExactReceiptDirectoryScan scanStep(const QString& directoryPath,
                                                qsizetype maximumEntries,
                                                bool* complete = nullptr);

private:
    struct Entry {
        qint64 size{-1};
        qint64 modifiedUtcMs{-1};
        QString fileSha256;
        bool accepted{false};
    };

    QStringList m_candidateTelemetrySources;
    QHash<QString, Entry> m_entries;
    QSet<QString> m_acceptedHashes;
    QString m_scanDirectory;
    QFileInfoList m_scanReceipts;
    qsizetype m_scanCursor{0};
};

struct FrontierResourceDevice {
    // Raw driver observation, never a rolling frequency or inferred zero.
    std::optional<double> smClockMhz;
    std::optional<double> memoryClockMhz;
    // The sampler reports driver/OS throttle reasons independently from the
    // optional control sidecar.  `throttleClass` is a normalized primary
    // class; `throttleReasons` retains every raw active/inactive reason so a
    // simultaneous power + thermal condition is never hidden by the badge.
    QString throttleClass{"UNKNOWN"};
    QStringList throttleReasons;
    QString throttleDetail;
    bool throttleTelemetryAuthoritative{false};
    QString deviceId;
    QString kind;
    QString name;
    QString computeProfile;
    QString computeShift;
    std::optional<double> utilizationPercent;
    std::optional<double> memoryUtilizationPercent;
    QString memoryUsedBytes;
    QString memoryTotalBytes;
    std::optional<double> powerWatts;
    std::optional<double> powerLimitWatts;
    std::optional<double> temperatureC;
    // Optional compute-side metrics.  The physical resource relay leaves
    // these absent; an XMP-aware sidecar may bind them per device.
    std::optional<double> e2ePositionsPerSecond;
    // True sliding-window rates derived only from explicit monotonic counters
    // and observed UTC timestamps.
    std::optional<double> e2eWindowPositionsPerSecond;
    std::optional<double> powmPerSecond;
    std::optional<double> efficiencyPositionsPerJoule;
};

struct FrontierResourceTelemetrySnapshot {
    bool valid{false};
    bool stale{false};
    QString error;
    QString path;
    qint64 sequence{-1};
    QDateTime observedUtc;
    // LOCAL validated-frame receipt; not remote measurement time or transit.
    std::optional<qint64> receivedMonotonicNs;
    std::optional<double> samplerIntervalMs;
    std::optional<double> samplerObservedIntervalMs;
    std::optional<double> samplerCollectWallMs;
    std::optional<double> samplerSelfCpuPercentHost;
    bool samplerBackoff{false};
    QVector<FrontierResourceDevice> devices;
};

// Decode the driver's supported/active masks, never infer NONE from missing data.
void FrontierApplyGpuClockMasks(const QString& supported, const QString& active,
                                FrontierResourceDevice& device);

FrontierResourceTelemetrySnapshot LoadFrontierResourceTelemetry(const QString& path);

struct FrontierTelemetrySnapshot {
    bool authoritative{false};
    bool stale{false};
    bool terminalSealed{false};
    QString error;
    QString telemetryPath;
    QString runId;
    qint64 sequence{-1};
    QDateTime observedUtc;
    QString state;
    QString message;

    QString sourceMode;
    QString artifactPath;
    QString artifactSha256;
    QString engineId;
    QString engineVersion;
    QString engineProfile;
    QString engineShift;
    // Exact search geometry only when explicitly emitted by the producer.
    // Never infer these values from a profile/domain name.
    std::optional<int> numberBits;
    std::optional<int> computeShift;
    QString staticComputeProfile;
    QString staticComputeShift;
    std::optional<double> staticProfileE2ePositionsPerSecond;
    std::optional<double> staticProfilePowmPerSecond;
    std::optional<double> staticProfileEfficiencyPositionsPerJoule;

    QString domainId;
    QString domainStart;
    QString domainCurrent;
    QString domainEnd;
    QString rowsCompleted;
    QString rowsTotal;
    std::optional<double> rateRowsPerSecond;
    std::optional<double> kernelPositionsPerSecond;
    std::optional<double> epochPipelinePositionsPerSecond;
    std::optional<double> e2ePositionsPerSecond;
    // True wall-clock sliding-window throughput, derived from data.rows deltas.
    std::optional<double> e2eWindowPositionsPerSecond;
    std::optional<double> powmPerSecond;
    // Adjacent base-2 probable endpoint pairs examined per wall second.
    // This is deliberately distinct from exact/certified gaps per second.
    std::optional<double> gapPairsTestedPerSecond;
    std::optional<double> epochGapPairsPerSecond;
    // True sliding-window pair rate, only when an explicit cumulative counter exists.
    std::optional<double> gapPairsWindowPerSecond;
    QString cumulativeGapPairsTested;
    QString segmentGapPairsTested;
    std::optional<double> kernelDutyFraction;
    QString baselineId;
    std::optional<double> performanceRatio;
    std::optional<double> efficiencyPositionsPerJoule;
    std::optional<double> efficiencyRatio;

    QString gpuUuid;
    QString gpuName;
    std::optional<double> gpuSmUtilPercent;
    std::optional<double> gpuSmUtilP5Percent;
    std::optional<double> gpuSmUtilP95Percent;
    std::optional<double> gpuDutyAbove90Fraction;
    std::optional<double> gpuSampleWindowSeconds;
    std::optional<double> gpuSampleCount;
    std::optional<double> gpuSampleIntervalP95Ms;
    std::optional<double> gpuMemoryUtilPercent;
    QString gpuVramUsedBytes;
    QString gpuVramTotalBytes;
    std::optional<double> gpuPowerWatts;
    std::optional<double> gpuPowerLimitWatts;
    std::optional<double> gpuTemperatureC;
    QDateTime gpuSampleUtc;

    QString sieveBound;
    QString compactPending;
    QString survivors;
    QString exactPending;
    QString segment;
    QString gapCandidates;
    QString cumulativeGapCandidates;
    QString leftGlobalIndex;
    QString rightGlobalIndex;

    QString bestSpan;
    QString bestStatus;
    QString bestGap;
    QString bestMerit;
    QString bestMagnitude;
    QString bestLogN;
    QString bestCandidateId;
    QString bestRow;
    QString bestBlockHeight;
    QString bestBlockHash;
    QString bestEndpointLeft;
    QString bestEndpointRight;

    QString producerStatus;
    QString verifierStatus;
    QString verifierThreads;
    QString verifierProcessId;
    QString verifierQueueSeen;
    QString verifierQueueClaimed;
    QString verifierQueueVerified;
    QString verifierQueueExact;
    QString verifierQueueErrors;
    std::optional<double> verifierItemsPerSecond;
    QString certificateStatus;
    QString certificateKind;
    QString certificateSha256;

    QJsonObject raw;
};


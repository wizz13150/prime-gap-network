#pragma once

#include <QString>
#include <QHash>

#include <optional>

namespace FrontierGpuControllerUi {

inline constexpr char CONFIG_SCHEMA[] =
    "gapminer-gpu-controller-config-v1";
inline constexpr char STATE_SCHEMA[] = "gapminer-gpu-controller-state-v1";
inline constexpr char TELEMETRY_SCHEMA[] =
    "gapminer-gpu-controller-telemetry-v1";
inline constexpr qint64 MAX_CONFIG_BYTES = 1024 * 1024;
inline constexpr qint64 MAX_STATE_BYTES = 4 * 1024 * 1024;
inline constexpr qint64 MAX_TELEMETRY_TAIL_BYTES = 512 * 1024;

struct DeviceStatus {
    QString uuid;
    bool configured{false};
    bool enabled{false};
    QString mode;
    std::optional<double> temperatureC;

    bool ownershipObserved{false};
    bool ownershipVerified{false};
    QString ownershipDetail;

    bool readbackObserved{false};
    bool readbackVerified{false};
    QString readbackDetail;

    bool restoreObserved{false};
    bool restoreVerified{false};
    QString restoreDetail;
};

struct Snapshot {
    bool configured{false};
    bool configValid{false};
    bool dryRunExplicit{false};
    bool liveMutationViolation{false};
    bool restoreOnExit{false};
    bool ownerRequired{false};
    bool stateAvailable{false};
    bool telemetryAvailable{false};
    double lowerC{82.0};
    double upperC{84.0};
    QString configPath;
    QString statePath;
    QString telemetryPath;
    QString error;
    QHash<QString, DeviceStatus> devices;

    QString controllerSummary() const;
    QString deviceSummary(const QString& uuid) const;
};

// Returns a safe, explicit disabled state for an omitted optional config.
Snapshot DisabledSnapshot();

// Reads only the controller's bounded config/state/telemetry artifacts. This
// function never creates files, writes requests, launches a process, or calls
// a GPU/system-control API.
bool LoadSnapshot(const QString& configPath, Snapshot& snapshot, QString& error);

} // namespace FrontierGpuControllerUi

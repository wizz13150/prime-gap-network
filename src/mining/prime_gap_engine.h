#pragma once
#include <QJsonObject>
#include <QHash>
#include <QStringList>
#include <optional>
#include "mining/retained_merit_policy_data.h"

namespace pgn::mining {
// Exact job representation used by the existing PGN controller. This object is
// a request, never reservation authority or a claim that GPU memory fits.
struct PrimeGapEngineJob {
    QString start, end, length;
    quint64 highWord{}, lowWord{}, positions{};
    int bits{}, descriptorK{};
    quint32 minimumGap{};
};
quint32 primeGapMinimumAt(const QString& position);
inline quint32 primeGapMinimumFloor(){return retention_policy::Steps.front().gap;}
QString primeGapNextThreshold(const QString& position);
QString primeGapSourceManifest();
std::optional<int> primeGapReleaseArchitecture(const QString& imageSha256);
QHash<QString,QString> primeGapRuntimePins(const QString& bundle,const QString& executable,const QString& imageSha256);
PrimeGapEngineJob primeGapEngineJob(const QString& start, const QString& end,
                                  int descriptorK, std::optional<quint32> minimum = {});
// The image name determines runtime K in the native entry point. Check the
// exact profile suffix before executing a registered artifact.
bool primeGapImageProfile(const QString& executable, int descriptorK);
QStringList primeGapPlanArguments(const PrimeGapEngineJob&, int visibleDeviceIndex);
QStringList primeGapRunArguments(const PrimeGapEngineJob&, int visibleDeviceIndex,
    const QString& checkpoint, const QString& gapLog, const QString& stopFile, quint32 ownerPid);
// Actual --plan echo, geometry and memory must match this exact job. This is
// plan consistency, not image/process authentication, launch or prime coverage.
QString validatePrimeGapPlan(const PrimeGapEngineJob&, const QJsonObject&);
}

#pragma once
#include <QJsonObject>
#include <QString>
#include "mining/prime_gap_engine.h"

namespace pgn::mining {
enum class RunSourceProfileStatus { Invalid, AbsentHistoricalUnattested, KnownLegacyA73, KnownBoundaryV2, KnownLazyV1, KnownLazySharedV1, KnownLazySharedPeriodicV1, KnownLazySharedPeriodicBankV1, KnownRadix70V1, KnownFloatPrefixV1, KnownBarrett70V1, KnownWindow2Recip5V1, KnownTop2V1, KnownRollingRootsV1, KnownRollingRootsLanes2V1, KnownCarryChainV1, KnownFusedSquareV1, KnownFusedHigh47V1, KnownFusedWindow3RingV1, KnownAdaptivePrimeGap };
struct RunSourceProfileInspection {
    RunSourceProfileStatus status{RunSourceProfileStatus::Invalid};
    QJsonObject profile;
    QString error;
    std::optional<PrimeGapEngineJob> engineJob;
};
// Pure schema/image-to-protocol mapping on the ORIGINAL immutable manifest.
// No current binding lookup, rewrite, backfill, file hash or process check.
// The caller still validates manifest/run/process provenance and RAW lineage.
// Known image mappings are metadata, NOT launch authentication or prime coverage.
RunSourceProfileInspection inspectRunSourceProfile(const QJsonObject& actualManifest);
// Operational protocol mapping, separate from successful RAW parsing. Empty
// means historical legacy grammar, NOT inferred image attestation. The exact
// qualified 74A mapping admits only its explicit v2 grammar, never legacy.
bool runSourceProtocolAdmitted(const RunSourceProfileInspection&, const QString& boundaryProtocol);
}

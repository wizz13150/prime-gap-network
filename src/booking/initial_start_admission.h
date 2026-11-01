#pragma once
#include "booking/source_registry.h"
#include "mining/next_work_policy.h"
#include <optional>

namespace pgn::booking {
struct InitialStartScope {
    QString executionStart,executionEnd,autoStart,autoEnd,blockAnchor,blockWidth;
};
// Caller declaration tied to its fresh observation. UUID/Ready are NOT an
// authenticated process observation, launch permission or physical inventory.
struct InitialBoundaryEvidence {
    pgn::mining::WorkBoundary boundary{pgn::mining::WorkBoundary::Unknown};
    QString observationUUID;
};
struct InitialStartDispatch {
    QString bookingId,start,end,device,state,origin;
    std::optional<QString> predecessor;
    std::optional<QString> run; // transported exactly for reconciliation, never invented
    std::optional<bool> automatic; // legacy pending origin may be UNKNOWN, not false/manual
};
enum class InitialStartStatus {Held,ResumeRequired,Prepared,ReconcilePrepared};
struct InitialStartResult {
    InitialStartStatus status{InitialStartStatus::Held};
    QString error;
    std::optional<InitialStartDispatch> dispatch;
    pgn::mining::MiningSessionMode mode{pgn::mining::MiningSessionMode::Unselected};
    bool replayed{};
};
// Pure exact validation/search, shared by the store, not another allocator.
// Search is O(registry rows * occupied intervals), no coordinate-space scan.
QString initialStartFingerprint(const QString& requestUUID,const QString& normalizedDevice,
    const InitialStartScope&,const SourceRegistry&,const InitialBoundaryEvidence&,QString* registryDigest=nullptr);
std::optional<QPair<QString,QString>> firstInitialFreeBlock(const InitialStartScope&,const SourceRegistry&,
    const QVector<QPair<QString,QString>>& occupied);
}

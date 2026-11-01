#pragma once
#include "booking/initial_start_admission.h"
#include <QJsonObject>

namespace pgn::booking {
// Actual, already committed SOURCE predecessor. The birth is preserved from
// its immutable process manifest, never restamped from an inventory time.
struct NextBoundarySource {
    QString run,birth,start,end;
    std::optional<QString> queuedIntent;
};
// Caller declaration only: the application pays fresh OFF/owned=false,
// operator_stop=false, SOURCE/conservation/lineage and its lifecycle latch.
// Neither this DTO nor a database reader authenticates that observation.
struct NextBoundaryEvidence {
    pgn::mining::WorkBoundary boundary{pgn::mining::WorkBoundary::Unknown};
    QString observationUUID;
};
enum class NextBoundaryStatus {Held,EndSession,Exhausted,Prepared,ReconcilePrepared};
struct NextBoundaryAdmission {
    NextBoundaryStatus status{NextBoundaryStatus::Held};
    QString error;
    // Reuse the existing exact storage tuple, NOT INITIAL_START semantics.
    // origin is QUEUED_ADVANCE; automatic describes this selection, whereas
    // mode below is session provenance and may remain ManualReservations.
    std::optional<InitialStartDispatch> dispatch;
    pgn::mining::MiningSessionMode mode{pgn::mining::MiningSessionMode::Unselected};
};
enum class PreparedNextReadStatus {Unavailable,Absent,Present};
struct PreparedNextAdmissionRead {
    PreparedNextReadStatus status{PreparedNextReadStatus::Unavailable};
    QString error;
    NextBoundaryAdmission admission;
    NextBoundarySource predecessor;
};
enum class ObservedDispatchReconcileStatus {Held,Unchanged,Reconciled};
struct ObservedDispatchReconcileResult {
    ObservedDispatchReconcileStatus status{ObservedDispatchReconcileStatus::Held};
    QString error,bookingId,run;
};
}

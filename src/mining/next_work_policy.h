#pragma once

namespace pgn::mining {
enum class MiningSessionMode { Unselected, Automatic, ManualReservations };
enum class WorkBoundary { Unknown, Running, Interrupted, Ready, Completed };
enum class NextWorkAction { Hold, ReconcileTransition, ResumeCurrent, StartManual, ClaimAvailable, EndSession };
struct NextWorkContext {
    bool applicationOpen{},miningEnabled{},transitionUncertain{},checkpointVerified{};
    bool manualWorkAvailable{},automaticAfterManual{};
    MiningSessionMode mode{MiningSessionMode::Unselected};
    WorkBoundary boundary{WorkBoundary::Unknown};
};
struct NextWorkDecision {
    NextWorkAction action{NextWorkAction::Hold};
    MiningSessionMode mode{MiningSessionMode::Unselected};
};
// Only an EXPLICIT user start establishes an initial mode. An empty queue
// observed after manual work is NOT a new automatic-start authorization.
constexpr MiningSessionMode explicitMiningStart(bool hasManualReservations){
    return hasManualReservations?MiningSessionMode::ManualReservations:MiningSessionMode::Automatic;
}
// Pure policy, not allocation/execution. The application controller must
// persist an accepted transition through the one existing booking store;
// it must verify ownership/receipts before declaring a boundary or checkpoint.
constexpr NextWorkDecision chooseNextWork(const NextWorkContext& c){
    const auto decision=[&](NextWorkAction action,MiningSessionMode mode){return NextWorkDecision{action,mode};};
    if(!c.applicationOpen||!c.miningEnabled||c.mode==MiningSessionMode::Unselected)
        return decision(NextWorkAction::Hold,c.mode);
    if(c.transitionUncertain)return decision(NextWorkAction::ReconcileTransition,c.mode);
    if(c.boundary==WorkBoundary::Unknown||c.boundary==WorkBoundary::Running)
        return decision(NextWorkAction::Hold,c.mode);
    if(c.boundary==WorkBoundary::Interrupted)
        return decision(c.checkpointVerified?NextWorkAction::ResumeCurrent:NextWorkAction::Hold,c.mode);
    if(c.manualWorkAvailable)return decision(NextWorkAction::StartManual,MiningSessionMode::ManualReservations);
    if(c.mode==MiningSessionMode::Automatic||c.automaticAfterManual)
        return decision(NextWorkAction::ClaimAvailable,c.mode);
    return decision(NextWorkAction::EndSession,c.mode);
}
}

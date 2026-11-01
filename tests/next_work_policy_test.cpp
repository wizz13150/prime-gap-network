#include "mining/next_work_policy.h"
#include <iostream>
#include <stdexcept>

using namespace pgn::mining;
namespace {int checks=0;void require(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}}
int main(){
    try{
        require(explicitMiningStart(false)==MiningSessionMode::Automatic,"explicit start without reservations selects automatic mode");
        require(explicitMiningStart(true)==MiningSessionMode::ManualReservations,"explicit start with reservations selects manual mode");
        NextWorkContext c;c.applicationOpen=true;c.miningEnabled=true;c.boundary=WorkBoundary::Ready;c.mode=explicitMiningStart(false);
        auto d=chooseNextWork(c);require(d.action==NextWorkAction::ClaimAvailable,"automatic start claims only at processing boundary");
        c.manualWorkAvailable=true;d=chooseNextWork(c);require(d.action==NextWorkAction::StartManual&&d.mode==MiningSessionMode::ManualReservations,"new manual reservations take priority and retain manual origin");
        c.mode=d.mode;c.manualWorkAvailable=false;c.boundary=WorkBoundary::Completed;
        require(chooseNextWork(c).action==NextWorkAction::EndSession,"empty manual queue does not masquerade as automatic initial start");
        c.manualWorkAvailable=true;require(chooseNextWork(c).action==NextWorkAction::StartManual,"OFF fallback never stops between pending manual reservations");
        c.manualWorkAvailable=false;c.automaticAfterManual=true;
        require(chooseNextWork(c).action==NextWorkAction::ClaimAvailable,"explicit fallback ON continues only after manual queue exhausted");
        c.miningEnabled=false;require(chooseNextWork(c).action==NextWorkAction::Hold,"toggling fallback is not mining start authority");
        c.miningEnabled=true;c.applicationOpen=false;require(chooseNextWork(c).action==NextWorkAction::Hold,"application closure dominates automatic fallback");
        c.applicationOpen=true;c.boundary=WorkBoundary::Interrupted;
        require(chooseNextWork(c).action==NextWorkAction::Hold,"unknown checkpoint cannot skip interrupted work");
        c.checkpointVerified=true;require(chooseNextWork(c).action==NextWorkAction::ResumeCurrent,"validated interrupted range resumes before new automatic allocation");
        c.manualWorkAvailable=true;require(chooseNextWork(c).action==NextWorkAction::ResumeCurrent,"resume current work precedes another manual reservation");
        c.transitionUncertain=true;require(chooseNextWork(c).action==NextWorkAction::ReconcileTransition,"uncertain prepared job reconciles instead of allocating another");
        for(const auto mode:{MiningSessionMode::Unselected,MiningSessionMode::Automatic,MiningSessionMode::ManualReservations})
        for(const auto boundary:{WorkBoundary::Unknown,WorkBoundary::Running,WorkBoundary::Interrupted,WorkBoundary::Ready,WorkBoundary::Completed})
        for(unsigned bits=0;bits<64;++bits){
            NextWorkContext x{bool(bits&1),bool(bits&2),bool(bits&4),bool(bits&8),bool(bits&16),bool(bits&32),mode,boundary};
            const auto result=chooseNextWork(x);
            if(!x.applicationOpen||!x.miningEnabled||mode==MiningSessionMode::Unselected)
                require(result.action==NextWorkAction::Hold,"no application/start authorization means no work");
            else if(x.transitionUncertain)
                require(result.action==NextWorkAction::ReconcileTransition,"all uncertain transitions must reconcile");
            else if(boundary==WorkBoundary::Running||boundary==WorkBoundary::Unknown)
                require(result.action==NextWorkAction::Hold,"no lookahead dispatch while running or ownership unknown");
            else if(boundary==WorkBoundary::Interrupted)
                require(result.action==(x.checkpointVerified?NextWorkAction::ResumeCurrent:NextWorkAction::Hold),"interrupted checkpoint boundary never falls through to queue");
            else if(x.manualWorkAvailable)
                require(result.action==NextWorkAction::StartManual&&result.mode==MiningSessionMode::ManualReservations,"manual priority independent of fallback switch");
            else if(mode==MiningSessionMode::ManualReservations&&!x.automaticAfterManual)
                require(result.action==NextWorkAction::EndSession,"manual-only session exhausts once, never automatic on next empty poll");
            else require(result.action==NextWorkAction::ClaimAvailable,"authorized automatic boundary may propose a claim, not a reservation ahead");
        }
        std::cout<<"Next-work policy: "<<checks<<" checks, 0 failures; pure decisions, no booking/process/GPU operation\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Next-work policy failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}

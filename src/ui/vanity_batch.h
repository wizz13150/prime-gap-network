#pragma once
#include <cstdint>
#include <limits>

namespace pgn::ui {
// One protected, verified address completes one item. Async completions carry
// an item token that is never reused across batches, so a cancelled worker or
// duplicate callback cannot advance a later search.
class VanityBatch {
public:
    enum class Phase { Idle, Running, Stopping, Finishing };
    enum class Completion { Ignored, Next, Finish };
    bool start(int target) {
        if (phase_!=Phase::Idle || target<0 || target>1000) return false;
        target_=target;found_=0;active_=0;phase_=Phase::Running;return true;
    }
    std::uint64_t beginItem() {
        if (phase_!=Phase::Running || active_ || serial_==std::numeric_limits<std::uint64_t>::max()) return 0;
        active_=++serial_;return active_;
    }
    Completion accept(std::uint64_t token) {
        if (!token || token!=active_ || (phase_!=Phase::Running&&phase_!=Phase::Stopping)) return Completion::Ignored;
        active_=0;++found_;
        if (phase_==Phase::Stopping || (target_&&found_>=std::uint64_t(target_))) {phase_=Phase::Finishing;return Completion::Finish;}
        return Completion::Next;
    }
    void stop() {
        if (phase_==Phase::Running) phase_=active_?Phase::Stopping:Phase::Finishing;
    }
    bool fail(std::uint64_t token) {
        if (!token || active_!=token) return false;
        active_=0;phase_=Phase::Finishing;return true;
    }
    bool finish() {
        if (phase_!=Phase::Finishing || active_) return false;
        phase_=Phase::Idle;return true;
    }
    Phase phase()const{return phase_;}
    std::uint64_t found()const{return found_;}
    int target()const{return target_;} // zero means continuous
private:
    Phase phase_{Phase::Idle};int target_{};
    std::uint64_t found_{},serial_{},active_{};
};
}

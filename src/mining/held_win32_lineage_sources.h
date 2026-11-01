#pragma once
#include "mining/source_mirror.h"
#include "mining/source_resume_lineage_verifier.h"

namespace pgn::mining {
// Product adapter for the existing synchronous native verifier. Construct on
// the mirror owner thread BEFORE dispatching an explicitly owned worker; keep
// this scope/opener alive through verify(). No event pump, polling, thread or
// source writer is created. It is NOT a process/ownership/durability proof.
class HeldWin32LineageSources final {
public:
    explicit HeldWin32LineageSources(SourceMirror&);
    HeldWin32LineageSources(const HeldWin32LineageSources&)=delete;
    HeldWin32LineageSources& operator=(const HeldWin32LineageSources&)=delete;
    HeldWin32LineageSources(HeldWin32LineageSources&&) noexcept=default;
    HeldWin32LineageSources& operator=(HeldWin32LineageSources&&) noexcept=default;
    // Captures OWNED shared freeze state, never a reference to this scope.
    // Every opener copy and open reader prolongs the writer barrier. Destroy
    // verifier/opener/last reader to release; cancellation also must unwind.
    OpenHeldLineageSource opener() const;
private:
    std::shared_ptr<SourceMirrorReadFreeze> freeze_;
};
}

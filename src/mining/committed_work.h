#pragma once
#include <cstdint>
#include <mutex>
#include <optional>

namespace pgn::mining {
// A source work-commit observation is separate from a periodic counter read.
// The old raw stream remains untouched. Values and age are never interpolated,
// averaged, or freshened by a timer/duplicate/physical-control transition.
struct CommittedWork {
    uint64_t sourceTimeNs{},positions{},tests{},batches{},gaps{},processedGaps{};
};
enum class CommitResult { published, duplicate, invalid };
class CommittedWorkChannel {
public:
    // One producer per engine run/lane. Readers only copy a fixed-size object
    // inside the lock; no logging, callbacks, verification or waits under it.
    CommitResult publish(const CommittedWork& value){
        std::lock_guard<std::mutex> lock(mutex_);
        if(!value.sourceTimeNs)return CommitResult::invalid;
        if(latest_){const auto& old=*latest_;
            if(value.positions<old.positions||value.tests<old.tests||value.batches<old.batches||
               value.gaps<old.gaps||value.processedGaps<old.processedGaps||value.sourceTimeNs<old.sourceTimeNs)
                return CommitResult::invalid;
            if(value.positions==old.positions&&value.tests==old.tests&&value.batches==old.batches&&
               value.gaps==old.gaps&&value.processedGaps==old.processedGaps)return CommitResult::duplicate;
            if(value.sourceTimeNs==old.sourceTimeNs)return CommitResult::invalid;
        }
        latest_=value;return CommitResult::published;
    }
    std::optional<CommittedWork> observe()const{
        std::lock_guard<std::mutex> lock(mutex_);return latest_;
    }
private:
    mutable std::mutex mutex_;
    std::optional<CommittedWork> latest_;
};
}

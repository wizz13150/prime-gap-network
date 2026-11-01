// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <pgn/candidate_range_schedule.h>
#include <memory>

namespace pgn::core {
// Local verified replay progress, not a caller-supplied availability snapshot.
// A partial slice never returns a usable schedule. An incompatible branch
// resets replay; immutable orphan archive records are never adopted as current.
class CandidateRangeScheduleReader {
    struct State;
    std::unique_ptr<State> m_state;
    bool m_more_work{};
public:
    CandidateRangeScheduleReader();
    ~CandidateRangeScheduleReader();
    CandidateRangeScheduleReader(const CandidateRangeScheduleReader&)=delete;
    CandidateRangeScheduleReader& operator=(const CandidateRangeScheduleReader&)=delete;
    RangeScheduleCheck Read(const CCoinsView&,const CBlockIndex&,const ChainRangePolicy&,
        const NativeRequestHistoryArchive&,RangeScheduleBudget);
    int32_t ProgressHeight()const;
    bool MoreWorkPending()const{return m_more_work;}
};
}

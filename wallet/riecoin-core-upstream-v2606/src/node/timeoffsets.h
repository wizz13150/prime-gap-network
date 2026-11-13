// Copyright (c) 2024-present The Bitcoin Core developers
// Copyright (c) 2013-present The Riecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_TIMEOFFSETS_H
#define BITCOIN_NODE_TIMEOFFSETS_H

#include <sync.h>

#include <chrono>
#include <cstddef>
#include <deque>
#include <limits>

namespace node {
class Warnings;
} // namespace node

class TimeOffsets
{
public:
    TimeOffsets(node::Warnings& warnings) : m_warnings{warnings} {}

private:
    //! Maximum number of timeoffsets stored.
    static constexpr size_t MAX_SIZE{50};

    mutable Mutex m_mutex;
    /** The observed time differences between our local clock and those of our outbound peers. A
     * positive offset means our peer's clock is ahead of our local clock. */
    std::deque<std::chrono::seconds> m_offsets GUARDED_BY(m_mutex){};

    node::Warnings& m_warnings;

public:
    /** Reference-network admission bound; native PGN uses this for warnings only.
     * A VERSION time sample includes unmeasured one-way delivery delay. */
    static constexpr std::chrono::seconds MAX_TIME_OFFSET{5};

    /** Compare VERSION time with socket receipt, not delayed message handling. */
    static constexpr std::chrono::seconds FromVersionTime(std::chrono::seconds peer_time, std::chrono::seconds receipt_time)
    {
        using Rep = std::chrono::seconds::rep;
        const Rep peer{peer_time.count()}, receipt{receipt_time.count()};
        constexpr auto low{std::numeric_limits<Rep>::min()};
        constexpr auto high{std::numeric_limits<Rep>::max()};
        if (receipt > 0 && peer < low + receipt) return std::chrono::seconds{low};
        if (receipt < 0 && peer > high + receipt) return std::chrono::seconds{high};
        return std::chrono::seconds{peer - receipt};
    }

    /** Add a new time offset sample. */
    void Add(std::chrono::seconds offset) EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /** Compute and return the median of the collected time offset samples. The median is returned
     * as 0 when there are less than 5 samples. */
    std::chrono::seconds Median() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);

    /** Raise warnings if the median time offset exceeds the warnings threshold. Returns true if
     * warnings were raised. */
    bool WarnIfOutOfSync() const EXCLUSIVE_LOCKS_REQUIRED(!m_mutex);
};

#endif // BITCOIN_NODE_TIMEOFFSETS_H

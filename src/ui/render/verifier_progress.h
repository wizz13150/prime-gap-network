#pragma once

#include <algorithm>
#include <cstdint>

// The verifier heartbeat counters are scoped to the current worker generation,
// while immutable receipts span every generation.  Keeping both namespaces
// explicit prevents displays such as "2/13", which compare unlike counters.
struct FrontierVerifierProgress {
    std::uint64_t sessionClaimed{0};
    std::uint64_t sessionVerified{0};
    std::uint64_t sessionExact{0};
    std::uint64_t sessionErrors{0};
    std::uint64_t sourceSeenTotal{0};
    std::uint64_t immutableReceiptTotal{0};

    [[nodiscard]] std::uint64_t sessionTotal() const
    {
        return std::max({sessionClaimed, sessionVerified, sessionExact});
    }

    [[nodiscard]] std::uint64_t sessionPending() const
    {
        return sessionClaimed > sessionVerified ? sessionClaimed - sessionVerified : 0;
    }

    [[nodiscard]] std::uint64_t totalVerified() const
    {
        return std::min(sourceSeenTotal, immutableReceiptTotal);
    }

    [[nodiscard]] std::uint64_t totalPending() const
    {
        return sourceSeenTotal > totalVerified() ? sourceSeenTotal - totalVerified() : 0;
    }

    [[nodiscard]] bool caughtUp() const
    {
        return sessionErrors == 0 && sessionPending() == 0 && totalPending() == 0;
    }
};


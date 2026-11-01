#include "engine/cuda/walk_slice_partition.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>

static bool check(uint64_t slots, uint64_t count) {
    uint64_t cursor = 0, shortest = slots, longest = 0;
    for (uint64_t job = 0; job < count; ++job) {
        const auto s = pgn_walk::slice(slots, count, job);
        if (s.lo != cursor || s.hi <= s.lo || s.hi > slots) return false;
        shortest = std::min(shortest, s.hi - s.lo);
        longest = std::max(longest, s.hi - s.lo);
        cursor = s.hi;
    }
    return cursor == slots && longest - shortest <= 1;
}
int main() {
    uint64_t cases = 0;
    for (uint64_t slots = 1; slots <= 1025; ++slots)
        for (uint64_t count = 1; count <= std::min<uint64_t>(slots, 97); ++count) {
            if (!check(slots, count)) return 1;
            ++cases;
        }
    for (const uint64_t slots : {uint64_t(1)<<28, (uint64_t(1)<<28)*65,
                                (uint64_t(1)<<28)*97, UINT64_MAX})
        for (const uint64_t count : {uint64_t(31),uint64_t(32),uint64_t(33),
                                     uint64_t(164)*512,uint64_t(164)*512*8}) {
            if (!check(slots, count)) return 2;
            ++cases;
        }
    for (const auto invalid : {pgn_walk::slice(0,0,0),pgn_walk::slice(7,8,0),
                               pgn_walk::slice(7,7,7)})
        if (invalid.lo || invalid.hi) return 3;
    const uint64_t slots = (uint64_t(1)<<28)*65, workers = 164*512;
    std::cout << "PASS " << cases << " exact disjoint complete balanced partitions; "
              << "legacy final slice=" << slots/workers + slots%workers
              << ", other legacy slices=" << slots/workers
              << "; no primality, throughput or coverage claim\n";
}

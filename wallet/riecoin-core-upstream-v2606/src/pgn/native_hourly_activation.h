// Private task13 integration. The shipped daemon has no hourly activation.
#pragma once
#include <cstdint>
#include <optional>
#include <uint256.h>
namespace pgn::core {
struct NativeHourlyMeritActivation {
    uint256 genesis;
    int64_t median_time;
};
// An explicit chain-bound rule is required. Local RPC settings never activate
// a new scientific threshold or change historical blocks.
std::optional<NativeHourlyMeritActivation> ConfiguredNativeHourlyMerit(const uint256& genesis);
}

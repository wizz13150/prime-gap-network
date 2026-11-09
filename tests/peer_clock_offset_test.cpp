#include <node/timeoffsets.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace std::chrono_literals;

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    try {
        constexpr auto arrival = 1794212940s;
        static_assert(TimeOffsets::MAX_TIME_OFFSET == 5s);
        for (const auto delay : {0s, 16s, 600s}) {
            for (const auto skew : {-6s, -5s, 0s, 1s, 5s, 6s}) {
                const auto measured = TimeOffsets::FromVersionTime(arrival + skew, arrival);
                require(measured == skew, "Processing delay changed peer clock measurement");
                require((std::chrono::abs(measured) <= TimeOffsets::MAX_TIME_OFFSET) ==
                        (skew >= -5s && skew <= 5s), "Five-second admission boundary changed");
                // Reproduce the old false rejection after a busy processing queue.
                if (delay >= 16s && skew == 1s)
                    require(std::chrono::abs(arrival + skew - (arrival + delay)) >
                            TimeOffsets::MAX_TIME_OFFSET, "Old queue-delay regression not reproduced");
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

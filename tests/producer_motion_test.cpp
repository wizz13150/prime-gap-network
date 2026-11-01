#include "ui/render/producer_rail.h"
#include <iostream>
#include <limits>
int main() {
    const auto zero = FrontierFirstEmittedProducerRate({0.0, 1000000000.0});
    if (!zero || *zero != 0.0) return 1;
    const auto missing = FrontierFirstEmittedProducerRate({std::nullopt, 17.0});
    if (!missing || *missing != 17.0) return 2;
    if (FrontierFirstEmittedProducerRate({std::nullopt, -1.0,
        std::numeric_limits<double>::quiet_NaN()})) return 3;
    const double phase = .375;
    const double ratio = FrontierProducerRelativeSpeed(*zero, 1000000000.0);
    if (ratio != 0.0 || FrontierAdvanceProducerPhase(phase, ratio, 125) != phase) return 4;
    if (FrontierProducerRelativeSpeed(20.0, 40.0) != .5 ||
        std::abs(FrontierAdvanceProducerPhase(phase,.5,125) - .385) > 1e-14) return 5;
    std::cout << "PASS emitted zero remains zero despite positive secondary metric; "
              << "missing distinct from zero; raw velocity has no interpolation\n";
}

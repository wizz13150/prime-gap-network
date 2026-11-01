#include "ui/render/gap_display_filter.h"
#include <iostream>
#include <limits>

int main() {
    int checks = 0, failures = 0;
    const auto check = [&](bool ok) { ++checks; if (!ok) ++failures; };
    const auto near = [](double a, double b) { return std::abs(a-b) < 1e-11; };
    using pgn::ui::GapDisplayFilter;
    GapDisplayFilter filter;
    check(filter.observe(10., "run", 0) == 10.);
    const double step = *filter.observe(30., "run", 2000000000ULL);
    check(near(step, 10. + 20. * (1. - std::exp(-1.))));
    for (int i = 0; i < 1000; ++i)
        check(filter.observe(30., "run", 2000000000ULL) == step);
    check(filter.observe(99., "run", 2000000000ULL) == 99.); // correction
    check(filter.observe(7., "new-run", 2000000001ULL) == 7.);
    check(filter.observe(8., "new-run", 1) == 8.); // clock reset
    check(!filter.observe(std::nullopt, "new-run", 2));
    check(filter.observe(4., "new-run", 3) == 4.); // no hidden old value
    check(!filter.observe(-1., "new-run", 4));
    check(!filter.observe(std::numeric_limits<double>::infinity(), "run", 5));
    check(!filter.observe(std::numeric_limits<double>::quiet_NaN(), "run", 6));
    check(filter.observe(3., "", 7) == 3.);
    check(filter.observe(2., "run", std::nullopt) == 2.);
    check(filter.observe(0., "run", 8) == 0.); // real known zero
    const auto rawZero = std::optional<double>(0.);
    check(filter.observe(10., "run", 9).has_value());
    check(filter.observe(rawZero, "run", 10) > 0.); // zero is a real input, not missing
    check(rawZero == 0.); // source not mutated
    GapDisplayFilter single, divided;
    single.observe(0., "same", 0); divided.observe(0., "same", 0);
    const auto end = single.observe(100., "same", 2000000000ULL);
    for (std::uint64_t t=500000000; t<=2000000000; t+=500000000)
        divided.observe(100., "same", t);
    check(near(*end, *divided.observe(100., "same", 2000000000ULL)));
    check(GapDisplayFilter(0.).observe(11., "run", 9) == 11.);
    GapDisplayFilter huge;
    huge.observe(std::numeric_limits<double>::max(), "huge", 0);
    check(std::isfinite(*huge.observe(0., "huge", std::numeric_limits<std::uint64_t>::max())));
    std::cout << "Gap display filter: checks=" << checks << " failures=" << failures
              << "; producer-clock only, no window/telemetry mutation\n";
    return failures ? 1 : 0;
}

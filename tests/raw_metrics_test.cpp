#include "mining/raw_metrics.h"
#include "ui/render/display_rate.h"
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace pgn::mining;
using namespace std::chrono_literals;

int main() {
    int checks = 0;
    const auto check = [&](bool condition) { ++checks; if (!condition) { std::cerr << "FAIL " << checks << '\n'; std::exit(1); } };
    check(FrontierDisplayedRate(0.0,206000000000.0)==0.0);
    check(FrontierDisplayedRate(std::nullopt,0.0)==0.0);
    check(!FrontierDisplayedRate(std::nullopt,std::nullopt));
    RawMetrics metrics;
    CounterSample s{"run-a", "3090", 1, 1s, 100, 2, 10};
    check(metrics.accept(s).status == SampleStatus::Baseline);
    s.sequence = 2; s.sourceTime = 2s; s.positions = 500; s.retainedGaps = 5; s.primalityTests = 18;
    const auto first = metrics.accept(s);
    check(first.interval && first.interval->positionsPerSecond == 400);
    check(first.interval->retainedGapsPerSecond == 3 && first.interval->testsPerSecond == 8);
    check(first.interval->retainedGapsPerBillionPositions == 7500000);
    s.sequence = 3; s.sourceTime = 3s;
    const auto dip = metrics.accept(s);
    check(dip.interval && dip.interval->positionsPerSecond == 0 && dip.interval->retainedGapsPerSecond == 0);
    check(!dip.interval->retainedGapsPerBillionPositions);
    check(metrics.accept(s).status == SampleStatus::OutOfOrder);
    auto old = s; old.sequence = 4; old.sourceTime = 2s;
    check(metrics.accept(old).status == SampleStatus::OutOfOrder && metrics.latest()->sequence == 3);
    s.sequence = 4; s.sourceTime = 4s; s.positions = 0;
    check(metrics.accept(s).status == SampleStatus::CounterReset);
    s.run = "run-b"; s.sequence = 0; s.sourceTime = 1s;
    check(metrics.accept(s).status == SampleStatus::IdentityChanged);
    auto second = *first.interval; second.source.device = "3060";
    auto total = simultaneousTotal({*first.interval, second});
    check(total && total->positions == 800 && total->retainedGapsPerSecond == 6);
    second.begin += 1ns;
    check(!simultaneousTotal({*first.interval, second}));
    check(!simultaneousTotal({*first.interval, *first.interval}));
    second = *first.interval; second.source.device = "3060";
    second.positions = std::numeric_limits<std::uint64_t>::max();
    check(!simultaneousTotal({*first.interval, second}));
    metrics.clear();
    check(!metrics.latest());
    s={"run-c","3090",1,1s,100,2,10,8};
    check(metrics.accept(s).status==SampleStatus::Baseline);
    s.sequence=2;s.sourceTime=2s;s.positions=300;s.retainedGaps=6;s.primalityTests=20;s.processedGaps=18;
    const auto distinct=metrics.accept(s);
    check(distinct.interval && distinct.interval->processedGapsPerSecond==10 && distinct.interval->retainedGapsPerSecond==4);
    check(distinct.interval->retainedFraction==0.4);
    s.sequence=3;s.sourceTime=3s;s.processedGaps.reset();
    const auto missing=metrics.accept(s);
    check(missing.interval && !missing.interval->processedGapsPerSecond && !missing.interval->retainedFraction);
    s.sequence=4;s.sourceTime=4s;s.processedGaps=23;
    check(!metrics.accept(s).interval->processedGapsPerSecond);
    s.sequence=5;s.sourceTime=5s;s.processedGaps=1;
    check(metrics.accept(s).status==SampleStatus::CounterReset);
    auto independent=*distinct.interval;independent.source.device="3060";independent.processedGaps.reset();
    check(!simultaneousTotal({*distinct.interval,independent})->processedGapsPerSecond);
    SourceRateWindow window;
    CounterSample burst{"window", "3090", 1, 0s, 0, 0, 0};
    check(!window.accept(burst));
    burst.sequence=2;burst.sourceTime=5s;burst.positions=1000;burst.primalityTests=100;
    auto averaged=window.accept(burst);
    check(averaged && averaged->duration==5s && averaged->positionsPerSecond==200);
    burst.sequence=3;burst.sourceTime=6s; // A real stalled interval is included.
    averaged=window.accept(burst);
    check(averaged && averaged->positions==1000 && averaged->duration==6s);
    burst.sequence=4;burst.sourceTime=7s;burst.positions=1400;burst.primalityTests=140;
    averaged=window.accept(burst);
    check(averaged && averaged->positionsPerSecond==200 && averaged->testsPerSecond==20);
    auto replay=burst;replay.sequence=3;replay.positions=0;
    check(!window.accept(replay));
    burst.sequence=5;burst.sourceTime=67s;burst.positions=13400;burst.primalityTests=1340;
    averaged=window.accept(burst);
    check(averaged && averaged->begin==7s && averaged->duration==60s && averaged->positionsPerSecond==200);
    burst.sequence=6;burst.sourceTime=68s;burst.positions=0;
    check(!window.accept(burst)); // Counter reset cannot inherit the old mean.
    burst.sequence=7;burst.sourceTime=73s;
    averaged=window.accept(burst);
    check(averaged && averaged->positionsPerSecond==0);
    burst.run="new-window";burst.sequence=1;burst.sourceTime=0s;
    check(!window.accept(burst));
    burst.sequence=2;burst.sourceTime=4s;burst.positions=800;
    check(!window.accept(burst));
    window.clear();burst.sequence=3;burst.sourceTime=10s;
    check(!window.accept(burst));
    std::cout << checks << " raw/window interval checks PASS\n";
}

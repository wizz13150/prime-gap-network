#include "ui/mining_summary.h"
#include "mining/cpu_activity.h"
#include <QCoreApplication>
#include <iostream>
int main(int argc,char** argv){QCoreApplication app(argc,argv);FrontierTelemetrySnapshot a,b;
 a.runId="run-a";a.gpuUuid="gpu-a";a.rowsCompleted="18446744073709551615";a.cumulativeGapCandidates="3";
 a.e2ePositionsPerSecond=100.;a.powmPerSecond=9.;a.gapPairsTestedPerSecond=1.;a.raw={{"pgn_process_running",true},{"pgn_source_interval_ns","200000000"},{"pgn_source_time_ns","1000000000"},{"pgn_clock_namespace","wizz-source"},{"pgn_cumulative_tests","25"},{"pgn_cumulative_processed_gaps","5"},{"pgn_processed_gaps_per_second",2.}};
 auto x=pgn::ui::summarizeMining({&a,&a});if(x.devices!=1||x.positions!=a.rowsCompleted||!x.yield||*x.yield!=1e7)return 1;
 b=a;b.runId="run-b";b.gpuUuid="gpu-b";b.rowsCompleted="5";x=pgn::ui::summarizeMining({&a,&b});
 if(x.positions!="18446744073709551620"||x.tests!="50"||x.processed!="10"||x.retained!="6"||x.positionsRate!=200.)return 2;
 b.raw.insert("pgn_source_time_ns","1000000001");x=pgn::ui::summarizeMining({&a,&b});if(x.positionsRate||x.yield||x.devices!=2)return 3;
 b=a;b.gpuUuid="gpu-b";b.raw.remove("pgn_clock_namespace");if(pgn::ui::summarizeMining({&a,&b}).positionsRate)return 4;
 a.e2ePositionsPerSecond=0.;x=pgn::ui::summarizeMining({&a});if(x.positionsRate!=0.||x.yield)return 5;
 if(pgn::ui::summarizeMining({&a},{"run-a"}).positions!="0")return 6;
 FrontierTelemetrySnapshot missing;missing.gpuUuid="missing";
 if(!pgn::ui::summarizeMining({&missing}).countsIncomplete)return 7;
 // Reproduce a live private3060 control packet: running, but no production
 // counts or clock. Keep the real zero/source counters exactly, in either order.
 missing.raw={{"pgn_test_only",true},{"pgn_process_running",true}};
 for(const auto& lanes:{QVector<const FrontierTelemetrySnapshot*>{&a,&missing},
                       QVector<const FrontierTelemetrySnapshot*>{&missing,&a},
                       QVector<const FrontierTelemetrySnapshot*>{nullptr,&missing,&a}}){
  const auto observed=pgn::ui::summarizeMining(lanes);
  const auto expected=pgn::ui::summarizeMining({&a});
  if(observed.devices!=1||observed.running!=1||observed.countsIncomplete||
     observed.positions!=expected.positions||observed.tests!=expected.tests||
     observed.processed!=expected.processed||observed.retained!=expected.retained||
     observed.positionsRate!=expected.positionsRate||observed.testsRate!=expected.testsRate||
     observed.processedRate!=expected.processedRate||observed.retainedRate!=expected.retainedRate||
     observed.sourceAgeLowerBoundNs!=expected.sourceAgeLowerBoundNs||!observed.windowsAligned)return 8;
 }
 missing.rowsCompleted="999999999999999999999";missing.e2ePositionsPerSecond=999e9;
 if(pgn::ui::summarizeMining({&a,&missing}).positions!=a.rowsCompleted)return 9;
 if(pgn::ui::summarizeMining({&missing}).devices!=0)return 10;
 missing.raw.remove("pgn_test_only");
 if(!pgn::ui::summarizeMining({&a,&missing}).countsIncomplete)return 11;
 b=a;b.gpuUuid="gpu-b";b.runId="run-b";
 const auto ab=pgn::ui::summarizeMining({&a,&b});
 const auto ba=pgn::ui::summarizeMining({&b,&a});
 if(ab.sourceTimeNs!="1000000000"||ab.displayClockIdentity.isEmpty()||
    ab.displayClockIdentity!=ba.displayClockIdentity)return 12;
 b.runId="new-run-b";
 if(pgn::ui::summarizeMining({&a,&b}).displayClockIdentity==ab.displayClockIdentity)return 13;
 if(pgn::ui::summarizeMining({&a}).displayClockIdentity==ab.displayClockIdentity)return 14;
 b.raw.remove("pgn_clock_namespace");
 if(!pgn::ui::summarizeMining({&b}).displayClockIdentity.isEmpty())return 15;
 a.raw.insert("pgn_source_window_ns","60000000000");a.raw.insert("pgn_window_tests_per_second",20.);
 a.raw.insert("pgn_window_retained_per_second",2.);a.raw.insert("pgn_window_processed_per_second",3.);a.e2eWindowPositionsPerSecond=200.;
 x=pgn::ui::summarizeMiningForDisplay({&a});
 if(x.positionsRate!=200.||x.testsRate!=20.||x.processedRate!=3.||x.retainedRate!=2.||x.yield!=1e7||x.rateCaption!="MEAN 60.0s"||x.intervalNs!="60000000000")return 16;
 if(pgn::ui::sourceWindowProcessed(a)!=x.processedRate||pgn::ui::sourceWindowYield(a)!=x.yield)return 22;
 // Changing the presentation must not mutate or substitute the raw evidence.
 if(pgn::ui::summarizeMining({&a}).positionsRate!=0.||a.powmPerSecond!=9.||a.e2ePositionsPerSecond!=0.)return 17;
 b=a;b.gpuUuid="gpu-b";b.runId="run-b";
 if(pgn::ui::summarizeMiningForDisplay({&a,&b}).positionsRate!=400.)return 18;
 b.raw.insert("pgn_source_window_ns","59900000000");
 if(pgn::ui::summarizeMiningForDisplay({&a,&b}).positionsRate)return 19;
 b.raw.remove("pgn_clock_namespace");
 if(!pgn::ui::summarizeMiningForDisplay({&b}).displayClockIdentity.isEmpty())return 20;
 a.raw.insert("pgn_source_window_ns","bad");
 if(pgn::ui::hasSourceRateWindow(a)||pgn::ui::summarizeMiningForDisplay({&a}).positionsRate!=0.)return 21;
 std::cout<<"PASS 21 raw/window summary scenarios: exact sums, deduplication, zero, isolation, labelled means, raw conservation, unknown/unaligned window refusal\n";
 pgn::ui::TotalCardRateWindow totalWindow;FrontierTelemetrySnapshot live=a;
 live.runId="mean-run";live.gpuUuid="mean-gpu";live.rowsCompleted="0";live.cumulativeGapCandidates="0";
 live.raw.insert("pgn_source_time_ns","1000000000");live.raw.insert("pgn_cumulative_tests","0");live.raw.insert("pgn_cumulative_processed_gaps","0");
 pgn::mining::stampSourceAge(live.raw,1000000000,1000000000,pgn::mining::receiptMonotonicNs());
 live.observedUtc=QDateTime::currentDateTimeUtc();live.e2ePositionsPerSecond=1400.;
 const auto warming=totalWindow.observe({&live});if(warming.positionsRate)return 30;
 live.rowsCompleted="5000";live.cumulativeGapCandidates="50";live.raw.insert("pgn_source_time_ns","11000000000");
 live.raw.insert("pgn_cumulative_tests","10000");live.raw.insert("pgn_cumulative_processed_gaps","60");
 const auto average=totalWindow.observe({&live});
 if(average.positionsRate!=500.||average.processedRate!=6.||average.retainedRate!=5.||live.e2ePositionsPerSecond!=1400.)return 31;
 live.runId="next-run";if(totalWindow.observe({&live}).positionsRate)return 32;
 std::cout<<"PASS TOTAL-only burst integration, raw GPU isolation, source-run reset\n";

    FrontierTelemetrySnapshot gpu,cpu;
    gpu.gpuUuid="gpu-live";gpu.runId="gpu-ticket";gpu.authoritative=true;gpu.observedUtc=QDateTime::currentDateTimeUtc();gpu.rowsCompleted="1000";gpu.cumulativeGapCandidates="3";gpu.e2ePositionsPerSecond=1e12;gpu.powmPerSecond=2.6e9;gpu.gapPairsTestedPerSecond=3.;
    gpu.raw={{"pgn_process_running",true},{"pgn_clock_namespace","remote-gpu"},{"pgn_source_time_ns","1000000000"},{"pgn_source_interval_ns","1000000000"},{"pgn_cumulative_tests","10"},{"pgn_cumulative_processed_gaps","3"},{"pgn_processed_gaps_per_second",3.},{"pgn_gap_window_positions_per_second",8e11}};
    pgn::mining::stampSourceAge(gpu.raw,0,0,pgn::mining::receiptMonotonicNs());
    cpu.gpuUuid="cpu-live";cpu.engineId="pgn-cpu-audit";cpu.runId="cpu-audit";cpu.authoritative=true;cpu.observedUtc=gpu.observedUtc;cpu.rowsCompleted="500";cpu.e2ePositionsPerSecond=1e9;cpu.raw={{"pgn_cpu_control",true},{"pgn_process_running",true}};
    auto totalEstimate=pgn::ui::summarizeLiveCardRates({&gpu,&cpu});
    if(totalEstimate.positionsRate!=1.001e12||totalEstimate.testsRate!=2.6e9||totalEstimate.processedRate!=3.||totalEstimate.yield!=.00375||!totalEstimate.searchRatesOnly||!totalEstimate.displayRateEstimate||totalEstimate.positions!="1500")return 40;
    if(pgn::ui::summarizeMining({&gpu,&cpu}).positionsRate)return 41;
    if(pgn::ui::summarizeLiveCardRates({&cpu,&gpu,&gpu}).positionsRate!=totalEstimate.positionsRate)return 42;
    cpu.e2ePositionsPerSecond.reset();auto missingRates=pgn::ui::summarizeLiveCardRates({&gpu,&cpu});if(missingRates.positionsRate||missingRates.testsRate!=2.6e9||missingRates.processedRate!=3.)return 43;
    cpu.e2ePositionsPerSecond=1e9;cpu.observedUtc=gpu.observedUtc.addSecs(-10);if(pgn::ui::summarizeLiveCardRates({&gpu,&cpu}).positionsRate)return 44;
    cpu.observedUtc=gpu.observedUtc;cpu.raw.insert("pgn_process_running",false);auto idle=pgn::ui::summarizeLiveCardRates({&gpu,&cpu});if(idle.positionsRate!=1e12||idle.searchRatesOnly)return 45;
    cpu.raw.insert("pgn_process_running",true);cpu.e2ePositionsPerSecond=std::numeric_limits<double>::infinity();if(pgn::ui::summarizeLiveCardRates({&gpu,&cpu}).positionsRate)return 46;
    cpu.e2ePositionsPerSecond=1e9;gpu.stale=true;auto stale=pgn::ui::summarizeLiveCardRates({&gpu,&cpu});if(stale.positionsRate||stale.testsRate||stale.processedRate)return 47;
    gpu.stale=false;auto noAudit=pgn::ui::summarizeLiveCardRates({&gpu,&cpu},{"cpu-audit"});if(noAudit.positionsRate!=1e12||noAudit.searchRatesOnly)return 48;
    gpu.e2ePositionsPerSecond=0.;gpu.powmPerSecond=0.;gpu.gapPairsTestedPerSecond=0.;gpu.raw.insert("pgn_processed_gaps_per_second",0.);auto zero=pgn::ui::summarizeLiveCardRates({&gpu});if(zero.positionsRate!=0.||zero.testsRate!=0.||zero.processedRate!=0.)return 49;
    std::cout<<"PASS cross-clock CPU+GPU live estimates, search-only test/gap denominator, independent missing metrics, staleness, deduplication, finite values and unchanged exact summaries\n";
    auto gpuAudit=gpu;gpuAudit.e2ePositionsPerSecond=1e12;gpuAudit.raw.insert("pgn_work_role","audit");
    auto verificationOnly=pgn::ui::summarizeLiveCardRates({&gpuAudit});
    if(verificationOnly.positionsRate!=1e12||verificationOnly.testsRate||verificationOnly.processedRate||verificationOnly.retainedRate||verificationOnly.yield||!verificationOnly.searchRatesOnly)return 55;
    std::cout<<"PASS GPU audit volume remains visible while search-only tests, gaps and yield remain unavailable\n";
    const QJsonObject calculating{{"state","RUNNING"},{"enabled",true},{"compute_started",true},{"active_workers",4},{"inflight_pieces",0},{"result_ready",false}};
    if(!pgn::mining::cpuCalculationActive(calculating,true)||pgn::mining::cpuCalculationActive(calculating,false))return 50;
    for(const auto& state:QStringList{"PAUSED","BUDGET_PAUSED","WAITING_FOR_AUDIT","COMPUTED","FAILED"}){auto inactive=calculating;inactive.insert("state",state);if(pgn::mining::cpuCalculationActive(inactive,true))return 51;}
    for(const auto& field:QStringList{"enabled","compute_started"}){auto inactive=calculating;inactive.insert(field,false);if(pgn::mining::cpuCalculationActive(inactive,true))return 52;}
    auto betweenPieces=calculating;betweenPieces.insert("resident_workers",4);betweenPieces.insert("active_workers",0);if(!pgn::mining::cpuCalculationActive(betweenPieces,true))return 56;
    auto noWorkers=calculating;noWorkers.insert("active_workers",0);if(pgn::mining::cpuCalculationActive(noWorkers,true))return 53;
    auto complete=calculating;complete.insert("result_ready",true);if(pgn::mining::cpuCalculationActive(complete,true))return 54;
    std::cout<<"PASS CPU calculation phase survives empty piece queue; explicit pause, budget pause, wait, disconnect and completed results stay inactive\n";

}

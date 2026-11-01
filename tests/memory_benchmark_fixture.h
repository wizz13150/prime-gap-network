#pragma once
#include "mining/memory_benchmark_search.h"
inline pgn::mining::MemoryBenchmarkCapabilities memoryCaps(bool historicalClosedFixture=false) {
    pgn::mining::MemoryBenchmarkCapabilities caps;
    caps.identity={"fixture-gpu","fixture-driver",QString(64,'a'),"Native Fast68",QString(64,'b'),{"native.exe","--g","800"},68,800,1000,100};
    caps.supportedMemoryMhz={9751,9501,5001,810,405};caps.baselineMemoryMhz=9501;
    caps.probePositions=100;caps.probeWarmupPositions=10;
    if(historicalClosedFixture){caps.minimumRequestedMhz=500;caps.maximumRequestedMhz=9500;}
    caps.requestedMemoryMhz=pgn::mining::regularMemoryRequests(caps.minimumRequestedMhz,caps.maximumRequestedMhz,caps.requestedPointCount);
    caps.exclusiveLease=caps.restoreOnDisconnect=caps.memoryOnly=caps.coreAutomatic=true;return caps;
}
inline pgn::mining::MemoryBenchmarkSample memorySample(const pgn::mining::MemoryBenchmarkCapabilities& caps,
    const pgn::mining::MemoryBenchmarkPoint& point,quint64 ns=0) {
    pgn::mining::MemoryBenchmarkSample s;s.identity=caps.identity;s.measurementId=QString::number(point.sequence);
    s.requestSequence=point.sequence;s.requestedMemoryMhz=point.memoryMhz;s.control=point.control;
    s.phase=point.phase;s.evidence=pgn::mining::MemoryBenchmarkEvidence::NativeCounters;
    s.nativeAccountingVerified=s.activeClockPlateauVerified=true;s.activeClockSamples=5;
    s.sourceRun="fixture-run-"+s.measurementId;s.counterNamespace="fixture-source-clock";s.observedUtc=QDateTime::currentDateTimeUtc();
    if(!ns)ns=point.phase==pgn::mining::MemoryBenchmarkPhase::Probe?1000000000ULL:10000000000ULL;
    s.sourceBeginNs=1000000000ULL;s.sourceEndNs=s.sourceBeginNs+ns;s.positionsBegin=100;s.positionsEnd=100+point.measuredPositions;
    s.warmupPositions=point.warmupPositions;s.expectedAppliedMemoryMhz=point.control?caps.baselineMemoryMhz:point.memoryMhz<810?810:point.memoryMhz<=5001?5001:9501;
    s.clockEffect=s.expectedAppliedMemoryMhz==point.memoryMhz?pgn::mining::MemoryClockEffect::Exact:pgn::mining::MemoryClockEffect::RoundedSupported;
    s.memoryMinMhz=s.memoryMaxMhz=s.expectedAppliedMemoryMhz;s.coreMinMhz=s.coreMaxMhz=1500;
    s.thermalThrottle=false;s.clockLimitReasons="fixture-none";return s;
}

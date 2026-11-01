#include "memory_benchmark_fixture.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>
#include <iostream>
#define REQUIRE(x) do{if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(0)
int main(int argc,char** argv) {
 QCoreApplication app(argc,argv);using namespace pgn::mining;auto caps=memoryCaps(true);MemoryBenchmarkSearch search;
 REQUIRE(regularMemoryRequests(4000,9250,10)==QVector<int>({4000,4583,5167,5750,6333,6917,7500,8083,8667,9250}));
 auto current=memoryCaps();REQUIRE(search.begin(current));
 while(const auto p=search.next()){REQUIRE(p->memoryMhz>=4000);REQUIRE(search.accept(memorySample(current,*p)));}
 REQUIRE(search.receipt().value("probes").toArray()[0].toObject().value("requested_target_achieved")==false);
 REQUIRE(search.begin(caps));int probes=0,confirmations=0;QSet<int> confirmedRequests;
 while(const auto p=search.next()) {
  REQUIRE(p->memoryMhz<=9501);const bool probe=p->phase==MemoryBenchmarkPhase::Probe;
  if(probe){++probes;REQUIRE(!p->control&&p->measuredPositions==100);}else{++confirmations;confirmedRequests.insert(p->candidateMhz);REQUIRE(p->measuredPositions==1000);}
  auto sample=memorySample(caps,*p,!probe&&!p->control&&p->candidateMhz>=1500?8000000000ULL:0);
  REQUIRE(search.accept(sample));
  if(probes<10){const auto r=search.receipt();REQUIRE(r.value("samples").toArray().isEmpty());REQUIRE(r.value("best_observed_native_positions_per_second").isNull());}
 }
 REQUIRE(probes==10&&confirmations==8&&confirmedRequests.size()==2&&search.complete()&&search.bestMemoryMhz()==5001);
 auto receipt=search.receipt();REQUIRE(receipt.value("planned_confirmation_count")==8);REQUIRE(receipt.value("distinct_applied_group_count")==3);
 REQUIRE(receipt.value("samples").toArray().size()==8&&receipt.value("probes").toArray().size()==10);
 REQUIRE(receipt.value("native_graph_eligible")==false&&receipt.value("global_optimum_proven")==false);
 const auto groups=receipt.value("physical_groups").toArray();REQUIRE(groups[1].toObject().value("request_count")==4);
 REQUIRE(groups[2].toObject().value("request_count")==5&&groups[2].toObject().value("baseline_alias")==true);
 for(const auto value:receipt.value("probes").toArray()) {const auto p=value.toObject();REQUIRE(p.value("native_graph_eligible")==false&&!p.contains("native_positions_per_second"));}
 // rc0/readback does not prove an effect: a lower request held at the old
 // absolute clamp is rejected, retained, and never schedules a long finalist.
 REQUIRE(search.begin(caps));auto sample=memorySample(caps,*search.next());sample.memoryMinMhz=sample.memoryMaxMhz=5001;
 REQUIRE(search.accept(sample));REQUIRE(search.receipt().value("probes").toArray()[0].toObject().value("accepted")==false);
 while(const auto p=search.next())REQUIRE(search.accept(memorySample(caps,*p)));
 REQUIRE(search.receipt().value("planned_confirmation_count")==4);
 REQUIRE(search.receipt().value("distinct_applied_group_count")==2);
 // Exact fine control may be absent from the application-clock list, but its
 // active clock must match the target. This tests policy only, not real9251MHz.
 auto fine=caps;fine.minimumRequestedMhz=251;fine.maximumRequestedMhz=9251;fine.requestedMemoryMhz=regularMemoryRequests(251,9251,10);
 REQUIRE(search.begin(fine));for(int i=0;i<10;++i){const auto p=*search.next();auto s=memorySample(fine,p);s.clockEffect=MemoryClockEffect::Exact;s.expectedAppliedMemoryMhz=p.memoryMhz;s.memoryMinMhz=s.memoryMaxMhz=p.memoryMhz;REQUIRE(search.accept(s));}
 REQUIRE(search.receipt().value("probes").toArray().last().toObject().value("accepted")==true);
 REQUIRE(search.receipt().value("planned_confirmation_count")==12);
 // The old ineffective fine-setting shape: expected9251, actual held5001.
 REQUIRE(search.begin(fine));for(int i=0;i<10;++i){const auto p=*search.next();auto s=memorySample(fine,p);s.clockEffect=MemoryClockEffect::Exact;s.expectedAppliedMemoryMhz=p.memoryMhz;s.memoryMinMhz=s.memoryMaxMhz=i==9?5001:p.memoryMhz;REQUIRE(search.accept(s));}
 REQUIRE(search.receipt().value("probes").toArray().last().toObject().value("accepted")==false);
 REQUIRE(search.begin(caps));while(search.next()->phase==MemoryBenchmarkPhase::Probe)REQUIRE(search.accept(memorySample(caps,*search.next())));
 REQUIRE(search.accept(memorySample(caps,*search.next())));auto changed=memorySample(caps,*search.next());
 changed.clockEffect=MemoryClockEffect::Exact;changed.expectedAppliedMemoryMhz=changed.requestedMemoryMhz;changed.memoryMinMhz=changed.memoryMaxMhz=changed.requestedMemoryMhz;
 REQUIRE(!search.accept(changed)&&search.error().contains("plateau changed"));
 REQUIRE(search.begin(caps));sample=memorySample(caps,*search.next());sample.clockEffect=MemoryClockEffect::NotApplied;
 REQUIRE(search.accept(sample)&&search.receipt().value("probes").toArray()[0].toObject().value("accepted")==false);
 REQUIRE(search.begin(caps));sample=memorySample(caps,*search.next());sample.activeClockSamples=1;
 REQUIRE(search.accept(sample)&&search.receipt().value("probes").toArray()[0].toObject().value("accepted")==false);
 REQUIRE(search.begin(caps));sample=memorySample(caps,*search.next());sample.evidence=MemoryBenchmarkEvidence::GpuServiceOnly;
 sample.nativeAccountingVerified=false;REQUIRE(search.accept(sample));
 auto service=search.receipt().value("probes").toArray()[0].toObject();REQUIRE(service.value("accepted")==false&&!service.contains("probe_native_positions_per_second")&&!service.contains("positions_end"));
 REQUIRE(search.receipt().value("samples").toArray().isEmpty());
 REQUIRE(search.begin(caps));sample=memorySample(caps,*search.next());sample.identity.driverVersion="changed";REQUIRE(!search.accept(sample));
 REQUIRE(search.begin(caps));sample=memorySample(caps,*search.next());sample.positionsEnd--;REQUIRE(!search.accept(sample));
 REQUIRE(search.begin(caps));sample=memorySample(caps,*search.next());sample.requestSequence++;REQUIRE(!search.accept(sample));
 REQUIRE(search.begin(caps));sample=memorySample(caps,*search.next());REQUIRE(search.accept(sample));auto replay=memorySample(caps,*search.next());replay.sourceRun=sample.sourceRun;REQUIRE(!search.accept(replay));
 REQUIRE(search.begin(caps));search.cancel("operator cancelled");REQUIRE(!search.complete()&&!search.next());
 auto invalid=caps;invalid.exclusiveLease=false;REQUIRE(!search.begin(invalid));invalid=caps;invalid.coreAutomatic=false;REQUIRE(!search.begin(invalid));
 invalid=caps;invalid.probePositions=invalid.identity.measuredPositions;REQUIRE(!search.begin(invalid));
 invalid=caps;invalid.requestedMemoryMhz.last()=9751;REQUIRE(!search.begin(invalid));
 std::cout<<"PASS closed CPU:10 short Native probes→3 applied groups→8 ABBA confirmations; no40 redundant long runs; unchanged clamps/unknown effect/one clock sample rejected; fine exact mapping, Native/service separation, identities/cancel/replay guarded\n";
}

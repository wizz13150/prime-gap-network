#include "mining/memory_benchmark_search.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QMap>
#include <algorithm>
#include <cmath>

namespace pgn::mining { namespace {
bool digest(const QString& value) { return QRegularExpression("^[0-9a-fA-F]{64}$").match(value).hasMatch(); }
double rate(const MemoryBenchmarkSample& s) { return double(s.positionsEnd-s.positionsBegin)*1e9/double(s.sourceEndNs-s.sourceBeginNs); }
bool stable(double a,double b) { return std::min(a,b)>0 && std::max(a,b)/std::min(a,b)<=1.03; }
double median(QVector<double> values) {std::sort(values.begin(),values.end());const auto n=values.size();return n%2?values[n/2]:(values[n/2-1]+values[n/2])/2.;}
}
QVector<int> regularMemoryRequests(int low,int high,int count) {
    QVector<int> result;if(low<=0||high<=low||count<2||count>12||high-low<count-1)return result;
    for(int i=0;i<count;++i)result.append(low+int((qint64(high-low)*i+(count-1)/2)/(count-1)));return result;
}
QJsonObject MemoryBenchmarkIdentity::json() const {
    QJsonArray cli;for(const auto& value:nativeCli)cli.append(value);
    const auto cliBytes=QJsonDocument(cli).toJson(QJsonDocument::Compact);
    return {{"gpu_uuid",gpuUuid},{"driver_version",driverVersion},{"image_sha256",imageSha256},
        {"profile",profile},{"number_bits",numberBits},{"gap",gap},{"min_gap",gap},{"native_cli",cli},
        {"native_cli_sha256",QString::fromLatin1(QCryptographicHash::hash(cliBytes,QCryptographicHash::Sha256).toHex())},
        {"work_sha256",workSha256},{"measured_positions",QString::number(measuredPositions)},
        {"warmup_positions",QString::number(warmupPositions)}};
}
QJsonObject MemoryBenchmarkSample::json(const MemoryBenchmarkPoint& point) const {
    QJsonObject value{{"sequence",point.sequence},{"candidate_memory_mhz",point.candidateMhz},{"control",point.control},
        {"requested_memory_mhz",point.memoryMhz},{"measurement_id",measurementId},{"source_run",sourceRun},
        {"counter_namespace",counterNamespace},{"observed_utc",observedUtc.toUTC().toString(Qt::ISODateWithMs)},
        {"source_begin_ns",QString::number(sourceBeginNs)},{"source_end_ns",QString::number(sourceEndNs)},
        {"positions_begin",QString::number(positionsBegin)},{"positions_end",QString::number(positionsEnd)},
        {"warmup_positions",QString::number(warmupPositions)},{"memory_min_mhz",memoryMinMhz},
        {"memory_max_mhz",memoryMaxMhz},{"core_min_mhz",coreMinMhz},{"core_max_mhz",coreMaxMhz},
        {"thermal_throttle",thermalThrottle?QJsonValue(*thermalThrottle):QJsonValue()},
        {"clock_limit_reasons",clockLimitReasons.isEmpty()?QJsonValue():QJsonValue(clockLimitReasons)},
        {"phase",point.phase==MemoryBenchmarkPhase::Probe?"PROBE":"CONFIRMATION"},
        {"evidence_kind",evidence==MemoryBenchmarkEvidence::NativeCounters?"NATIVE_COUNTERS":"NOT_NATIVE"},
        {"native_accounting_verified",nativeAccountingVerified},{"active_clock_samples",activeClockSamples},
        {"expected_applied_memory_mhz",expectedAppliedMemoryMhz},{"native_graph_eligible",false}};
    value.insert("clock_effect",clockEffect==MemoryClockEffect::Exact?"EXACT_TARGET_PLATEAU":clockEffect==MemoryClockEffect::RoundedSupported?"DECLARED_CLOCK_ROUNDING":clockEffect==MemoryClockEffect::NotApplied?"NOT_APPLIED":"UNKNOWN");
    value.insert("requested_target_achieved",clockEffect==MemoryClockEffect::Exact&&std::abs(memoryMinMhz-point.memoryMhz)<=15&&std::abs(memoryMaxMhz-point.memoryMhz)<=15);
    value.insert("expected_confirmation_plateau_mhz",point.expectedPlateauMhz>0?QJsonValue(point.expectedPlateauMhz):QJsonValue());
    if(evidence==MemoryBenchmarkEvidence::NativeCounters&&nativeAccountingVerified&&sourceEndNs>sourceBeginNs)
        value.insert(point.phase==MemoryBenchmarkPhase::Probe?"probe_native_positions_per_second":"native_positions_per_second",rate(*this));
    if(evidence!=MemoryBenchmarkEvidence::NativeCounters)for(const auto* key:{"source_begin_ns","source_end_ns","positions_begin","positions_end","warmup_positions"})value.remove(key);
    return value;
}
bool MemoryBenchmarkSearch::begin(const MemoryBenchmarkCapabilities& caps) {
    *this=MemoryBenchmarkSearch{};capabilities_=caps;
    const auto& id=caps.identity;
    if(id.gpuUuid.isEmpty()||id.driverVersion.isEmpty()||!digest(id.imageSha256)||id.profile.isEmpty()||
       !digest(id.workSha256)||id.nativeCli.isEmpty()||id.numberBits<=0||id.gap!=800||
       !id.measuredPositions||!id.warmupPositions) {
        error_="Missing exact Native/g800 workload, precision, driver, image or CLI identity";return false;
    }
    if(!caps.exclusiveLease||!caps.restoreOnDisconnect||!caps.memoryOnly||!caps.coreAutomatic) {
        error_="Exclusive memory-only lease with autonomous restore and automatic core is required";return false;
    }
    if(!caps.probePositions||caps.probePositions>=id.measuredPositions||!caps.probeWarmupPositions||
       caps.probeWarmupPositions>id.warmupPositions||caps.maximumFinalistGroups<1||caps.maximumFinalistGroups>3) {
        error_="Bounded short-probe work and one to three finalist groups are required";return false;
    }
    if(caps.baselineMemoryMhz<=0||caps.supportedMemoryMhz.isEmpty()||caps.supportedMemoryMhz.size()>32||
       !caps.supportedMemoryMhz.contains(caps.baselineMemoryMhz)) {
        error_="Observed baseline and supported memory clocks are unavailable or inconsistent";return false;
    }
    const auto grid=regularMemoryRequests(caps.minimumRequestedMhz,caps.maximumRequestedMhz,caps.requestedPointCount);
    if(grid.isEmpty()||grid!=caps.requestedMemoryMhz||caps.maximumRequestedMhz>caps.baselineMemoryMhz) {
        error_="Requested memory grid does not match its bounded min/max/count";return false;
    }
    QVector<int> candidates;
    for(int mhz:caps.supportedMemoryMhz) {
        if(mhz<=0) {error_="Invalid supported memory clock";return false;}
        if(caps.requestedMemoryMhz.isEmpty()&&mhz<caps.baselineMemoryMhz&&!candidates.contains(mhz))candidates.append(mhz);
    }
    if(!caps.requestedMemoryMhz.isEmpty()) {
        for(int mhz:caps.requestedMemoryMhz) {
            if(mhz<=0||mhz>caps.baselineMemoryMhz||candidates.contains(mhz)) {error_="Memory request is duplicate, invalid or above the observed automatic baseline";return false;}
            candidates.append(mhz);
        }
    } else std::sort(candidates.begin(),candidates.end(),std::greater<int>());
    if(candidates.size()>12) {error_="More than twelve memory levels: explicit bounded selection required";return false;}
    if(candidates.isEmpty()) {error_="No supported memory level below the observed automatic baseline";return false;}
    // Ten SHORT Native probes first. Long work is not scheduled until the
    // observed physical plateaus have been grouped, excluding rejected writes.
    for(int mhz:candidates)points_.append({int(points_.size()),mhz,mhz,false,
        MemoryBenchmarkPhase::Probe,caps.probePositions,caps.probeWarmupPositions});
    started_=true;bestRequestedMemoryMhz_=bestMemoryMhz_=caps.baselineMemoryMhz;return true;
}
std::optional<MemoryBenchmarkPoint> MemoryBenchmarkSearch::next() const {
    if(!started_||!error_.isEmpty()||cursor_>=points_.size())return {};
    return points_[cursor_];
}
void MemoryBenchmarkSearch::cancel(const QString& reason) {error_=reason.isEmpty()?QStringLiteral("Cancelled"):reason;}
bool MemoryBenchmarkSearch::accept(const MemoryBenchmarkSample& sample) {
    const auto point=next();if(!point)return false;
    const auto fail=[this](const QString& reason){error_=reason;return false;};
    if(sample.requestSequence!=point->sequence||sample.requestedMemoryMhz!=point->memoryMhz||sample.control!=point->control||sample.phase!=point->phase)
        return fail("Source measurement does not bind the exact requested point/policy");
    if(sample.identity!=capabilities_.identity)return fail("Native workload, GPU, driver, image, CLI or profile changed");
    if(point->phase==MemoryBenchmarkPhase::Probe&&sample.evidence==MemoryBenchmarkEvidence::GpuServiceOnly&&!sample.nativeAccountingVerified) {
        if(sample.measurementId.isEmpty()||!sample.observedUtc.isValid())return fail("Missing diagnostic probe provenance");
        for(const auto& previous:samples_)if(previous.measurementId==sample.measurementId)return fail("Replayed diagnostic probe");
        auto probe=sample.json(*point);probe.insert("accepted",false);probe.insert("rejection","GPU service diagnostic only; no Native work measured or graph point admitted");
        samples_.append(sample);probes_.append(probe);++cursor_;if(cursor_==points_.size())planConfirmations();return true;
    }
    if(sample.measurementId.isEmpty()||sample.sourceRun.isEmpty()||sample.counterNamespace.isEmpty()||!sample.observedUtc.isValid())
        return fail("Missing actual source measurement provenance");
    for(const auto& previous:samples_)if(previous.measurementId==sample.measurementId)return fail("Replayed source measurement");
    for(const auto& previous:samples_)if(previous.sourceRun==sample.sourceRun&&previous.counterNamespace==sample.counterNamespace&&
        sample.sourceBeginNs<previous.sourceEndNs&&sample.sourceEndNs>previous.sourceBeginNs)return fail("Overlapping or replayed source counter interval");
    if(sample.evidence!=MemoryBenchmarkEvidence::NativeCounters||!sample.nativeAccountingVerified)
        return fail("GPU service observations are not verified Native source work");
    if(sample.sourceEndNs<=sample.sourceBeginNs||sample.sourceEndNs-sample.sourceBeginNs<1000000000ULL||
       sample.positionsEnd<sample.positionsBegin||sample.positionsEnd-sample.positionsBegin!=point->measuredPositions||
       sample.warmupPositions!=point->warmupPositions)
        return fail("Incomplete fixed work, source interval or warmup accounting");
    if(sample.coreMinMhz<=0||sample.coreMaxMhz<sample.coreMinMhz)
        return fail("Actual core frequency observations are missing or inconsistent");
    QString clockError;
    if(!sample.activeClockPlateauVerified||sample.activeClockSamples<3||sample.memoryMinMhz<=0||sample.memoryMaxMhz<sample.memoryMinMhz||sample.memoryMaxMhz-sample.memoryMinMhz>30||
       sample.memoryMaxMhz>capabilities_.baselineMemoryMhz+15||
       std::abs(sample.memoryMinMhz-sample.expectedAppliedMemoryMhz)>15||std::abs(sample.memoryMaxMhz-sample.expectedAppliedMemoryMhz)>15)
        clockError="Actual active-work clock plateau does not match the setting readback";
    int rounded=0;for(int mhz:capabilities_.supportedMemoryMhz)if(mhz>=point->memoryMhz&&mhz<=capabilities_.baselineMemoryMhz&&(!rounded||mhz<rounded))rounded=mhz;
    const bool exact=sample.clockEffect==MemoryClockEffect::Exact&&sample.expectedAppliedMemoryMhz==point->memoryMhz;
    const bool declaredRounding=!point->control&&sample.clockEffect==MemoryClockEffect::RoundedSupported&&sample.expectedAppliedMemoryMhz==rounded;
    if(!exact&&!declaredRounding)clockError="Memory request not applied or its rounding is unverified; API success is insufficient";
    if(!point->control&&(sample.memoryMinMhz<capabilities_.minimumRequestedMhz-15||sample.memoryMaxMhz>capabilities_.maximumRequestedMhz+15))
        clockError="Applied clock is outside the requested physical range; diagnostic only";
    if(point->expectedPlateauMhz>0&&std::abs(sample.expectedAppliedMemoryMhz-point->expectedPlateauMhz)>15)
        clockError="Physical memory plateau changed between probe and confirmation";
    if(!clockError.isEmpty()&&point->phase==MemoryBenchmarkPhase::Confirmation)return fail(clockError);
    samples_.append(sample);++cursor_;
    if(point->phase==MemoryBenchmarkPhase::Probe) {
        auto probe=sample.json(*point);probe.insert("accepted",clockError.isEmpty());probe.insert("rejection",clockError);probes_.append(probe);
        if(cursor_==points_.size())planConfirmations();return true;
    }
    if((cursor_-confirmationStart_)%4==0) {
        const auto offset=cursor_-4;
        const double a0=rate(samples_[offset]),b0=rate(samples_[offset+1]),b1=rate(samples_[offset+2]),a1=rate(samples_[offset+3]);
        bool thermal=false,thermalKnown=true;for(qsizetype i=offset;i<cursor_;++i){thermalKnown&=samples_[i].thermalThrottle.has_value();thermal|=samples_[i].thermalThrottle.value_or(false);}
        const auto& firstCandidate=samples_[offset+1];const auto& secondCandidate=samples_[offset+2];
        const int actual=(firstCandidate.memoryMinMhz+firstCandidate.memoryMaxMhz)/2;
        const bool sameLevel=std::abs(secondCandidate.memoryMinMhz-actual)<=15&&std::abs(secondCandidate.memoryMaxMhz-actual)<=15;
        bool alias=std::abs(actual-capabilities_.baselineMemoryMhz)<=15;
        for(const auto& previous:comparisons_)if(std::abs(previous.value("actual_memory_mhz").toInt()-actual)<=15)alias=true;
        const bool qualified=thermalKnown&&!thermal&&sameLevel&&stable(a0,a1)&&stable(b0,b1);
        const bool improvement=qualified&&std::min(b0,b1)>std::max(a0,a1)*1.01;
        const double candidate=(b0+b1)/2.;
        int coreMin=samples_[offset].coreMinMhz,coreMax=samples_[offset].coreMaxMhz;
        for(qsizetype i=offset+1;i<cursor_;++i){coreMin=std::min(coreMin,samples_[i].coreMinMhz);coreMax=std::max(coreMax,samples_[i].coreMaxMhz);}
        comparisons_.append({{"requested_memory_mhz",point->candidateMhz},{"actual_memory_mhz",actual},{"rounded_alias",alias},{"stable",qualified},{"improvement_over_controls",improvement},
            {"candidate_native_positions_per_second",candidate},{"control_before",a0},{"control_after",a1},
            {"candidate_first",b0},{"candidate_second",b1},{"thermal_throttle",thermalKnown?QJsonValue(thermal):QJsonValue()},
            {"core_min_mhz",coreMin},{"core_max_mhz",coreMax},{"core_clock_variation_percent",100.*double(coreMax-coreMin)/double(coreMin)},
            {"memory_only_causality_proven",false},
            {"qualification",qualified?"Core AUTO observed; repeated Native throughput and controls within 3%; aliases aggregated into one physical group":"Unstable controls/repeats/readback, thermal throttle or unavailable thermal evidence; not accepted"}});
        rebuildGroups();
    }
    return true;
}
void MemoryBenchmarkSearch::planConfirmations() {
    confirmationStart_=cursor_;planningComplete_=true;
    QMap<int,QVector<QJsonObject>> grouped;
    for(const auto& probe:probes_)if(probe.value("accepted").toBool()) {
        const int actual=probe.value("expected_applied_memory_mhz").toInt();int key=actual;
        for(auto it=grouped.cbegin();it!=grouped.cend();++it)if(std::abs(it.key()-actual)<=15){key=it.key();break;}
        grouped[key].append(probe);
    }
    struct Finalist {int group,request,actual;double rate;};QVector<Finalist> finalists;
    for(auto it=grouped.cbegin();it!=grouped.cend();++it) {
        QJsonArray requests;QVector<double> rates;bool eligible=true;
        for(const auto& probe:it.value()) {requests.append(probe.value("requested_memory_mhz"));rates.append(probe.value("probe_native_positions_per_second").toDouble());
            eligible&=probe.value("thermal_throttle").isBool()&&!probe.value("thermal_throttle").toBool();}
        const bool baseline=std::abs(it.key()-capabilities_.baselineMemoryMhz)<=15;
        const int index=probeGroups_.size();const double probeRate=median(rates);
        probeGroups_.append({{"actual_memory_mhz",it.key()},{"requested_memory_mhz",requests},{"request_count",requests.size()},
            {"baseline_alias",baseline},{"stable",false},{"confirmation_validated",false},{"finalist",false},
            {"probe_native_positions_per_second_median",probeRate},{"native_positions_per_second_median",QJsonValue()},
            {"qualification_state","PROBE_ONLY_NOT_NATIVE_GRAPH"},{"native_graph_eligible",false}});
        if(eligible&&!baseline)finalists.append({index,requests.first().toInt(),it.key(),probeRate});
    }
    std::sort(finalists.begin(),finalists.end(),[](const auto& a,const auto& b){return a.rate>b.rate;});
    for(int index=0;index<std::min(int(finalists.size()),capabilities_.maximumFinalistGroups);++index) {
        const auto f=finalists[index];probeGroups_[f.group].insert("finalist",true);probeGroups_[f.group].insert("qualification_state","AWAITING_CONFIRMATION");
        for(int slot=0;slot<4;++slot) {const bool control=slot==0||slot==3;
            points_.append({int(points_.size()),f.request,control?capabilities_.baselineMemoryMhz:f.request,control,
                MemoryBenchmarkPhase::Confirmation,capabilities_.identity.measuredPositions,capabilities_.identity.warmupPositions,
                control?capabilities_.baselineMemoryMhz:f.actual});}
    }
}
void MemoryBenchmarkSearch::rebuildGroups() {
    groups_.clear();bestRate_=0;bestRequestedMemoryMhz_=bestMemoryMhz_=capabilities_.baselineMemoryMhz;
    QMap<int,QVector<QJsonObject>> grouped;QVector<double> allControls;bool controlsValid=true;
    for(const auto& comparison:comparisons_) {
        const int actual=comparison.value("actual_memory_mhz").toInt();int key=actual;
        for(auto it=grouped.cbegin();it!=grouped.cend();++it)if(std::abs(it.key()-actual)<=15){key=it.key();break;}
        grouped[key].append(comparison);allControls.append(comparison.value("control_before").toDouble());allControls.append(comparison.value("control_after").toDouble());
        controlsValid&=comparison.value("thermal_throttle").isBool()&&!comparison.value("thermal_throttle").toBool();
    }
    if(!allControls.isEmpty()&&controlsValid&&stable(*std::min_element(allControls.begin(),allControls.end()),*std::max_element(allControls.begin(),allControls.end())))bestRate_=median(allControls);
    for(auto it=grouped.cbegin();it!=grouped.cend();++it) {
        QVector<double> candidates,controls;QJsonArray requests;bool allStable=true;int coreMin=0,coreMax=0;
        for(const auto& comparison:it.value()) {
            requests.append(comparison.value("requested_memory_mhz"));candidates.append(comparison.value("candidate_first").toDouble());candidates.append(comparison.value("candidate_second").toDouble());
            controls.append(comparison.value("control_before").toDouble());controls.append(comparison.value("control_after").toDouble());allStable&=comparison.value("stable").toBool();
            const int low=comparison.value("core_min_mhz").toInt(),high=comparison.value("core_max_mhz").toInt();coreMin=coreMin?std::min(coreMin,low):low;coreMax=std::max(coreMax,high);
        }
        const double low=*std::min_element(candidates.begin(),candidates.end()),high=*std::max_element(candidates.begin(),candidates.end());
        const double controlLow=*std::min_element(controls.begin(),controls.end()),controlHigh=*std::max_element(controls.begin(),controls.end());
        const bool qualified=allStable&&stable(low,high)&&stable(controlLow,controlHigh);
        const bool baselineAlias=std::abs(it.key()-capabilities_.baselineMemoryMhz)<=15;
        const bool improved=qualified&&!baselineAlias&&low>1.01*controlHigh;const double candidate=median(candidates);
        if(improved&&candidate>bestRate_) {bestRate_=candidate;bestMemoryMhz_=it.key();bestRequestedMemoryMhz_=requests.first().toInt();}
        groups_.append({{"actual_memory_mhz",it.key()},{"requested_memory_mhz",requests},{"request_count",requests.size()},
            {"candidate_sample_count",candidates.size()},{"stable",qualified},{"baseline_alias",baselineAlias},{"improvement_over_controls",improved},
            {"native_positions_per_second_median",candidate},{"candidate_min",low},{"candidate_max",high},{"control_min",controlLow},{"control_max",controlHigh},
            {"core_min_mhz",coreMin},{"core_max_mhz",coreMax},{"memory_only_causality_proven",false}});
    }
}
QJsonObject MemoryBenchmarkSearch::receipt() const {
    QJsonArray samples,probes,comparisons,groups,supported,requested;
    for(qsizetype i=0;i<samples_.size();++i)if(points_[i].phase==MemoryBenchmarkPhase::Confirmation)samples.append(samples_[i].json(points_[i]));
    for(const auto& probe:probes_)probes.append(probe);
    for(const auto& value:comparisons_)comparisons.append(value);for(int mhz:capabilities_.supportedMemoryMhz)supported.append(mhz);
    for(int mhz:capabilities_.requestedMemoryMhz)requested.append(mhz);
    for(auto group:probeGroups_) {
        for(const auto& confirmed:groups_)if(std::abs(group.value("actual_memory_mhz").toInt()-confirmed.value("actual_memory_mhz").toInt())<=15) {
            for(auto it=confirmed.begin();it!=confirmed.end();++it)if(it.key()!="requested_memory_mhz"&&it.key()!="request_count")group.insert(it.key(),it.value());
            group.insert("confirmation_validated",confirmed.value("stable"));group.insert("qualification_state",confirmed.value("stable").toBool()?"NATIVE_CONFIRMATION_VALIDATED":"CONFIRMATION_REJECTED");
        }
        groups.append(group);
    }
    int achieved=0;for(const auto& probe:probes_)achieved+=probe.value("accepted").toBool()&&probe.value("requested_target_achieved").toBool();
    return {{"schema","pgn-native-memory-search-v2"},{"identity",capabilities_.identity.json()},
        {"requested_minimum_mhz",capabilities_.minimumRequestedMhz},{"requested_maximum_mhz",capabilities_.maximumRequestedMhz},{"requested_point_count",capabilities_.requestedPointCount},
        {"requested_targets_achieved",achieved},{"requested_grid_realized",achieved==capabilities_.requestedPointCount},
        {"supported_memory_mhz",supported},{"requested_memory_mhz",requested},{"baseline_memory_mhz",capabilities_.baselineMemoryMhz},{"core_policy","AUTO"},
        {"best_observed_memory_mhz",bestRate_>0?QJsonValue(bestMemoryMhz_):QJsonValue()},{"best_observed_request_memory_mhz",bestRate_>0?QJsonValue(bestRequestedMemoryMhz_):QJsonValue()},
        {"best_request_is_group_representative_only",true},{"physical_groups",groups},{"distinct_applied_group_count",groups.size()},
        {"probes",probes},{"completed_probe_count",probes.size()},{"planned_confirmation_count",planningComplete_?int(points_.size()-confirmationStart_):0},
        {"completed_confirmation_count",samples.size()},{"phase",!planningComplete_?"PROBES":complete()?"COMPLETE":"CONFIRMATIONS"},{"native_graph_eligible",false},
        {"best_observed_native_positions_per_second",bestRate_>0?QJsonValue(bestRate_):QJsonValue()},
        {"complete",complete()},{"error",error_},{"samples",samples},{"comparisons",comparisons},
        {"protocol","Short Native probes, physical plateau groups, then fixed-work A-B-B-A for distinct finalist groups only; core AUTO measured"},
        {"objective","Stable total Native throughput, including automatic core response"},{"memory_only_causality_proven",false},
        {"scope","LOCAL_OBSERVED_LEVELS_ONLY"},{"global_optimum_proven",false},{"setting_applied_after_release",false},
        {"network_published",false}};
}
}

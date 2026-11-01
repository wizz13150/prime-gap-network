#pragma once
#include "telemetry.h"
#include "booking/decimal_range.h"
#include "mining/source_age.h"
#include <QSet>
#include <QStringList>
#include <QVector>
#include <optional>
#include <stdexcept>
#include <QHash>
#include <array>
#include <deque>
#include <cmath>

namespace pgn::ui {
struct MiningSummary {
    QString positions{"0"},tests{"0"},processed{"0"},retained{"0"};int devices{},running{};
    std::optional<double> positionsRate,testsRate,processedRate,retainedRate,yield;
    std::optional<qint64> sourceAgeLowerBoundNs;
    QDateTime observedUtc;QString intervalNs;bool windowsAligned{},countsIncomplete{};
    // Presentation-clock metadata only; does not alter any summed raw value.
    QString sourceTimeNs, displayClockIdentity;
    QString rateCaption{"RAW INTERVAL"};
    bool displayRateEstimate{},searchRatesOnly{};
};

inline bool hasSourceRateWindow(const FrontierTelemetrySnapshot& lane){
    bool spanOk=false;const auto span=lane.raw.value("pgn_source_window_ns").toString().toLongLong(&spanOk);
    return lane.e2eWindowPositionsPerSecond && spanOk && span>=5'000'000'000LL
        && lane.raw.value("pgn_window_tests_per_second").isDouble()
        && lane.raw.value("pgn_window_retained_per_second").isDouble();
}
inline std::optional<double> sourceDisplayTests(const FrontierTelemetrySnapshot& lane){
    return hasSourceRateWindow(lane)?std::optional<double>(lane.raw.value("pgn_window_tests_per_second").toDouble()):lane.powmPerSecond;
}
inline std::optional<double> sourceWindowProcessed(const FrontierTelemetrySnapshot& lane){
    const auto value=lane.raw.value("pgn_window_processed_per_second");
    return hasSourceRateWindow(lane)&&value.isDouble()?std::optional<double>(value.toDouble()):std::nullopt;
}
inline std::optional<double> sourceWindowYield(const FrontierTelemetrySnapshot& lane){
    if(!hasSourceRateWindow(lane)||*lane.e2eWindowPositionsPerSecond<=0)return {};
    return lane.raw.value("pgn_window_retained_per_second").toDouble()*1e9/ *lane.e2eWindowPositionsPerSecond;
}
inline QString sourceRateCaption(const FrontierTelemetrySnapshot& lane){
    return hasSourceRateWindow(lane)?QString("MEAN %1s").arg(lane.raw.value("pgn_source_window_ns").toString().toDouble()/1e9,0,'f',1)
        :QStringLiteral("RAW · WINDOW WARMING");
}
// Counts add exactly, with UUID deduplication. Rates only add when their exact
// producer-clock interval is identical. No rolling window or invented zero.
inline MiningSummary summarizeMining(const QVector<const FrontierTelemetrySnapshot*>& lanes,
                                      const QSet<QString>& finished={},bool displayWindow=false){
    MiningSummary result;QSet<QString> seen;QString interval,clock,clockNamespace;bool rateSet=false,ageMissing=false,windowBasis=false;
    QStringList displayMembers;std::optional<double> gapWindowPositions;QString gapWindowSpan;bool gapWindowComplete=true;
    // Scoped Bench/control tests are visible device observations, not production
    // mining lanes. Their absent counters/clock must not erase production totals.
    for(const auto* lane:lanes){if(!lane||lane->raw.value("pgn_test_only").toBool()||
        lane->gpuUuid.isEmpty()||seen.contains(lane->gpuUuid)||finished.contains(lane->runId))continue;
        seen.insert(lane->gpuUuid);++result.devices;if(lane->raw.value("pgn_process_running").toBool())++result.running;
        displayMembers.append(QString::number(lane->gpuUuid.size())+":"+lane->gpuUuid+
                              QString::number(lane->runId.size())+":"+lane->runId);
        const auto age=pgn::mining::sourceAgeLowerBoundNs(lane->raw);
        if(age)result.sourceAgeLowerBoundNs=std::max(result.sourceAgeLowerBoundNs.value_or(0),*age);else ageMissing=true;
        const auto add=[&](QString& total,const QString& value){if(value.isEmpty()){result.countsIncomplete=true;return;}try{const auto n=pgn::booking::canonicalUnsigned(value);total=pgn::booking::addUnsigned(total,n);}catch(const std::invalid_argument&){result.countsIncomplete=true;}};
        add(result.positions,lane->rowsCompleted);add(result.tests,lane->raw.value("pgn_cumulative_tests").toString());
        add(result.processed,lane->raw.value("pgn_cumulative_processed_gaps").toString());add(result.retained,lane->cumulativeGapCandidates);
        const bool window=displayWindow&&hasSourceRateWindow(*lane);
        const auto positionRate=window?lane->e2eWindowPositionsPerSecond:lane->e2ePositionsPerSecond;
        const auto testRate=window?sourceDisplayTests(*lane):lane->powmPerSecond;
        const auto retainedRate=window?std::optional<double>(lane->raw.value("pgn_window_retained_per_second").toDouble()):lane->gapPairsTestedPerSecond;
        const auto meanPositions=lane->raw.value("pgn_gap_window_positions_per_second");const auto meanSpan=lane->raw.value("pgn_gap_window_ns").toString();
        if(!meanPositions.isDouble()||meanSpan.isEmpty())gapWindowComplete=false;else if(!gapWindowPositions){gapWindowPositions=meanPositions.toDouble();gapWindowSpan=meanSpan;}else if(meanSpan!=gapWindowSpan)gapWindowComplete=false;else *gapWindowPositions+=meanPositions.toDouble();
        const auto processed=lane->raw.value(window?"pgn_window_processed_per_second":"pgn_processed_gaps_per_second");
        const auto span=lane->raw.value(window?"pgn_source_window_ns":"pgn_source_interval_ns").toString(),end=lane->raw.value("pgn_source_time_ns").toString(),
            name=lane->raw.value("pgn_clock_namespace").toString();
        if(!rateSet){interval=span;clock=end;clockNamespace=name;windowBasis=window;result.positionsRate=positionRate;result.testsRate=testRate;
            result.rateCaption=displayWindow?sourceRateCaption(*lane):QStringLiteral("RAW INTERVAL");
            result.retainedRate=retainedRate;
            if(processed.isDouble())result.processedRate=processed.toDouble();result.observedUtc=lane->observedUtc;rateSet=true;result.windowsAligned=true;}
        else if(window!=windowBasis||name.isEmpty()||name!=clockNamespace||span.isEmpty()||end.isEmpty()||span!=interval||end!=clock)result.windowsAligned=false;
        else{const auto sum=[](auto& total,const auto& value){if(total&&value)*total+=*value;else total.reset();};
            sum(result.positionsRate,positionRate);sum(result.testsRate,testRate);sum(result.retainedRate,retainedRate);
            sum(result.processedRate,processed.isDouble()?std::optional<double>(processed.toDouble()):std::nullopt);}
    }
    if(!result.windowsAligned){result.positionsRate.reset();result.testsRate.reset();result.processedRate.reset();result.retainedRate.reset();result.rateCaption="UNALIGNED WINDOWS";}
    if(gapWindowComplete&&gapWindowPositions&&*gapWindowPositions>0&&result.retainedRate)result.yield=*result.retainedRate*1e9 / *gapWindowPositions;
    else if(result.positionsRate&&*result.positionsRate>0&&result.retainedRate)result.yield=*result.retainedRate*1e9 / *result.positionsRate;
    if(ageMissing)result.sourceAgeLowerBoundNs.reset();result.intervalNs=interval;
    displayMembers.sort();
    result.sourceTimeNs=clock;
    if(!clockNamespace.isEmpty())result.displayClockIdentity=clockNamespace+QChar(u'\0')+displayMembers.join(QChar(u'\0'));
    return result;
}
inline MiningSummary summarizeMiningForDisplay(const QVector<const FrontierTelemetrySnapshot*>& lanes,
                                                const QSet<QString>& finished={}){
    return summarizeMining(lanes,finished,true);
}
// A device rate is already a derivative in seconds. Adding fresh device rates
// does not require their clock origins to match, but it is an estimate over
// their individual windows, not an exact shared-interval measurement.
// CPU auditors currently report positions only. Search tests/gap yield retain
// their search-only denominator rather than treating absent audit data as zero.
inline MiningSummary summarizeLiveCardRates(const QVector<const FrontierTelemetrySnapshot*>& lanes,
    const QSet<QString>& finished={},QDateTime now=QDateTime::currentDateTimeUtc(),
    quint64 receiptNs=pgn::mining::receiptMonotonicNs()){
    auto result=summarizeMining(lanes,finished);QSet<QString> seen;QStringList members;
    double positions=0,tests=0,processed=0,retained=0,gapPositions=0;
    bool positionsComplete=true,testsComplete=true,processedComplete=true,retainedComplete=true,gapPositionsComplete=true;
    int active=0,searches=0;bool auditors=false;
    const auto valid=[](const std::optional<double>& value){return value&&std::isfinite(*value)&&*value>=0;};
    for(const auto* lane:lanes){
        if(!lane||lane->raw.value("pgn_test_only").toBool()||lane->gpuUuid.isEmpty()||seen.contains(lane->gpuUuid)||
           finished.contains(lane->runId)||!lane->raw.value("pgn_process_running").toBool())continue;
        seen.insert(lane->gpuUuid);++active;
        members.append(QString::number(lane->gpuUuid.size())+":"+lane->gpuUuid+QString::number(lane->runId.size())+":"+lane->runId);
        const bool audit=lane->engineId=="pgn-cpu-audit"||lane->raw.value("pgn_work_role")=="audit";auditors|=audit;
        const auto age=pgn::mining::sourceAgeLowerBoundNs(lane->raw);
        const auto receiptAge=lane->observedUtc.isValid()?lane->observedUtc.msecsTo(now):qint64(-1);
        const bool fresh=lane->authoritative&&!lane->stale&&!lane->terminalSealed&&!lane->runId.isEmpty()&&
            (age?*age<=7'000'000'000LL:receiptAge>=0&&receiptAge<=5000);
        if(fresh&&valid(lane->e2ePositionsPerSecond))positions+=*lane->e2ePositionsPerSecond;else positionsComplete=false;
        if(audit)continue;
        ++searches;
        const auto test=sourceDisplayTests(*lane),output=lane->gapPairsTestedPerSecond;
        const auto gaps=lane->raw.value("pgn_processed_gaps_per_second"),meanPositions=lane->raw.value("pgn_gap_window_positions_per_second");
        const auto denominator=meanPositions.isDouble()?std::optional<double>(meanPositions.toDouble()):lane->e2ePositionsPerSecond;
        if(fresh&&valid(test))tests+=*test;else testsComplete=false;
        if(fresh&&gaps.isDouble()&&std::isfinite(gaps.toDouble())&&gaps.toDouble()>=0)processed+=gaps.toDouble();else processedComplete=false;
        if(fresh&&valid(output))retained+=*output;else retainedComplete=false;
        if(fresh&&valid(denominator))gapPositions+=*denominator;else gapPositionsComplete=false;
    }
    result.positionsRate=active&&positionsComplete?std::optional<double>(positions):std::nullopt;
    result.testsRate=searches&&testsComplete?std::optional<double>(tests):std::nullopt;
    result.processedRate=searches&&processedComplete?std::optional<double>(processed):std::nullopt;
    result.retainedRate=searches&&retainedComplete?std::optional<double>(retained):std::nullopt;
    result.yield=searches&&retainedComplete&&gapPositionsComplete&&gapPositions>0?std::optional<double>(retained*1e9/gapPositions):std::nullopt;
    result.displayRateEstimate=true;result.searchRatesOnly=auditors;result.rateCaption=auditors?"LIVE EST · *SEARCH":"LIVE RATE ESTIMATE";
    members.sort();result.displayClockIdentity="ui-live-rate-estimate:"+members.join(QChar(u'\0'));
    result.sourceTimeNs=QString::number(receiptNs);result.intervalNs.clear();return result;
}
// TOTAL presentation only. Original device telemetry is never modified.
class TotalCardRateWindow {
    struct Sample {quint64 ns;std::array<quint64,4> counts;};
    struct Series {QString epoch;std::deque<Sample> samples;};
    QHash<QString,Series> series_;
public:
    MiningSummary observe(const QVector<const FrontierTelemetrySnapshot*>& lanes,
                          const QSet<QString>& finished={}) {
        QVector<FrontierTelemetrySnapshot> copies;copies.reserve(lanes.size());QSet<QString> live;
        bool ready=true;
        for(const auto* lane:lanes){
            if(!lane||lane->raw.value("pgn_test_only").toBool()||lane->gpuUuid.isEmpty()||
               finished.contains(lane->runId)||live.contains(lane->gpuUuid))continue;
            live.insert(lane->gpuUuid);copies.append(*lane);auto& copy=copies.last();
            auto& state=series_[lane->gpuUuid];const auto age=pgn::mining::sourceAgeLowerBoundNs(lane->raw);
            const auto epoch=lane->raw.value("pgn_clock_namespace").toString()+":"+lane->runId;
            bool t=false,a=false,b=false,c=false,d=false;
            const auto ns=lane->raw.value("pgn_source_time_ns").toString().toULongLong(&t);
            const std::array<quint64,4> counts{lane->rowsCompleted.toULongLong(&a),
                lane->raw.value("pgn_cumulative_tests").toString().toULongLong(&b),
                lane->raw.value("pgn_cumulative_processed_gaps").toString().toULongLong(&c),
                lane->cumulativeGapCandidates.toULongLong(&d)};
            if(!t||!a||!b||!c||!d||!age||*age>5'000'000'000LL||
               !lane->raw.value("pgn_process_running").toBool()||lane->runId.isEmpty()){
                state={};ready=false;continue;
            }
            bool backward=false;if(!state.samples.empty())for(int i=0;i<4;++i)
                backward|=counts[i]<state.samples.back().counts[i];
            if(state.epoch!=epoch||(!state.samples.empty()&&ns<state.samples.back().ns)||backward){state={};state.epoch=epoch;}
            if(state.samples.empty()||ns>state.samples.back().ns)state.samples.push_back({ns,counts});
            else if(counts!=state.samples.back().counts){state={};ready=false;continue;}
            const auto cutoff=ns>60'000'000'000ULL?ns-60'000'000'000ULL:0;
            while(state.samples.size()>2&&state.samples[1].ns<=cutoff)state.samples.pop_front();
            while(state.samples.size()>1024)state.samples.pop_front();
            const auto span=ns-state.samples.front().ns;
            if(span<5'000'000'000ULL){ready=false;continue;}
            const double seconds=double(span)/1e9;const auto& first=state.samples.front().counts;
            copy.e2eWindowPositionsPerSecond=double(counts[0]-first[0])/seconds;
            copy.raw.insert("pgn_source_window_ns",QString::number(span));
            copy.raw.insert("pgn_window_tests_per_second",double(counts[1]-first[1])/seconds);
            copy.raw.insert("pgn_window_processed_per_second",double(counts[2]-first[2])/seconds);
            copy.raw.insert("pgn_window_retained_per_second",double(counts[3]-first[3])/seconds);
        }
        for(auto it=series_.begin();it!=series_.end();)if(!live.contains(it.key()))it=series_.erase(it);else ++it;
        QVector<const FrontierTelemetrySnapshot*> view;for(const auto& copy:copies)view.append(&copy);
        auto total=summarizeMiningForDisplay(view);
        if(!ready){total.positionsRate.reset();total.testsRate.reset();total.processedRate.reset();total.retainedRate.reset();total.yield.reset();total.rateCaption="MEAN 60s · WARMING / PAUSED";}
        return total;
    }
};

}

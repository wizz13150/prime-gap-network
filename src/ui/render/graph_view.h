#pragma once

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>
#include <utility>
#include <optional>
#include <vector>

struct FrontierGraphView {
    double minTimeMs{0.0};
    double maxTimeMs{1.0};
    double minMerit{0.0};
    double maxMerit{1.0};
};

// Keep the X-axis meaning explicit.  In particular, a label such as "1m"
// is not used in the UI because it is ambiguous between one minute and one
// month.  The graph itself can still use a compact trading-style set of
// presets, but each preset carries an unambiguous unit.
enum class FrontierGraphXAxis { Time, Length };

struct FrontierTimeRangePreset {
    const char* label;
    const char* tooltip;
    long long spanMs;
    bool allHistory;
};

inline constexpr FrontierTimeRangePreset FrontierTimeRangePresets[] = {
    {"15 MIN", "Fifteen minutes", 15LL * 60LL * 1000LL, false},
    {"1 H", "One hour", 60LL * 60LL * 1000LL, false},
    {"6 H", "Six hours", 6LL * 60LL * 60LL * 1000LL, false},
    {"1 D", "One day", 24LL * 60LL * 60LL * 1000LL, false},
    {"1 W", "Seven days", 7LL * 24LL * 60LL * 60LL * 1000LL, false},
    {"1 M", "Thirty days", 30LL * 24LL * 60LL * 60LL * 1000LL, false},
    {"1 Y", "One year", 365LL * 24LL * 60LL * 60LL * 1000LL, false},
    {"ALL", "All retained history", 0, true},
};

inline constexpr std::size_t FrontierTimeRangePresetCount =
    sizeof(FrontierTimeRangePresets) / sizeof(FrontierTimeRangePresets[0]);

inline constexpr int FrontierGraphIntervalCount = 6;

struct FrontierTimelinePresetState {
    int intervalHours{4};
    bool fullHistory{false};
    bool zoomActive{false};
};

inline FrontierTimelinePresetState FrontierApplyTimelinePreset(
    const FrontierTimelinePresetState& current, int intervalHours)
{
    FrontierTimelinePresetState next = current;
    next.intervalHours = std::clamp(intervalHours, 1, 24);
    next.zoomActive = false;
    // Dataset selection and time-window selection are orthogonal.  Choosing
    // 1H/4H/12H/1D must never silently switch FULL HISTORY to CURRENT RUN.
    return next;
}

inline FrontierGraphView FrontierPanSingleGraphAxis(const FrontierGraphView& current,
                                                     bool timeAxis,
                                                     double deltaRatio)
{
    FrontierGraphView next = current;
    if (timeAxis) {
        const double span = std::max(1.0, current.maxTimeMs - current.minTimeMs);
        const double shift = -deltaRatio * span;
        next.minTimeMs += shift;
        next.maxTimeMs += shift;
    } else {
        const double span = std::max(1.0e-6, current.maxMerit - current.minMerit);
        const double shift = deltaRatio * span;
        next.minMerit += shift;
        next.maxMerit += shift;
    }
    return next;
}

inline FrontierGraphView FrontierPanGraphView(const FrontierGraphView& current,
                                               double deltaXRatio,
                                               double deltaYRatio)
{
    const double timeSpan = std::max(1.0, current.maxTimeMs - current.minTimeMs);
    const double meritSpan = std::max(1.0e-6, current.maxMerit - current.minMerit);
    const double timeShift = -deltaXRatio * timeSpan;
    const double meritShift = deltaYRatio * meritSpan;
    return {current.minTimeMs + timeShift, current.maxTimeMs + timeShift,
            current.minMerit + meritShift, current.maxMerit + meritShift};
}

inline FrontierGraphView FrontierScaleGraphAxis(const FrontierGraphView& current,
                                                 bool timeAxis,
                                                 double dragPixels)
{
    const double factor = std::exp(std::clamp(dragPixels, -500.0, 500.0) / 180.0);
    FrontierGraphView next = current;
    if (timeAxis) {
        const double center = (current.minTimeMs + current.maxTimeMs) / 2.0;
        const double half = std::max(500.0, (current.maxTimeMs - current.minTimeMs) * factor / 2.0);
        next.minTimeMs = center - half;
        next.maxTimeMs = center + half;
    } else {
        const double center = (current.minMerit + current.maxMerit) / 2.0;
        const double half = std::max(0.025, (current.maxMerit - current.minMerit) * factor / 2.0);
        next.minMerit = center - half;
        next.maxMerit = center + half;
    }
    return next;
}

inline FrontierGraphView FrontierScaleGapGraphAxis(const FrontierGraphView& current,
                                                    bool gapAxis,
                                                    double dragPixels)
{
    const double factor = std::exp(std::clamp(dragPixels, -500.0, 500.0) / 180.0);
    FrontierGraphView next = current;
    if (gapAxis) {
        const double center = (current.minTimeMs + current.maxTimeMs) / 2.0;
        const double half = std::max(0.5,
            (current.maxTimeMs - current.minTimeMs) * factor / 2.0);
        next.minTimeMs = center - half;
        next.maxTimeMs = center + half;
    } else {
        const double center = (current.minMerit + current.maxMerit) / 2.0;
        const double half = std::max(0.025,
            (current.maxMerit - current.minMerit) * factor / 2.0);
        next.minMerit = center - half;
        next.maxMerit = center + half;
    }
    return next;
}

inline double FrontierNiceNumericStep(double rawStep)
{
    rawStep = std::max(rawStep, 1.0e-12);
    const double magnitude = std::pow(10.0, std::floor(std::log10(rawStep)));
    const double normalized = rawStep / magnitude;
    const double nice = normalized <= 1.0 ? 1.0 : (normalized <= 2.0 ? 2.0 :
                         (normalized <= 5.0 ? 5.0 : 10.0));
    return nice * magnitude;
}

// Choose a rounded step no larger than the requested spacing. Rounding UP
// could halve the tick density and leave the upper observations unmarked.
inline double FrontierReadableNumericStep(double rawStep)
{
    if(!std::isfinite(rawStep)||rawStep<=0.)return 1.;
    const double magnitude=std::pow(10.,std::floor(std::log10(rawStep)));
    const double n=rawStep/magnitude;
    return (n>=5.?5.:n>=2.?2.:1.)*magnitude;
}
inline double FrontierNiceTimeStepMs(double rawStepMs)
{
    constexpr double steps[]{1000.0, 5000.0, 10000.0, 30000.0, 60000.0,
                             300000.0, 600000.0, 1800000.0, 3600000.0,
                             7200000.0, 21600000.0, 43200000.0, 86400000.0,
                             604800000.0};
    for (double step : steps) {
        if (step >= rawStepMs) return step;
    }
    return FrontierNiceNumericStep(rawStepMs);
}

// One enclosing fit for every axis policy. Rounding is viewport expansion,
// not permission to slide the lower edge past admitted source coordinates.
template<class RoundStep>
inline std::pair<double,double> FrontierEnclosingGraphBounds(
    double minimum,double maximum,int intervals,double step,RoundStep roundStep)
{
    intervals = std::max(1, intervals);
    double low=std::floor(minimum/step)*step,high=low+step*intervals;
    while(high<maximum){
        const double required=(maximum-low)/intervals;
        step=roundStep(std::nextafter(std::max(step,required),
                                     std::numeric_limits<double>::infinity()));
        low=std::floor(minimum/step)*step;high=low+step*intervals;
    }
    return {low,high};
}

inline FrontierGraphView FrontierNiceGraphView(const FrontierGraphView& requested,
                                                int intervals = 5)
{
    intervals = std::max(1, intervals);
    const double timeStep = FrontierNiceTimeStepMs(
        (requested.maxTimeMs - requested.minTimeMs) / intervals);
    const double meritStep = FrontierNiceNumericStep(
        (requested.maxMerit - requested.minMerit) / intervals);
    const auto time = FrontierEnclosingGraphBounds(requested.minTimeMs, requested.maxTimeMs,intervals,timeStep,FrontierNiceTimeStepMs);
    const auto merit = FrontierEnclosingGraphBounds(requested.minMerit, requested.maxMerit,intervals,meritStep,FrontierNiceNumericStep);
    return {time.first, time.second, merit.first, merit.second};
}

// Integral merit ticks with exactly `intervals` divisions. Fitting may expand
// the requested bounds, NEVER discard their lower edge to fit the upper one.
// The time geometry remains untouched.
inline FrontierGraphView FrontierIntegerMeritGraphView(
    const FrontierGraphView& requested, int intervals = FrontierGraphIntervalCount)
{
    intervals = std::max(1, intervals);
    const double span = std::max(1.0, requested.maxMerit - requested.minMerit);
    const double step = std::max(1.0, std::ceil(span / intervals));
    const auto fitted=FrontierEnclosingGraphBounds(requested.minMerit,requested.maxMerit,
        intervals,step,[](double value){return std::ceil(value);});
    double minimum=fitted.first,maximum=fitted.second;
    if (minimum < 0.0) {
        minimum = 0.0;
        maximum = fitted.second-fitted.first;
    }
    return {requested.minTimeMs, requested.maxTimeMs, minimum, maximum};
}

// The canonical scientific graph uses an integer gap length on X.  Keep this
// separate from FrontierNiceGraphView so legacy diagnostic timelines retain
// their millisecond step table without imposing a 1000-unit minimum on small
// prime gaps.
inline FrontierGraphView FrontierNiceGapGraphView(const FrontierGraphView& requested,
                                                   int intervals = 5)
{
    intervals = std::max(1, intervals);
    const double gapStep = FrontierNiceNumericStep(
        (requested.maxTimeMs - requested.minTimeMs) / intervals);
    const double meritStep = FrontierNiceNumericStep(
        (requested.maxMerit - requested.minMerit) / intervals);
    const auto gap = FrontierEnclosingGraphBounds(requested.minTimeMs,requested.maxTimeMs,intervals,gapStep,FrontierNiceNumericStep);
    const auto merit = FrontierEnclosingGraphBounds(requested.minMerit,requested.maxMerit,intervals,meritStep,FrontierNiceNumericStep);
    return {gap.first, gap.second, merit.first, merit.second};
}

// Fit observed data, not a prescribed number of rounded tick intervals. X is
// already the selected exact time/length domain and is never expanded here.
inline std::optional<FrontierGraphView> FrontierContentGraphView(
    const FrontierGraphView& observed, double paddingFraction = 0.025)
{
    if (!std::isfinite(observed.minTimeMs) || !std::isfinite(observed.maxTimeMs) ||
        !std::isfinite(observed.minMerit) || !std::isfinite(observed.maxMerit) ||
        observed.maxTimeMs <= observed.minTimeMs || observed.minMerit < 0.0 ||
        observed.maxMerit < observed.minMerit || !std::isfinite(paddingFraction) ||
        paddingFraction < 0.0 || paddingFraction > 0.1) return std::nullopt;
    const double span = observed.maxMerit - observed.minMerit;
    const double pad = span > 0.0 ? std::max(0.05, span * paddingFraction) : 0.65;
    const double low = std::max(0.0, observed.minMerit - pad);
    const double limit = std::numeric_limits<double>::max();
    const double high = observed.maxMerit > limit - pad ? limit : observed.maxMerit + pad;
    if (high <= low) return std::nullopt;
    return FrontierGraphView{observed.minTimeMs, observed.maxTimeMs, low, high};
}

// Ticks are exact values INSIDE the viewport; integral labels need not force
// empty bands above the data or round a nonintegral coordinate to a false label.
inline std::vector<double> FrontierVisibleMeritTicks(const FrontierGraphView& view,
                                                     int intervals, bool integral)
{
    std::vector<double> result;
    if (!std::isfinite(view.minMerit) || !std::isfinite(view.maxMerit) ||
        view.maxMerit <= view.minMerit) return result;
    intervals = std::clamp(intervals, 1, 64);
    double step = FrontierReadableNumericStep((view.maxMerit - view.minMerit) / intervals);
    if (integral) step = std::max(1.0, std::ceil(step));
    if (!std::isfinite(step) || step <= 0.0) return result;
    double value = std::ceil(view.minMerit / step) * step;
    for (int n = 0; n <= 3 * intervals + 1 && value <= view.maxMerit; ++n) {
        if (std::isfinite(value) && value >= view.minMerit) result.push_back(value);
        const double next = value + step;
        if (next <= value) break;
        value = next;
    }
    return result;
}

enum class FrontierNowViewportRelation { Before, Inside, After };

inline FrontierNowViewportRelation FrontierNowRelation(const FrontierGraphView& view,
                                                        double nowMs)
{
    if (nowMs < view.minTimeMs) return FrontierNowViewportRelation::Before;
    if (nowMs > view.maxTimeMs) return FrontierNowViewportRelation::After;
    return FrontierNowViewportRelation::Inside;
}

inline double FrontierClampMarkerCoordinate(double value, double minimum, double maximum,
                                            double margin)
{
    return std::clamp(value, minimum + margin, maximum - margin);
}

inline bool FrontierGraphDragExceedsThreshold(double deltaX, double deltaY,
                                               double threshold = 4.0)
{
    return std::hypot(deltaX, deltaY) >= threshold;
}

inline bool FrontierScreenPointsCluster(double leftX, double leftY,
                                        double rightX, double rightY,
                                        double radius = 12.0)
{
    return std::hypot(leftX - rightX, leftY - rightY) <= radius;
}

inline FrontierGraphView FrontierZoomGraphView(const FrontierGraphView& current,
                                                double cursorXRatio,
                                                double cursorYRatio,
                                                double wheelSteps,
                                                double minimumTimeSpanMs = 1000.0)
{
    cursorXRatio = std::clamp(cursorXRatio, 0.0, 1.0);
    cursorYRatio = std::clamp(cursorYRatio, 0.0, 1.0);
    // Preserve the exact value under the cursor while using a deliberately
    // gentle per-notch factor.  Fractional wheel steps support high-resolution
    // touchpads without quantization jumps.
    const double factor = std::pow(1.18, wheelSteps);
    const double timeSpan = std::max(1.0, current.maxTimeMs - current.minTimeMs);
    const double meritSpan = std::max(1.0e-6, current.maxMerit - current.minMerit);
    constexpr double maxTimeSpanMs = 10.0 * 365.0 * 24.0 * 60.0 * 60.0 * 1000.0;
    const double nextTimeSpan = std::clamp(timeSpan / factor,
                                           std::max(1.0, minimumTimeSpanMs),
                                           maxTimeSpanMs);
    const double nextMeritSpan = std::clamp(meritSpan / factor, 0.05, 10000.0);
    const double anchorTime = current.minTimeMs + cursorXRatio * timeSpan;
    const double anchorMerit = current.maxMerit - cursorYRatio * meritSpan;
    return {
        anchorTime - cursorXRatio * nextTimeSpan,
        anchorTime + (1.0 - cursorXRatio) * nextTimeSpan,
        anchorMerit - (1.0 - cursorYRatio) * nextMeritSpan,
        anchorMerit + cursorYRatio * nextMeritSpan,
    };
}

inline FrontierGraphView FrontierZoomGapGraphView(const FrontierGraphView& current,
                                                   double cursorXRatio,
                                                   double cursorYRatio,
                                                   double wheelSteps)
{
    cursorXRatio = std::clamp(cursorXRatio, 0.0, 1.0);
    cursorYRatio = std::clamp(cursorYRatio, 0.0, 1.0);
    const double factor = std::pow(1.18, wheelSteps);
    const double gapSpan = std::max(1.0, current.maxTimeMs - current.minTimeMs);
    const double meritSpan = std::max(1.0e-6, current.maxMerit - current.minMerit);
    const double nextGapSpan = std::clamp(gapSpan / factor, 1.0,
                                          static_cast<double>(1ULL << 53));
    const double nextMeritSpan = std::clamp(meritSpan / factor, 0.05, 10000.0);
    const double anchorGap = current.minTimeMs + cursorXRatio * gapSpan;
    const double anchorMerit = current.maxMerit - cursorYRatio * meritSpan;
    return {
        anchorGap - cursorXRatio * nextGapSpan,
        anchorGap + (1.0 - cursorXRatio) * nextGapSpan,
        anchorMerit - (1.0 - cursorYRatio) * nextMeritSpan,
        anchorMerit + cursorYRatio * nextMeritSpan,
    };
}

#pragma once
#include <QVector>
#include <QtGlobal>
#include <optional>

enum class FrontierGraphRange {
    OneMinute, FiveMinutes, FifteenMinutes, OneHour, FourHours, OneDay,
    OneWeek, OneMonth, ThreeMonths, SixMonths, OneYear, All,
};
struct FrontierGraphRangePreset {
    FrontierGraphRange range{FrontierGraphRange::All};
    const char* label{"All"};
    std::optional<qint64> durationMs;
};
// One canonical preset set: extracted from the Gapcoin graph model.
inline QVector<FrontierGraphRangePreset> FrontierGraphRangePresets()
{
    constexpr qint64 minute=60000, hour=60*minute, day=24*hour, month=30*day;
    return {
        {FrontierGraphRange::OneMinute,"1m",minute},
        {FrontierGraphRange::FiveMinutes,"5m",5*minute},
        {FrontierGraphRange::FifteenMinutes,"15m",15*minute},
        {FrontierGraphRange::OneHour,"1h",hour},
        {FrontierGraphRange::FourHours,"4h",4*hour},
        {FrontierGraphRange::OneDay,"1d",day},
        {FrontierGraphRange::OneWeek,"1w",7*day},
        {FrontierGraphRange::OneMonth,"1mo",month},
        {FrontierGraphRange::ThreeMonths,"3mo",3*month},
        {FrontierGraphRange::SixMonths,"6mo",6*month},
        {FrontierGraphRange::OneYear,"1y",365*day},
        {FrontierGraphRange::All,"All",std::nullopt},
    };
}

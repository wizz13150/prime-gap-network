#pragma once
#include <QString>
#include <array>
#include <cmath>
#include <optional>

namespace pgn::ui {
// A future producer UTC timestamp cannot establish a negative data age.
// Preserve the raw measurement; presentation reports it as unavailable.
inline QString telemetryAgeCaption(const std::optional<qint64>& milliseconds,
                                   bool lowerBound = false)
{
    if (!milliseconds || *milliseconds < 0) return QStringLiteral("—");
    return (lowerBound ? QStringLiteral("≥") : QString{}) +
        QString::number(*milliseconds) + QStringLiteral("ms");
}

// Presentation only: never replace, average or write the measured rate.
// Expanded, compact and TOTAL cards use exactly the same caption policy.
inline QString compactMetricCaption(const std::optional<double>& value)
{
    if (!value || !std::isfinite(*value) || *value < 0.0) return QStringLiteral("—");
    if (*value == 0.0) return QStringLiteral("0");
    if (*value < 0.01) return QStringLiteral("<0.01");
    if (*value >= 1.0e27)
        return QString::number(*value, 'g', 1).replace(QStringLiteral("e+"), QStringLiteral("e"));
    static const std::array<QString, 9> units{
        QString{}, QStringLiteral("K"), QStringLiteral("M"), QStringLiteral("G"),
        QStringLiteral("T"), QStringLiteral("P"), QStringLiteral("E"),
        QStringLiteral("Z"), QStringLiteral("Y")};
    double scaled = *value;
    std::size_t unit = 0;
    // Include rounding carry: never show an overlong "1000M" at a unit edge.
    while (scaled >= 999.5 && unit + 1 < units.size()) { scaled /= 1000.0; ++unit; }
    const int decimals = scaled >= 100.0 ? 0 : scaled >= 10.0 ? 1 : 2;
    QString number = QString::number(scaled, 'f', decimals);
    if (number.contains(QLatin1Char('.'))) {
        while (number.endsWith(QLatin1Char('0'))) number.chop(1);
        if (number.endsWith(QLatin1Char('.'))) number.chop(1);
    }
    return number + units[unit];
}
} // namespace pgn::ui

#pragma once

#include <QDateTime>
#include <QString>
#include <QTimeZone>

#include <algorithm>

inline qint64 FrontierTimelineUpperBoundUtcMs(qint64 lastEventUtcMs,
                                              qint64 nowUtcMs,
                                              qint64 tickStepMs)
{
    const qint64 step = std::max<qint64>(1, tickStepMs);
    const qint64 newest = std::max(lastEventUtcMs, nowUtcMs);
    const qint64 quotient = newest / step;
    // Always reserve the next tick for the visibly distinct FUTURE region;
    // NOW remains at the exact wall-clock position inside that axis.
    return (quotient + 1) * step;
}

inline QString FrontierTimelineTickLabelInZone(qint64 utcTimestampMs,
                                                qint64 spanMs,
                                                const QTimeZone& zone)
{
    const QDateTime local = QDateTime::fromMSecsSinceEpoch(utcTimestampMs, Qt::UTC)
                                .toTimeZone(zone);
    return spanMs < 24LL * 60 * 60 * 1000
        ? local.toString(QStringLiteral("HH:mm:ss"))
        : local.toString(QStringLiteral("MM-dd HH:mm"));
}

inline QString FrontierLocalTimelineTickLabel(qint64 utcTimestampMs, qint64 spanMs)
{
    return FrontierTimelineTickLabelInZone(utcTimestampMs, spanMs,
                                           QTimeZone::systemTimeZone());
}

inline double FrontierTimelineX(qint64 minUtcMs,
                                qint64 maxUtcMs,
                                qint64 instantUtcMs,
                                double left,
                                double width)
{
    if (maxUtcMs <= minUtcMs || width <= 0.0) return left;
    const double ratio = std::clamp(
        (instantUtcMs - minUtcMs) / static_cast<double>(maxUtcMs - minUtcMs), 0.0, 1.0);
    return left + ratio * width;
}

inline QString FrontierLocalNowLabel(qint64 utcTimestampMs)
{
    return QStringLiteral("NOW · %1")
        .arg(QDateTime::fromMSecsSinceEpoch(utcTimestampMs, Qt::UTC)
                 .toLocalTime().toString(QStringLiteral("HH:mm:ss")));
}

inline QString FrontierNowLabelInZone(qint64 utcTimestampMs, const QTimeZone& zone)
{
    return QStringLiteral("NOW · %1")
        .arg(QDateTime::fromMSecsSinceEpoch(utcTimestampMs, Qt::UTC)
                 .toTimeZone(zone).toString(QStringLiteral("HH:mm:ss")));
}

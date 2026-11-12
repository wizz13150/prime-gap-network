#pragma once
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace pgn::ui {
inline constexpr qint64 BusyHistoryWindowMs = 120000;
inline constexpr qint64 BusyHistoryMaximumGapMs = 15000;
struct BusyHistoryPoint { QPointF position; bool connect = false; };

// Observation time owns horizontal distance. Missing/stale/out-of-order samples
// break a segment; an actual zero remains an observation at the bottom edge.
template<class History>
QVector<BusyHistoryPoint> busyHistoryGeometry(const History& history,
                                            const QRectF& area,
                                            qint64 rightUtcMs)
{
    QVector<BusyHistoryPoint> result;
    if (area.isEmpty()) return result;
    qint64 previous = 0;
    bool drawing = false;
    for (const auto& sample : history) {
        const qint64 time = sample.observedUtc.toMSecsSinceEpoch();
        if (!sample.observedUtc.isValid() || !sample.utilizationEmitted ||
            !std::isfinite(sample.utilization) || time > rightUtcMs ||
            time < rightUtcMs - BusyHistoryWindowMs) {
            drawing = false;
            continue;
        }
        const bool connect = drawing && time > previous &&
                             time - previous <= BusyHistoryMaximumGapMs;
        const qreal x = area.right() - area.width() *
                       (rightUtcMs - time) / qreal(BusyHistoryWindowMs);
        const qreal y = area.bottom() - area.height() *
                        std::clamp(sample.utilization / 100.0, 0.0, 1.0);
        result.append({QPointF(x, y), connect});
        previous = time;
        drawing = true;
    }
    return result;
}
}

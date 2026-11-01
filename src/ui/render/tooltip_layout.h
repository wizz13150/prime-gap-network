#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

struct FrontierTooltipPlacement {
    QRectF rect;
    QPointF leaderAnchor;
};

inline bool FrontierRectIntersectsCircle(const QRectF& rect,
                                         const QPointF& center,
                                         double radius)
{
    const double closestX = std::clamp(center.x(), rect.left(), rect.right());
    const double closestY = std::clamp(center.y(), rect.top(), rect.bottom());
    const double dx = center.x() - closestX;
    const double dy = center.y() - closestY;
    return dx * dx + dy * dy < radius * radius;
}

inline FrontierTooltipPlacement FrontierPlaceFixedTooltip(const QRectF& bounds,
                                                           const QPointF& cursor,
                                                           const QPointF& target,
                                                           const QSizeF& size)
{
    constexpr double offsetX = 24.0;
    constexpr double offsetY = 18.0;
    double x = cursor.x() + offsetX;
    double y = cursor.y() + offsetY;
    if (x + size.width() > bounds.right()) x = cursor.x() - offsetX - size.width();
    if (y + size.height() > bounds.bottom()) y = cursor.y() - offsetY - size.height();
    x = std::clamp(x, bounds.left(), bounds.right() - size.width());
    y = std::clamp(y, bounds.top(), bounds.bottom() - size.height());
    QRectF rect(x, y, size.width(), size.height());

    // Edge flips above are normally sufficient.  These deterministic final
    // adjustments preserve the hard cursor/point exclusion without choosing
    // among dynamic quadrants.
    if (FrontierRectIntersectsCircle(rect, cursor, 24.0) ||
        FrontierRectIntersectsCircle(rect, target, 14.0)) {
        const double leftX = cursor.x() - offsetX - size.width();
        if (leftX >= bounds.left()) rect.moveLeft(leftX);
        const double aboveY = cursor.y() - offsetY - size.height();
        if ((FrontierRectIntersectsCircle(rect, cursor, 24.0) ||
             FrontierRectIntersectsCircle(rect, target, 14.0)) && aboveY >= bounds.top()) {
            rect.moveTop(aboveY);
        }
    }
    const auto valid = [&](const QRectF& candidate) {
        return bounds.contains(candidate) &&
            !FrontierRectIntersectsCircle(candidate, cursor, 24.0) &&
            !FrontierRectIntersectsCircle(candidate, target, 14.0);
    };
    if (!valid(rect)) {
        const auto clamped = [&](QRectF candidate) {
            candidate.moveLeft(std::clamp(candidate.left(), bounds.left(),
                                           bounds.right() - candidate.width()));
            candidate.moveTop(std::clamp(candidate.top(), bounds.top(),
                                          bounds.bottom() - candidate.height()));
            return candidate;
        };
        std::array<QRectF, 4> adjusted{rect, rect, rect, rect};
        adjusted[0].moveRight(std::min(cursor.x() - 24.0, target.x() - 14.0));
        adjusted[1].moveLeft(std::max(cursor.x() + 24.0, target.x() + 14.0));
        adjusted[2].moveBottom(std::min(cursor.y() - 24.0, target.y() - 14.0));
        adjusted[3].moveTop(std::max(cursor.y() + 24.0, target.y() + 14.0));
        double bestDistance = std::numeric_limits<double>::max();
        for (QRectF candidate : adjusted) {
            candidate = clamped(candidate);
            if (!valid(candidate)) continue;
            const QPointF delta = candidate.topLeft() - rect.topLeft();
            const double distance = delta.x() * delta.x() + delta.y() * delta.y();
            if (distance < bestDistance) {
                rect = candidate;
                bestDistance = distance;
            }
        }
    }
    const QPointF leader(std::clamp(target.x(), rect.left(), rect.right()),
                         std::clamp(target.y(), rect.top(), rect.bottom()));
    return {rect, leader};
}

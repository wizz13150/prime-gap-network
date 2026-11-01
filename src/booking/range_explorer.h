#pragma once
#include "booking/source_registry.h"
#include <QStringList>

namespace pgn::booking {
enum class RangeBound {Length, ExclusiveEnd};
struct RangeSelection {
    QString start, end, length;
    int firstBits{}, lastBits{};
};
struct RangeAvailability {
    bool fullyPublished{}, declaredOpen{};
    QString status;
    QStringList owners;
    qsizetype rows{};
};

// User input is converted directly to integer digits. No floating-point value
// is ever used as a reservation, report, checkpoint or execution coordinate.
QString rangeInputInteger(QString text);
RangeSelection rangeSelection(const QString& first,const QString& second,RangeBound bound);
RangeAvailability rangeAvailability(const SourceRegistry&,const RangeSelection&);

// Pan/zoom uses a local integer origin before converting to screen coordinates.
// Even a one-position selection near2^95 retains its exact endpoints.
QString rangePositionAtPixel(const QString& first,const QString& last,int pixel,int width);
}

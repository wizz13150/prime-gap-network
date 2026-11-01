#pragma once
#include "ui/range_atlas.h"
#include "booking/source_registry.h"
#include "ui/booking_context_request.h"
#include <QRectF>
#include <QVector>
#include <functional>
#include <optional>
namespace pgn::ui {
struct RangeCalendarSlot {QString start,end;};
struct RangeCalendarInterval {double begin,end;std::optional<double> progressEnd;};
// Exact endpoint clipping precedes pixel conversion. A partial local job must
// never visually claim the entire cell or its unsearched leading portion.
std::optional<RangeCalendarInterval> rangeCalendarInterval(const RangeCalendarSlot& cell,
    const QString& start,const QString& end,const QString& cursor={});
// Complete disjoint exact partition, including a non-divisible final remainder.
QVector<RangeCalendarSlot> rangeCalendarSlots(const QString& start,const QString& end,int maximum=100);
QWidget* createRangeCalendar(pgn::booking::BookingModel*,std::function<void(const QString&,const QString&)>,DeviceAccent={},QWidget* parent=nullptr);
QRectF rangeCalendarCell(QWidget*,int index);
// Exact selected cell bounds; camera/display state only, never a reservation.
std::optional<RangeCalendarSlot> rangeCalendarSelection(QWidget*);
QVector<RangeCalendarSlot> rangeCalendarSelections(QWidget*);
void bindRangeCalendarSelection(QWidget*,std::function<void(const QVector<RangeCalendarSlot>&)>);
// False leaves the current selection unchanged when no displayed zone
// fully contains the requested interval. Never silently report an apply.
bool selectRangeCalendar(QWidget*,const QString& start,const QString& end);
// Inspect a source zone without choosing every block or creating work.
void inspectRangeCalendar(QWidget*,const QString& start,const QString& end);
void focusCurrentRangeCalendar(QWidget*);
void bindRangeCalendarBrowse(QWidget*,std::function<void(const QString&,const QString&)>);
void bindRangeCalendarContext(QWidget*,BookingContextHandler);
void bindRangeCalendarClear(QWidget*,std::function<void()>);
}

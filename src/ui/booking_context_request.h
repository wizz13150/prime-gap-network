#pragma once
#include <QString>
#include <QPoint>
#include <functional>
#include "booking/booking_store.h"
namespace pgn::ui {
struct BookingContextRequest {QString start,end,zoneStart,zoneEnd;QPoint global;bool blockActions{true};QVector<pgn::booking::ExactBlockSelection> blocks,zones;};
using BookingContextHandler=std::function<void(const BookingContextRequest&)>;
}

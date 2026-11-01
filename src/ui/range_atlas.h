#pragma once
#include <QWidget>
#include <QColor>
#include <functional>
#include "ui/booking_context_request.h"
namespace pgn::booking {class BookingModel;}
namespace pgn::ui {
using DeviceAccent=std::function<QColor(const QString&)>;
QWidget* createRangeAtlas(pgn::booking::BookingModel*,bool local,
    std::function<void(const QString&,const QString&)> inspectSource={},DeviceAccent accent={},QWidget* parent=nullptr);
QRectF rangeAtlasJobRect(QWidget*,const QString& id);
void bindRangeAtlasContext(QWidget*,BookingContextHandler);
void focusRangeAtlas(QWidget*,const QString& start,const QString& end);
}

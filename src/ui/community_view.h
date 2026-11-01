#pragma once
#include <QWidget>
#include "ui/range_atlas.h"
namespace pgn::booking {class BookingModel;}
namespace pgn::ui {
QWidget* createCommunityView(pgn::booking::BookingModel*,QWidget* parent=nullptr,DeviceAccent accent={});
void selectCommunityBooking(QWidget*,const QString& id);
}

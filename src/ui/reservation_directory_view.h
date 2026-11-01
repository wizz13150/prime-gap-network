#pragma once
#include <QWidget>
#include <functional>
namespace pgn::booking {class BookingModel;}
namespace pgn::ui {
QWidget* createReservationDirectory(pgn::booking::BookingModel*,std::function<void(const QString&,const QString&)>,QWidget* parent=nullptr);
}

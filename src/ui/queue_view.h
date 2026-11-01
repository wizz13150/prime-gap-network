#pragma once
#include <QWidget>
#include <QString>
#include <functional>
namespace pgn::booking {class BookingModel;}
namespace pgn::ui {
QWidget* createQueueView(pgn::booking::BookingModel*,std::function<void(const QString&,const QString&)> inspect,QWidget* parent=nullptr);
}

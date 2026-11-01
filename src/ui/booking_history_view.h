#pragma once
#include <QString>
class QWidget;
namespace pgn::ui {
QWidget* createBookingHistoryView(const QString& database,QWidget* parent=nullptr);
}

#pragma once
class QDialog;
class QWidget;
namespace pgn::booking { class BookingModel; }
namespace pgn::ui {
QDialog* createResearcherProfileDialog(booking::BookingModel* model,QWidget* parent=nullptr);
}

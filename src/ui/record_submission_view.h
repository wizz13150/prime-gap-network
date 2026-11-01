#pragma once
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
class QWidget;
namespace pgn::ui {
QWidget* createRecordSubmissionView(const QString& productRoot,QWidget* parent=nullptr,bool ticketMode=false);
void bindRecordEligibility(QWidget*,std::function<void(const QJsonArray&)>);
void presentRecordTicketData(QWidget*,const QJsonObject&);
}

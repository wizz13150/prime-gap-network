#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
struct FrontierTelemetrySnapshot;
namespace pgn::ui {
QWidget* createMiningResultsView(const QString& productRoot,QWidget* parent=nullptr);
void presentMiningRecordEligibility(QWidget*,const QJsonArray&);
void presentMiningResultsSource(QWidget*,const FrontierTelemetrySnapshot&);
void presentMiningTicketData(QWidget*,const QJsonObject&);
void presentMiningResultsIndex(QWidget*,const QJsonObject&);
void presentMiningProductivity(QWidget*,const QJsonArray&,const QString& ticketWidth);
void presentMiningNetworkMetrics(QWidget*,const QJsonObject&,const QString& expectedGenesis);
}

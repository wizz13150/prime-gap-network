#pragma once
#include <QWidget>
#include <QJsonObject>
#include <functional>
struct FrontierTelemetrySnapshot;
struct FrontierResourceTelemetrySnapshot;
namespace pgn::ui {
QWidget* createBenchmarkView(const QString& root,QWidget* parent=nullptr);
QWidget* createEngineConfigurationView(const QString& root,QWidget* parent=nullptr);
void presentEngineTools(QWidget*,const FrontierTelemetrySnapshot&);
void presentBenchmarkResources(QWidget*,const FrontierResourceTelemetrySnapshot&);
bool importEngineProfile(QWidget*,const QString& path);
// Read-only producer observation. Never launches or applies a module. Timestamp
// and artifact identity are supplied by the caller, never synthesized here.
bool presentEngineModuleCapabilities(QWidget*,const QJsonObject& capabilities,
    const QString& observedUtc={},const QString& artifactSha256={});
void bindEngineModuleActions(QWidget*,std::function<void(const QJsonObject&)>);
bool addEngineModuleDescriptor(QWidget*,const QString& path);
void presentEngineModuleRegistry(QWidget*,const QJsonObject& receipt);
}

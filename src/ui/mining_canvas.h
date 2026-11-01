#pragma once
#include <QWidget>
#include <QVariant>
#include <QJsonObject>
#include <QJsonArray>
#include <QColor>
#include <functional>
#include "wallet/chain_observation.h"
#include "verification/journal_reader.h"

struct FrontierTelemetrySnapshot;
struct FrontierResourceTelemetrySnapshot;
struct FrontierGapEvent;
namespace pgn::booking {class BookingModel;}

namespace pgn::ui {
QRectF miningToggleRect(QWidget*,const QString&);
QRectF miningBudgetRect(QWidget*,const QString&);
void scrollMiningDevicesEnd(QWidget*);
QWidget* createMiningCanvas(QWidget* parent = nullptr);
// Native Mining uses compact device controls; scientific plots live in Number Lens.
void configureMiningDashboard(QWidget*);
// A viewport may shrink without forcing the fixed-band scientific dashboard
// to overlap its instruments or reduce its text below readable sizes.
QWidget* createMiningViewport(QWidget* canvas, QWidget* parent = nullptr);
// Scientific workspace reuses the exact indexed graph painter/controls, with
// no device cards, controller binding or secondary animation clock.
QWidget* createNumberLensCanvas(QWidget* parent = nullptr);
void bindObservationCatalog(QWidget*,const QString& database);
void bindNativeCatalog(QWidget*,const QString& database);
bool selectNativeGraphAxis(QWidget*,const QString& axis);
void presentNativeIndex(QWidget*,const QJsonObject&);
QJsonObject catalogDiagnostics(QWidget*);
void bindBookingModel(QWidget*,pgn::booking::BookingModel*);
QRectF reservationSegmentRect(QWidget*,const QString& id);
QColor miningDeviceAccent(QWidget*,const QString& device);
void presentDevice(QWidget*, const FrontierTelemetrySnapshot&, bool secondary = false);
// Isolated control observations never replace the production source or graph.
void presentTestDevice(QWidget*,const FrontierTelemetrySnapshot&);
void presentCpuInventory(QWidget*, const QJsonObject&);
void presentCpuController(QWidget*, const QJsonObject& inventory, const QJsonObject& status, bool connected=true);
void presentResources(QWidget*, const FrontierResourceTelemetrySnapshot&);
void presentNetwork(QWidget*,const std::optional<pgn::wallet::ChainObservation>&);
void presentVerificationReceipt(QWidget*,const pgn::verification::LatestReceipt&);
void presentLocalSourceProofStatus(QWidget*,const QJsonObject&);
void presentGap(QWidget*, const FrontierGapEvent&, bool secondary = false);
void bindMiningControls(QWidget*, std::function<void(const QString&, const QString&, bool)>);
void bindMiningResourceControls(QWidget*, std::function<void(const QString&, const QString&, bool, int)>);
void resolveDeviceControl(QWidget*, const QString& deviceId, const QString& requestId,
                          bool applied, const QString& detail);
QVariantList miningCardStates(QWidget*);
QJsonArray miningDeviceRates(QWidget*);
QStringList miningTooltips(QWidget*);
void beginHistoryBatch(QWidget*);
void endHistoryBatch(QWidget*);
}

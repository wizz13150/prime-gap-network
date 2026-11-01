#pragma once
#include <QObject>
#include <QProcess>
#include <QPointer>
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <functional>
#include "telemetry.h"
#include "mining/raw_metrics.h"
namespace pgn::mining {
enum class NativeInventoryPurpose { Start,Close,Boundary };
enum class NativeInventoryPhaseKind { Profile,Snapshot,Lineage,SourceRead };
struct NativeApplicationInventoryPhase {
 QString requestId,inventoryId,phaseId;
 NativeInventoryPurpose purpose{NativeInventoryPurpose::Boundary};
 NativeInventoryPhaseKind phase{NativeInventoryPhaseKind::Profile};
 bool active{};
};
class NativeBridge final : public QObject {
public:
 explicit NativeBridge(QObject* parent=nullptr);
 ~NativeBridge() override;
 void start(const QString& node, const QString& script, const QString& transportConfig,bool indexed=false,bool residentViewer=false);
 // Separate local capability, not inferred from packets or residentViewer.
 // True accepts an asynchronous launch attempt; it is not process readiness.
 bool startNativeObserver(const QString& program,const QStringList& args);
 // In-process channel for the unified native runtime. Attachment is only a
 // local capability, NOT connectivity/physical ownership. Requests are queued
 // on this lifetime owner's thread; no reentrant acknowledgement or retry.
 // No Node/SSH/process is launched by either method.
 using NativeRequestSink=std::function<bool(const QJsonObject&)>;
 bool attachNativeRuntime(QObject* lifetime,NativeRequestSink);
  // Observation capability refuses commands before they reach a request sink.
  // A catalogue/control attempt must not invalidate subsequent telemetry.
  bool attachNativeObservation(QObject* lifetime);
 bool acceptNativeRuntimePacket(QObject* lifetime,const QJsonObject&,std::optional<qint64> resourceReceiptNs={});
 void control(const QString& device, const QString& request, bool enabled);
 // No allocation or physical ACK: initial start follows native admission.
 bool inspectApplicationStart(const QString& requestId,const QString& inventoryId);
 void cancelApplicationStart(const QString& requestId);
 bool controlPreparedInitial(const QString& device,const QString& requestId,
                             const QString& observationId,const QJsonObject& base,
                             const QString& bookingId,const QString& start,const QString& end);
 bool controlPreparedResume(const QString& device,const QString& requestId,
                            const QString& observationId,const QJsonObject& base,
                            const QString& bookingId,const QString& nextRun,
                            const QString& checkpointSha256);
 std::function<void(const QString&,const QJsonObject&)> onApplicationStartInventory;
 // ONLY the attached native creating-thread channel can deliver these typed
 // lifecycle phases. Not physical progress, proof, freshness or permission.
 // Boundary currently has no UI latch and cannot suspend Start/Close timers.
 std::function<void(const NativeApplicationInventoryPhase&)> onApplicationInventoryPhase;
 // Retire ONLY an exact local phase lease (Held/cancel). Does not unfreeze
 // admission, cancel a transport/worker or modify inventory/proof contents.
 void retireApplicationInventoryPhases(NativeInventoryPurpose,const QString& requestId,const QString& inventoryId);
 // Authenticated relay observation, after physical-row/device projection.
 // Consumers still validate scope/identity before persisting an origin.
 std::function<void(const QJsonObject&)> onSourceSnapshot;
 void dispatch(const QString& bookingId);
 void moduleAction(const QJsonObject& action);
 // Correlated local admission freeze/inventory only, never permission to quit.
 // STOP remains available; new starts/dispatch/module mutations are refused.
 bool prepareApplicationClose(const QString& requestId);
 bool inspectApplicationClose(const QString& requestId,const QString& inventoryId);
 std::function<void(const QString&,bool,const QString&)> onApplicationCloseFrozen;
 std::function<void(const QString&,const QJsonObject&)> onApplicationCloseInventory;
 std::function<void(const QString&,bool,const QString&)> onDispatch;
 std::function<void(const FrontierTelemetrySnapshot&)> onDevice;
 std::function<void(const FrontierTelemetrySnapshot&)> onTestDevice;
 std::function<void(const FrontierResourceTelemetrySnapshot&)> onResources;
 std::function<void(const FrontierGapEvent&)> onGap;
 std::function<void(const QString&,const QString&,bool,const QString&)> onControl;
 std::function<void(const QString&)> onStatus;
 std::function<void(const QJsonObject&)> onNativeIndex;
 std::function<void(const QJsonObject&,bool)> onSourceCompletion;
 std::function<void(const QJsonObject&,const QString&,const QString&)> onModuleCapabilities;
 std::function<void(const QJsonObject&)> onModuleRegistry;
 std::function<void()> onBatchBegin,onBatchEnd;
private:
 void publishResources(const QJsonObject&,std::optional<qint64> resourceReceiptNs={});
 friend struct NativeBridgeTestAccess;
 void consumeBytes(const QByteArray&);
 void receive(const QJsonObject&,std::optional<qint64> resourceReceiptNs={});
 bool receiveNativeInventoryPhase(const QJsonObject&);
 void relayUnavailable(const QString& detail);
 bool runtimeAvailable() const;
 bool writeCommand(const QByteArray&);
 QPointer<QObject> nativeRuntime_;
 NativeRequestSink nativeSend_;
 bool nativeUsable_{};
 QProcess relay_; QByteArray incoming_; RawMetrics metrics_;
 FrontierTelemetrySnapshot lane_; QDateTime lastCounters_; qint64 sequence_{};
 std::uint64_t lastCounterClock_{};
 struct Pending {QString device; bool enabled;QString bookingId,start,end,resumeRun,previousRun,checkpointSha256;}; QHash<QString,Pending> pending_;
 QSet<QString> pendingDispatch_;
 bool historyBatchOpen_{};
 bool observationOnly_{};
 QString applicationCloseId_;
 QString applicationCloseInventoryId_;
 QString applicationStartId_,applicationStartInventoryId_;
 QString acceptedStartId_,acceptedStartInventoryId_;
 struct InventoryPhaseLatch {bool open{};QString id;NativeInventoryPhaseKind phase{NativeInventoryPhaseKind::Profile};};
 InventoryPhaseLatch startPhase_,closePhase_;
 QSet<QString> consumedInventoryPhases_;
};
}

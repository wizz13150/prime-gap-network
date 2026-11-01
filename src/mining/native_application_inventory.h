#pragma once
#include "mining/native_application_writer.h"
#include "mining/native_control_transport.h"
#include "mining/native_source_runtime.h"
#include <QObject>
#include <functional>
#include <memory>

namespace pgn::mining {
struct NativeApplicationInventoryConfig {
    NativeSourceRuntimeConfig source;
    QString bookingDatabase;
    NativeControlTimeouts finiteTimeouts;
    bool timingAudit{false}; // opt-in metadata only; never changes acquisition/Ready
    bool finiteManaged{true}; // trusted local scope; false never fabricates an idle observation
};
// Local acquisition data, not an ACK or an authenticated capability. The
// control component owns the private one-use no-op ticket and rechecks it.
struct NativeCloseInitialObservation {
    QString closeId,inventoryId;quint64 epoch{};qint64 beganMonotonicMs{};
    SshObservationFrame constant;NativeFiniteObservation finite;
};
struct NativeApplicationInventoryCallbacks {
    std::function<void(const QJsonObject&)> packet;
    // Native-only progress, delivered immediately rather than queued behind
    // the writer whose real acquisition/lineage operation it describes.
    std::function<void(const QJsonObject&)> phase;
    // Called after each ACTUAL start acquisition. A stopped boundary is
    // acquired again after lineage; never refresh a timer on the old DTO.
    std::function<void(const QString&,const QString&,const SshObservationFrame&)> startObserved;
    std::function<void(const NativeCloseInitialObservation&)> closeInitialObserved;
    std::function<void(const NativeSourcePublication&)> publication;
    // Independent exact graph read snapshot, never SOURCE admission or a
    // control/close prerequisite. Delivered on the creating thread.
    std::function<void(const QJsonObject&)> nativeIndex;
    std::function<void(const NativeSourceAttempt&)> sourceFinished;
    // Final close boundary only: caller may identify an already active or ONE
    // explicitly repaired SOURCE-only read. Returns its real request UUID,
    // never a proof. Inventory waits for that attempt's conserved publication
    // then acquires a NEW snapshot; first close read never delays STOP for it.
    std::function<QString(const QString&,const QString&,const SshObservationFrame&)> sourceBoundaryRead;
    std::function<void()> stopped;
    // Separate from an operator's initial-start ticket. Only the final actual
    // acquisition may feed automatic continuation; conservation is still raw
    // evidence, not allocation, physical Ready or independent coverage.
    std::function<void(const QString&,const QString&,const SshObservationFrame&,const QJsonObject&)> boundary;
    // Optional read-only profile qualification inside the already owned
    // writer slot, AFTER expensive lineage and BEFORE the actual new snapshot.
    // Must settle once; an error holds admission, never renews old evidence.
    std::function<void(const QString&,const QString&,std::function<void(QString)>)> beforeRefresh;
    // Independent SOURCE lifecycle/first-frame diagnostics; not a packet,
    // phase, writer operation, wall-clock refresh or ownership assertion.
    std::function<void(const QJsonObject&)> timing;
};
// Concrete in-process start/close inventory. ONE writer serializes acquisition
// with commands; ONE worker owns SOURCE/SQLite/Win32 handles. Finite snapshots
// use the existing bounded profile, never a made-up idle/default observation.
// Writer must outlive this object. API/callbacks creating thread only.
class NativeApplicationInventory final:public QObject {
public:
    NativeApplicationInventory(NativeApplicationWriter&,NativeApplicationInventoryConfig,
                               NativeApplicationInventoryCallbacks,QObject* parent=nullptr);
    ~NativeApplicationInventory() override;
    bool prepareClose(const QString& requestId);
    bool inspectStart(const QString& requestId,const QString& inventoryId);
    bool inspectBoundary(const QString& requestId,const QString& inventoryId);
    bool inspectClose(const QString& requestId,const QString& inventoryId);
    NativeSourceRuntime& source();
    // Interrupt only an admitted lineage read, so an operator STOP/close can
    // reach the same writer. Never stops a GPU or cancels the SOURCE stream.
    bool cancelLineage();
    // Local observation cleanup ONLY; does not stop any GPU. Caller must first
    // qualify its actual mining close and then await stopped() before quitting.
    void stop();
private:
    class Impl;
    std::unique_ptr<Impl> d_;
};
}

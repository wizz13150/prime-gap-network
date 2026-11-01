#pragma once
#include "booking/booking_store.h"
#include "mining/native_application_writer.h"
#include "mining/native_control_transport.h"
#include <QSet>

namespace pgn::mining {
// Shared read-only terminal consistency check. It grants no next-range or
// ticket allocation and does not replace caller-paid fresh/held RAW evidence.
QJsonObject verifiedSourceCompletionAtBoundary(const SshObservationFrame&,const QJsonObject& conservation);
// ONE application writer + SAME booking SQLite. This is not a poller, queue,
// allocator, timer/service, physical reconciler or transport retry mechanism.
// Writer and Store must outlive this QObject; use its creating thread only.
// submit MUST come from the final actual fresh acquisition callback AFTER
// expensive RAW-lineage work, not a relabelled cached snapshot. The caller
// pays transport/known producer, actual bytes/digests and registry acquisition;
// JSON and these consistency checks do NOT authenticate that acquisition.
class NativeQueuedBoundaryControl final : public QObject {
public:
    NativeQueuedBoundaryControl(NativeApplicationWriter&,booking::BookingStore&,
        SshObservationEndpoint,NativeControlTimeouts,int maximumObservationAgeMs,QObject* parent=nullptr);
    ~NativeQueuedBoundaryControl() override;
    // Defaults paused. Any change invalidates already queued admission even
    // if pause is subsequently cleared; no remote operation is implied.
    void setDesiredPaused(bool);
    bool submit(const QString& requestUUID,const QString& observationUUID,
        const SshObservationFrame& actualFresh,const QJsonObject& sourceConservation,
        const booking::InitialStartScope&,const booking::SourceRegistry& actualRegistry);
    // SOURCE receipt (only after commit), then local boundary/transport result.
    // accepted never means STARTED/physical ownership or permission to close.
    std::function<void(const QJsonObject&)> onPacket;
private:
    void owner() const;
    void publish(const QJsonObject&);
    NativeApplicationWriter& writer_;
    booking::BookingStore& store_;
    SshObservationEndpoint endpoint_;
    NativeControlTimeouts timeouts_;
    int maximumAge_;
    bool paused_{true},busy_{};
    quint64 generation_{};
    QSet<QString> consumedRequests_,consumedObservations_,consumedRuns_;
    // Owner-local proof that SQL committed but NO transport was attempted.
    // Never reconstructed from a PREPARED row or an uncertain SSH outcome.
    QSet<QString> unsentPreparedRuns_;
    NativeApplicationWriter::Done pendingDone_;
};
}

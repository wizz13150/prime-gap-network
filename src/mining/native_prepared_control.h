#pragma once
#include "booking/booking_store.h"
#include "mining/native_application_writer.h"
#include "mining/native_control_transport.h"
#include <QElapsedTimer>
#include <QSet>

namespace pgn::mining {
// Native composition of the SAME SQLite admission, one-use fresh observation,
// pure command encoder and bounded asynchronous transport. Integrated utility,
// not another process/service or an allocator. The application supplies its
// ONE writer also used for stop/close/dispatch/inventory operations.
// Writer and store MUST outlive this controller (destroy controller first).
// The observation caller MUST have completed actual transport acquisition
// after that writer's barrier. This local API cannot authenticate a DTO.
class NativePreparedControl final : public QObject {
public:
    NativePreparedControl(NativeApplicationWriter&,booking::BookingStore&,
        SshObservationEndpoint,NativeControlTimeouts,int maximumObservationAgeMs,QObject* parent=nullptr);
    ~NativePreparedControl() override;
    // Remembers a successful OFF start-inventory acquisition, never consumes
    // source bytes or produces Ready. Cold base is exact null, not a fake run.
    bool rememberStartInventory(const QString& request,const QString& inventory,const SshObservationFrame&);
    // Synchronous local STOP barrier, BEFORE enqueueing STOP. Retains every
    // SQLite receipt; invalidates armed/queued/in-flight local start intent.
    // Already sent commands are not undone. A fresh observation is required.
    void invalidateQueuedStarts();
    // Only existing eight-key start_prepared / ten-key resume_prepared ABI.
    // Consumed once even after an uncertain outcome; no retry/reprepare/Auto.
    bool submit(const QJsonObject& command);
    std::function<void(const QJsonObject&)> onPacket;
private:
    struct Ticket {QString request,inventory;std::optional<NativeObservedBase> base;};
    struct Outcome {QJsonObject packet;};
    void owner()const;
    void publish(const QJsonObject&);
    NativeApplicationWriter& writer_;
    booking::BookingStore& store_;
    SshObservationEndpoint endpoint_;
    NativeControlTimeouts timeouts_;
    int maximumAge_;
    QElapsedTimer age_;
    std::optional<Ticket> ticket_;
    QSet<QString> consumed_;
    QSet<QString> consumedInventories_;
    bool busy_{};
    quint64 generation_{};
    bool generationExhausted_{};
    NativeApplicationWriter::Done pendingDone_;
};
}

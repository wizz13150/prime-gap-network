#pragma once
#include "mining/native_prepared_control.h"
#include <QObject>
#include <functional>
#include <memory>
namespace pgn::mining {
struct NativeCloseInitialObservation;
// The unified application's real start/stop command path, not another service
// or an allocator. Shares its ONE writer + SAME BookingStore with inventory
// and queue. Writer/store must outlive this object; creating-thread API only.
class NativeApplicationControl final:public QObject {
public:
    NativeApplicationControl(NativeApplicationWriter&,booking::BookingStore&,
        SshObservationEndpoint,NativeControlTimeouts,int maximumObservationAgeMs,QObject* parent=nullptr);
    ~NativeApplicationControl() override;
    bool rememberStartInventory(const QString&,const QString&,const SshObservationFrame&);
    // Internal typed Initial acquisition ONLY. This records original data,
    // not Ready/STOP authority; a private one-use ticket is rechecked at STOP
    // writer entry against SAME store, original age and lifetime uncertainty.
    bool rememberCloseInitial(const NativeCloseInitialObservation&);
    // prepared initial/resume; explicit finite-card ON; exact observed STOP.
    // Naked productive START is refused, never falls back to configured work.
    // Each standalone STOP acquires identity within the writer before sending.
    // A valid, unused STOP immediately invalidates starts for that device,
    // even if writer admission fails. It never removes SQLite PREPARED state
    // or retracts a remote submission; an old reply remains an old report.
    // Unknown/empty-unlatched constant profile may use the DISTINCT configured
    // capability acquisition/latch. This never emits an OFF sample or accepted
    // physical ACK. Failed observed-identity CAS never falls back to that latch.
    // A fresh, typed already-disabled profile may avoid a remote STOP only if
    // the SAME store knows no PREPARED dispatch and no local START was seen.
    // Its ALREADY_OFF_OBSERVED report is neither a mutation ACK nor Ready;
    // historical constant reports require a separate fresh observation.
    bool submit(const QJsonObject& command);
    std::function<void(const QJsonObject&)> onPacket;
private:
    class Impl;
    std::unique_ptr<Impl> d_;
};
}
